/*
 * app_task.c: the app task. It owns the core model (g_app), LVGL and the ui, and is the only task that touches them.
 * Owner: lead developer.
 *
 * Each loop (about every 5 to 33 ms, as LVGL asks):
 *   1. drain the bus: button, orientation, Wi-Fi, time, calendar and notify events, and tb_bus_exec() jobs (the
 *      HTTP/USB router runs here, so its reads and writes of the model are consistent)
 *   2. let a held finger fire the hold, run core's timers (tb_app_tick) and the protocol's (net_api_tick)
 *   3. carry out core's effects (sound, backlight, rotation, power, Wi-Fi setup, calendar sync, saving)
 *   4. redraw what changed (ui_update), run LVGL (lv_timer_handler, which also reads the touch panel), save what's due
 *   5. feed the task watchdog
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_timer.h"
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
#include "ui.h"

static const char *TAG = "app";

tb_app_t g_app;
static bool s_clock_valid;

#define APP_CORE      1
#define APP_PRIORITY  5
#define LOOP_MIN_MS   5
#define LOOP_MAX_MS   33

tb_clock_t app_clock_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tb_clock_t c = {.mono = esp_timer_get_time() / 1000, .wall = tv.tv_sec, .valid = s_clock_valid};
    return c;
}

void app_clock_set_valid(bool valid)
{
    s_clock_valid = valid;
}

void app_apply_time_zone(const char *iana)
{
    const char *posix = iana && *iana ? cal_tz_posix_for(iana) : NULL;
    if (!posix) posix = "UTC0";
    setenv("TZ", posix, 1);
    tzset();
    cal_sync_set_time_zone(posix);
}

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
        s_clock_valid = true;
        if (ev->u.time.source != 2) board_rtc_save_now();   /* keep the RTC in step with SNTP or the Mac */
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
        break;
    case TB_EV_CAL_EVENT:
        tb_app_calendar_event(&g_app, (tb_cal_event_t)ev->u.i32, &now);
        break;
    case TB_EV_NOTIFY:
        tb_app_notify(&g_app, ev->u.text, &now);
        break;
    default:
        break;
    }
}

static void run_effects(void)
{
    tb_effect_t fx[TB_EFFECTS_MAX];
    int n = tb_app_take_effects(&g_app, fx, TB_EFFECTS_MAX);
    for (int i = 0; i < n; i++) {
        tb_clock_t now = app_clock_now();
        switch (fx[i].kind) {
        case TB_FX_CHIME: board_audio_chime(fx[i].arg != 0); break;
        case TB_FX_TICKING: board_audio_set_ticking(fx[i].arg); break;
        case TB_FX_BACKLIGHT: board_backlight_set((uint8_t)fx[i].arg); break;
        case TB_FX_ROTATE: board_display_set_flipped(fx[i].arg != 0); break;
        case TB_FX_POWER_OFF:
            settings_store_flush(&g_app);
            board_audio_stop();
            board_power_off();
            break;
        case TB_FX_RESTART:
            settings_store_flush(&g_app);
            board_audio_stop();
            board_restart();
            break;
        case TB_FX_WIFI_SETUP: net_setup_begin(); break;
        case TB_FX_WIFI_SKIP: net_setup_skip(); cal_sync_set_online(false); break;
        case TB_FX_WIFI_DONE: net_setup_done(); break;
        case TB_FX_CAL_SYNC: cal_sync_now(); break;
        case TB_FX_SAVE_SETTINGS: {
            static char last_tz[TB_TZ_NAME_BYTES], last_name[TB_DEVICE_NAME_BYTES];
            settings_store_mark_settings();
            if (strcmp(last_tz, g_app.set.device.time_zone)) {
                strcpy(last_tz, g_app.set.device.time_zone);
                app_apply_time_zone(last_tz);
            }
            if (strcmp(last_name, g_app.set.device.name)) {
                strcpy(last_name, g_app.set.device.name);
                net_name_changed(last_name);
            }
            break;
        }
        case TB_FX_SAVE_STATE: settings_store_mark_state(); break;
        case TB_FX_PAIRING_CANCELED: net_api_pairing_canceled(&now); break;
        case TB_FX_FORGET_DEVICES: net_api_forget_devices(&now); break;
        }
    }
}

static void app_task(void *arg)
{
    (void)arg;
    tb_bus_set_app_task();
    board_wdt_add_self();
    ui_set_pointer_cb(on_pointer, NULL);
    uint32_t wait = 0;
    for (;;) {
        tb_event_t ev;
        while (tb_bus_receive(&ev, wait)) {
            handle_event(&ev);
            wait = 0;
        }
        tb_clock_t now = app_clock_now();
        tb_app_pointer_poll(&g_app, &now);
        tb_app_tick(&g_app, &now);
        net_api_tick(&now);
        run_effects();
        ui_update(&g_app, &now);
        uint32_t next = lv_timer_handler();
        settings_store_poll(&g_app, now.mono);
        board_wdt_feed();
        wait = next < LOOP_MIN_MS ? LOOP_MIN_MS : next > LOOP_MAX_MS ? LOOP_MAX_MS : next;
    }
}

void app_task_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(app_task, "app", CONFIG_TINYBAR_APP_TASK_STACK, NULL, APP_PRIORITY, NULL, APP_CORE);
    configASSERT(ok == pdPASS);
}
