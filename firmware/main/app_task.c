/*
 * app_task.c: the app task. It owns the core model (g_app), LVGL and the ui, and is the only task that touches them.
 * Owner: lead developer. See ARCHITECTURE.md sections 3 (tasks) and 5 (the bus).
 *
 * Each loop (about every 5 to 33 ms, as LVGL asks):
 *   1. drain the bus (at most APP_EVENTS_PER_LOOP): button, orientation, Wi-Fi, time, calendar and notify events,
 *      and tb_bus_exec() jobs (the HTTP/USB router runs here, so its reads and writes of the model are consistent)
 *   2. let a held finger fire the hold, run core's timers (tb_app_tick) and the protocol's (net_api_tick)
 *   3. under the display lock: carry out core's effects (sound, backlight, rotation, power, Wi-Fi setup, calendar
 *      sync, saving), redraw what changed (ui_update), run LVGL (lv_timer_handler: renders, flushes, reads the touch
 *      panel, whose samples reach core through ui's pointer callback), then the effects those touches caused, then
 *      let net check the setup network against core's screens (net_setup_follow)
 *   4. write the settings and state that are due (debounced NVS), feed the task watchdog
 *
 * LVGL locking: LVGL is built without an OS layer (CONFIG_LV_OS_NONE) and only this task calls it. The task holds
 * board_display_lock() while it does, so the rare code that must touch LVGL from elsewhere (none today; the board
 * bring-up self-test doesn't) has one lock to take. main's start-up calls LVGL before this task exists, which is safe.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "app.h"
#include "board.h"
#include "cal_sync.h"
#include "cal_tz.h"
#include "net.h"
#include "net_api.h"
#include "settings_store.h"
#include "tb_bus.h"
#include "tb_text.h"
#include "ui.h"

static const char *TAG = "app";

/* The model lives in PSRAM (about 11 KB, most of it today's meetings): only the app task touches it, never while the
 * flash cache is off, and internal RAM is the scarce one. */
EXT_RAM_BSS_ATTR tb_app_t g_app;

#define APP_CORE              1
#define APP_PRIORITY          5
#define LOOP_MIN_MS           5
#define LOOP_MAX_MS           33
#define APP_EVENTS_PER_LOOP   16      /* events and router jobs per loop; the rest wait for the next one */
#define APP_LVGL_LOCK_MS      1000
#define APP_SLOW_LOOP_MS      250     /* log a loop slower than this (bring-up: LVGL, NVS or a router job) */
#define APP_HEALTH_FIRST_MS   15000   /* the first health line, 15 s after start (Wi-Fi and mDNS are up by then) */
#define APP_HEALTH_EVERY_MS   60000   /* heap, stack and bus figures in the log, for bring-up */
#define CLOCK_VALID_AFTER     1735689600  /* 2025-01-01: same rule as net and calendar ("the clock was set") */

static volatile bool s_clock_valid;
static volatile bool s_net_ready;
static bool s_health_now;           /* log the health line at the next loop (after the first calendar fetch) */
static bool s_synced_once;

/* Wi-Fi effects core asked for before net was up (in practice never: the splash outlasts net's start-up). */
static tb_effect_kind_t s_deferred[4];
static int s_n_deferred;

/* What main last handed to the C library / net, so a settings save only re-applies what changed. */
static char s_tz_applied[TB_TZ_NAME_BYTES];
static char s_name_applied[TB_DEVICE_NAME_BYTES];

/* ---------- clock and time zone ---------- */

tb_clock_t app_clock_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tb_clock_t c = {.mono = esp_timer_get_time() / 1000, .wall = tv.tv_sec, .valid = s_clock_valid};
    return c;
}

void app_clock_set_valid(bool valid)
{
    /* The RTC set the clock, or it survived a restart or a deep sleep (the chip's RTC timer keeps the system time
     * through both). The second case matters after Restart when the RTC chip's time wasn't trusted. */
    s_clock_valid = valid || time(NULL) > CLOCK_VALID_AFTER;
}

void app_apply_time_zone(const char *iana)
{
    const char *posix = iana && *iana ? cal_tz_posix_for(iana) : NULL;
    if (iana && *iana && !posix) ESP_LOGW(TAG, "unknown time zone \"%s\"; using UTC", iana);
    if (!posix) posix = "UTC0";
    setenv("TZ", posix, 1);
    tzset();
    cal_sync_set_time_zone(posix);
    strncpy(s_tz_applied, iana ? iana : "", sizeof(s_tz_applied) - 1);
    s_tz_applied[sizeof(s_tz_applied) - 1] = '\0';
}

void app_net_ready(void)
{
    s_net_ready = true;
}

/* ---------- inputs ---------- */

static void on_pointer(void *ctx, bool pressed, int16_t x, int16_t y, int8_t tile)
{
    (void)ctx;
    tb_clock_t now = app_clock_now();
    tb_app_pointer(&g_app, pressed, x, y, tile, &now);
}

static void handle_event(const tb_event_t *ev)
{
    tb_clock_t now = app_clock_now();
    switch (ev->kind) {
    case TB_EV_BUTTON:
        tb_app_button(&g_app, (tb_button_t)ev->u.button, &now);
        break;
    case TB_EV_ORIENTATION:
        tb_app_flip(&g_app, ev->u.orient.flipped, ev->u.orient.initial, &now);
        break;
    case TB_EV_WIFI:
        switch (ev->u.wifi.ev) {
        case TB_WIFI_EV_CONNECTING:
            tb_app_wifi_connecting(&g_app, ev->u.wifi.ssid, &now);
            break;
        case TB_WIFI_EV_CONNECTED:
            tb_app_wifi_connected(&g_app, ev->u.wifi.ssid, ev->u.wifi.ip, ev->u.wifi.host, &now);
            cal_sync_set_online(true);
            break;
        case TB_WIFI_EV_FAILED:
            tb_app_wifi_failed(&g_app, ev->u.wifi.ssid, ev->u.wifi.error, &now);
            break;
        case TB_WIFI_EV_LINK_UP:
            /* with several saved networks the one that came up isn't always the one setup joined */
            if (ev->u.wifi.ssid[0]) tb_strlcpy(g_app.wifi_ssid, ev->u.wifi.ssid, sizeof g_app.wifi_ssid);
            tb_app_wifi_link(&g_app, true, ev->u.wifi.ip, ev->u.wifi.host, &now);
            cal_sync_set_online(true);
            break;
        case TB_WIFI_EV_LINK_DOWN:
            tb_app_wifi_link(&g_app, false, NULL, NULL, &now);
            cal_sync_set_online(false);
            break;
        }
        break;
    case TB_EV_TIME_SET:
        /* 1 SNTP, 3 the Mac's hello (net); 2 would be the RTC, which main reads at boot itself. */
        ESP_LOGI(TAG, "clock set (%s)", ev->u.time.source == 1 ? "network time" : ev->u.time.source == 3 ? "Mac" : "RTC");
        s_clock_valid = true;
        if (ev->u.time.source != 2) board_rtc_save_now();   /* keep the RTC in step for the next power-up */
        break;
    case TB_EV_USB_LINK:
        ESP_LOGI(TAG, "USB protocol link %s", ev->u.flag ? "up" : "down");
        break;
    case TB_EV_CAL_MEETINGS: {
        tb_cal_meetings_t *m = ev->u.ptr;
        if (m) {
            tb_app_set_meetings(&g_app, m->m, m->n, &now);
            free(m);
        }
        break;
    }
    case TB_EV_CAL_STATUS:
        tb_app_set_calendar(&g_app, ev->u.cal.saved, ev->u.cal.checking, ev->u.cal.last_sync, &now);
        /* The first TLS fetch is the high-water mark for internal RAM: log the figures right after it. */
        if (!s_synced_once && !ev->u.cal.checking && ev->u.cal.last_sync) {
            s_synced_once = true;
            s_health_now = true;
        }
        break;
    case TB_EV_CAL_EVENT:
        tb_app_calendar_event(&g_app, (tb_cal_event_t)ev->u.i32, &now);
        break;
    case TB_EV_CAL_LIST: {
        tb_cal_list_t *l = ev->u.ptr;
        if (l) {
            tb_app_set_cal_list(&g_app, l->c, &now);
            free(l);
        }
        break;
    }
    case TB_EV_CAL_REMOVED: {
        tb_cal_removed_t *r = ev->u.ptr;
        if (r) {
            tb_app_calendar_removed(&g_app, r->name, r->list.c, r->meetings.m, r->meetings.n, &now);
            free(r);
        }
        break;
    }
    case TB_EV_NOTIFY:
        tb_app_notify(&g_app, ev->u.text, &now);
        break;
    case TB_EV_NONE:    /* a tb_bus_exec() job that already ran */
    case TB_EV_EXEC:
    default:
        break;
    }
}

/* ---------- effects ---------- */

static void wifi_effect(tb_effect_kind_t kind)
{
    switch (kind) {
    case TB_FX_WIFI_SETUP: net_setup_begin(); break;
    case TB_FX_WIFI_SKIP: net_setup_skip(); break;
    case TB_FX_WIFI_DONE: net_setup_done(); break;
    default: break;
    }
}

static void settings_saved(void)
{
    settings_store_mark_settings();
    const tb_settings_t *s = &g_app.set;
    if (strcmp(s_tz_applied, s->device.time_zone)) app_apply_time_zone(s->device.time_zone);
    if (strcmp(s_name_applied, s->device.name)) {
        strcpy(s_name_applied, s->device.name);
        net_name_changed(s_name_applied);
    }
}

/* Power goes off or the chip restarts: save what's pending, go quiet, and let the office Wi-Fi know we're leaving. */
static void before_power_change(void)
{
    settings_store_flush(&g_app);
    board_audio_stop();
    if (s_net_ready) esp_wifi_stop();   /* IDF asks for it before deep sleep; esp_restart does it itself */
}

static void run_effects(void)
{
    tb_effect_t fx[TB_EFFECTS_MAX];
    int n = tb_app_take_effects(&g_app, fx, TB_EFFECTS_MAX);
    for (int i = 0; i < n; i++) {
        tb_clock_t now = app_clock_now();
        switch (fx[i].kind) {
        case TB_FX_CHIME: board_audio_chime(fx[i].arg != 0); break;
        case TB_FX_MEETING_CHIME: board_audio_meeting_chime(); break;
        case TB_FX_TICKING: board_audio_set_ticking(fx[i].arg); break;
        case TB_FX_BACKLIGHT: board_backlight_set((uint8_t)fx[i].arg); break;
        case TB_FX_ROTATE: board_display_set_flipped(fx[i].arg != 0); break;
        case TB_FX_POWER_OFF:
            ESP_LOGW(TAG, "powering off");
            before_power_change();
            board_power_off();      /* doesn't return: deep sleep on USB, power cut on battery */
            break;
        case TB_FX_RESTART:
            ESP_LOGW(TAG, "restarting");
            before_power_change();
            board_restart();        /* doesn't return */
            break;
        case TB_FX_WIFI_SETUP:
        case TB_FX_WIFI_SKIP:
        case TB_FX_WIFI_DONE:
            if (fx[i].kind == TB_FX_WIFI_SKIP) cal_sync_set_online(false);
            if (s_net_ready) {
                wifi_effect(fx[i].kind);
            } else if (s_n_deferred < (int)(sizeof s_deferred / sizeof s_deferred[0])) {
                s_deferred[s_n_deferred++] = fx[i].kind;
            } else {
                ESP_LOGW(TAG, "Wi-Fi effect %d dropped: net isn't up yet", (int)fx[i].kind);
            }
            break;
        case TB_FX_CAL_SYNC:
            /* core checked Wi-Fi and the address first; if the calendar still refuses (it hasn't heard the link is
             * up, or the address went meanwhile), say so instead of leaving the quick menu's "Sync…" without an
             * answer. */
            if (cal_sync_now() != ESP_OK) tb_app_calendar_event(&g_app, TB_CALEV_SYNC_FAILED, &now);
            break;
        case TB_FX_SAVE_SETTINGS: settings_saved(); break;
        case TB_FX_SAVE_STATE: settings_store_mark_state(); break;
        case TB_FX_PAIRING_CANCELED: net_api_pairing_canceled(&now); break;
        case TB_FX_PAIRING_RESET: net_api_pairing_reset(&now); break;
        case TB_FX_FORGET_DEVICES: net_api_forget_devices(&now); break;
        }
    }
}

/* ---------- bring-up figures ---------- */

/* Every task we know of by name, MiniBar's and ESP-IDF's: the bytes of stack it has never used. A name that doesn't
 * exist (mDNS before the first address, the setup DNS before setup) is left out. */
static const char *const TASKS[] = {"app", "main", "net", "usb_rx", "dns", "httpd", "imu", "audio", "cal_sync",
                                    "tiT", "sys_evt", "wifi", "esp_timer", "mdns", "Tmr Svc", "ipc0", "ipc1", "IDLE0",
                                    "IDLE1"};

static void log_stacks(void)
{
    char line[400];
    int n = 0;
    for (size_t i = 0; i < sizeof TASKS / sizeof TASKS[0] && n < (int)sizeof line - 32; i++) {
        TaskHandle_t t = xTaskGetHandle(TASKS[i]);
        if (t) n += snprintf(line + n, sizeof line - n, " %s %u", TASKS[i], (unsigned)uxTaskGetStackHighWaterMark(t));
    }
    ESP_LOGI(TAG, "stack left (bytes):%s", n ? line : " none found");
}

static void log_health(tb_ms_t now_ms)
{
    static tb_ms_t next = APP_HEALTH_FIRST_MS;
    if (now_ms < next && !s_health_now) return;
    next = now_ms + APP_HEALTH_EVERY_MS;
    s_health_now = false;
    uint32_t frames, avg_ms, max_ms;
    board_display_stats(&frames, &avg_ms, &max_ms);
    ESP_LOGI(TAG, "health: internal %u free (low %u, largest %u), PSRAM %u free, bus dropped %u, effects dropped %u, "
                  "%lu frames (rotate + send: average %lu ms, slowest %lu ms lately)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)tb_bus_dropped(), (unsigned)g_app.fx_dropped, (unsigned long)frames, (unsigned long)avg_ms,
             (unsigned long)max_ms);
    log_stacks();
}

/* The time a frame takes on the app task: LVGL's render plus the flush (rotate + send), averaged over the frames of
 * each minute, at INFO so it shows in a normal log. The decision to keep LVGL out of IRAM rests on it. */
static void note_frame_time(tb_ms_t handler_ms, uint32_t frames_before)
{
    static uint32_t n;
    static tb_ms_t total, worst, next = APP_HEALTH_FIRST_MS;
    if (board_display_frame_count() != frames_before) {
        n++;
        total += handler_ms;
        if (handler_ms > worst) worst = handler_ms;
    }
    tb_ms_t now = esp_timer_get_time() / 1000;
    if (now < next) return;
    next = now + APP_HEALTH_EVERY_MS;
    if (n) ESP_LOGI(TAG, "frame time (render + rotate + send): %lu frames, average %lld ms, slowest %lld ms",
                    (unsigned long)n, (long long)(total / n), (long long)worst);
    n = 0;
    total = worst = 0;
}

/* ---------- the loop ---------- */

static void app_task(void *arg)
{
    (void)arg;
    tb_bus_set_app_task();
    if (board_wdt_add_self() != ESP_OK) ESP_LOGW(TAG, "couldn't subscribe to the task watchdog");
    ui_set_pointer_cb(on_pointer, NULL);
    strncpy(s_name_applied, g_app.set.device.name, sizeof(s_name_applied) - 1);   /* net_init() gets it from g_app */
    tb_ms_t slow_logged = 0;
    uint32_t wait = 0;
    for (;;) {
        tb_event_t ev;
        int handled = 0;
        while (handled < APP_EVENTS_PER_LOOP && tb_bus_receive(&ev, handled == 0 ? wait : 0)) {
            handle_event(&ev);
            handled++;
        }

        tb_ms_t t0 = esp_timer_get_time() / 1000;
        tb_clock_t now = app_clock_now();
        if (s_net_ready && s_n_deferred) {
            for (int i = 0; i < s_n_deferred; i++) wifi_effect(s_deferred[i]);
            s_n_deferred = 0;
        }
        tb_app_pointer_poll(&g_app, &now);
        tb_app_tick(&g_app, &now);
        if (s_net_ready) net_api_tick(&now);

        uint32_t next = LOOP_MIN_MS;
        if (board_display_lock(APP_LVGL_LOCK_MS)) {
            run_effects();
            ui_update(&g_app, &now);
            uint32_t frames_before = board_display_frame_count();
            tb_ms_t h0 = esp_timer_get_time() / 1000;
            next = lv_timer_handler();
            note_frame_time(esp_timer_get_time() / 1000 - h0, frames_before);
            run_effects();      /* what this frame's touch samples asked for (a wake, a chime), without a loop's delay */
            /* Core's effects are all out now, so net can compare: a setup network core no longer shows is closed
             * even if its TB_FX_WIFI_DONE was lost (net.h). */
            if (s_net_ready && !s_n_deferred) net_setup_follow(g_app.wifi_mode, now.mono);
            board_display_unlock();
        } else {
            ESP_LOGW(TAG, "display lock busy; effects and the frame wait for the next loop");
        }
        settings_store_poll(&g_app, now.mono);
        board_wdt_feed();

        tb_ms_t t1 = esp_timer_get_time() / 1000;
        if (t1 - t0 > APP_SLOW_LOOP_MS && t1 - slow_logged > 10000) {
            slow_logged = t1;
            ESP_LOGW(TAG, "slow loop: %d ms", (int)(t1 - t0));
        }
        log_health(t1);
        wait = next < LOOP_MIN_MS ? LOOP_MIN_MS : next > LOOP_MAX_MS ? LOOP_MAX_MS : next;
        /* A full budget means the queue never ran dry, so this loop never blocked: give core 1's idle task a tick,
         * or a flood of requests would trip the idle-task watchdog. */
        if (handled == APP_EVENTS_PER_LOOP) vTaskDelay(1);
    }
}

void app_task_start(void)
{
    /* The stack stays in internal RAM: this task writes NVS (flash), which can't run with a stack in PSRAM. */
    BaseType_t ok = xTaskCreatePinnedToCore(app_task, "app", CONFIG_TINYBAR_APP_TASK_STACK, NULL, APP_PRIORITY, NULL,
                                            APP_CORE);
    configASSERT(ok == pdPASS);
}
