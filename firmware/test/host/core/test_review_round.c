/*
 * test_review_round.c: the rules the 2026-10-04 review round asked for (QA's gap tests, adapted to the fixes), plus
 * QA's sweep of every control in every state. Owner: lead developer (core's folder).
 */
#include <string.h>

#include "core_fixture.h"

/* ---------- Wi-Fi: Skip is remembered; the setup menu doesn't outlive setup; a dropped link ---------- */

/* decisions.md, Wi-Fi: "Skip uses the bar offline". net saves the skip (TB_FX_WIFI_SKIP) and main starts the next
 * boot with TB_WIFI_OFFLINE, as the mock-up's powerOn() keeps offline mode. */
TB_TEST(skipped_wifi_stays_skipped_after_a_restart)
{
    bench_t *b = bench_new_opts(false, true);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI_SKIP);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OFFLINE);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SKIP), 1);         /* net saves the skip when it gets this */
    hold(b);
    tap_tile_named(b, TB_ACT_POWER);
    tap_tile_named(b, TB_ACT_RESTART);
    TB_EQ_INT(fx_count(b, TB_FX_RESTART), 1);
    /* main: tb_app_init(&g_app, &settings, net_wifi_start_mode(), &now) */
    tb_settings_t s = b->a.set;
    tb_app_init(&b->a, &s, TB_WIFI_OFFLINE, &b->now);
    bench_run(b, TB_BOOT_SPLASH_MS + 100);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OFFLINE);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SETUP), 0);
    /* nothing saved, nothing skipped: the QR code; anything else counts as that too */
    tb_app_init(&b->a, &s, TB_WIFI_SETUP, &b->now);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_SETUP);
    tb_app_init(&b->a, &s, TB_WIFI_CONNECTING, &b->now);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_SETUP);
    tb_app_init(&b->a, &s, TB_WIFI_OK, &b->now);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);
}

/* Set up from the offline bar's Wi-Fi menu: TB_FX_WIFI_SETUP (net clears the saved skip). */
TB_TEST(set_up_again_after_skipping)
{
    bench_t *b = bench_new_opts(false, true);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI_SKIP);
    bench_clear_log(b);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    tap_tile_named(b, TB_ACT_WIFI_SETUP);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_SETUP);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SETUP), 1);
}

/* decisions.md: "Connected is the end of setup ... so Skip can't undo it". A setup menu opened on Connecting closes
 * once the join worked and setup moved on underneath it. */
TB_TEST(setup_menu_closes_when_setup_is_over)
{
    bench_t *b = bench_new_opts(false, true);
    tb_app_wifi_connecting(&b->a, "Office-WiFi", &b->now);
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_SETUP);
    tb_app_wifi_connected(&b->a, "Office-WiFi", "10.0.4.42", "minibar.local", &b->now);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_SETUP);            /* still on a setup screen (Connected) */
    bench_run(b, TB_CONNECTED_MS + 200);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SKIP), 0);
}

/* The Calendar tile agrees with Sync now when the link dropped: it can't sync, and it says when it last did. */
TB_TEST(calendar_tile_when_the_link_dropped)
{
    bench_t *b = bench_new();
    mt_t m = {60, 30, "Design review", NULL, false};
    cal_save(b, &m, 1);
    tb_app_set_calendar(&b->a, true, false, b->now.wall - 120, &b->now);
    bench_run(b, TB_TOAST_MS + 100);
    tb_app_wifi_link(&b->a, false, NULL, NULL, &b->now);
    hold(b);
    const tb_tile_t *t = tile_with(b, TB_ACT_SYNC);
    TB_TRUE(t != NULL);
    TB_EQ_STR(t->value, "Offline");
    TB_EQ_STR(t->foot, "synced 2m ago");
    TB_EQ_STR(tile_with(b, TB_ACT_WIFI)->value, "On");
    TB_EQ_STR(tile_with(b, TB_ACT_WIFI)->foot, "reconnecting");
    tap_tile_named(b, TB_ACT_SYNC);
    TB_EQ_STR(b->a.toast, "No Wi-Fi, can't sync");
    TB_EQ_INT(fx_count(b, TB_FX_CAL_SYNC), 0);
    /* back up: Sync again */
    tb_app_wifi_link(&b->a, true, "10.0.4.42", "minibar.local", &b->now);
    hold(b);
    TB_EQ_STR(tile_with(b, TB_ACT_SYNC)->value, "Sync");
    TB_EQ_STR(tile_with(b, TB_ACT_WIFI)->foot, "Office-WiFi");
}

/* ---------- Message with nothing ever set ---------- */

TB_TEST(message_shown_by_tap_stores_hello)
{
    bench_t *b = bench_new();
    TB_EQ_STR(b->a.message, "");
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_MESSAGE, NULL, NULL, true, &b->now), TB_E_NO_MESSAGE);
    go_status(b, TB_ST_AWAY);
    tap(b);                                             /* Away -> Message */
    TB_EQ_INT(b->a.idx, TB_ST_MESSAGE);
    TB_EQ_STR(b->a.message, "Hello");
    TB_EQ_INT(b->a.message_at, b->now.wall);
    TB_TRUE(fx_count(b, TB_FX_SAVE_STATE) >= 1);
    /* now the API has a message to show */
    go_status(b, TB_ST_BUSY);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_MESSAGE, NULL, NULL, true, &b->now), TB_OK);
    TB_EQ_STR(b->a.message, "Hello");
    /* a real message is never replaced */
    TB_EQ_INT(tb_app_remote_message(&b->a, "Back at 3", true, &b->now), TB_OK);
    go_status(b, TB_ST_AWAY);
    tap(b);
    TB_EQ_STR(b->a.message, "Back at 3");
}

/* ---------- The Remote and the API ---------- */

/* api.md 9.1: every action silences a ringing alarm first, even "pause" when nothing runs (which still answers
 * not_running). */
TB_TEST(api_pause_while_ringing_silences_first)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, POMO_FOCUS_MS + 100);
    TB_TRUE(b->a.ringing);
    bench_run(b, TB_TOAST_MS + 100);
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_PAUSE, 0, true, &b->now), TB_E_NOT_RUNNING);
    TB_FALSE(b->a.ringing);
    TB_EQ_STR(b->a.toast, "Alarm off");
    TB_TRUE(b->a.pomo.waiting);
    /* nothing ringing: the error changes nothing */
    uint32_t rev = b->a.rev;
    bench_run(b, TB_TOAST_MS + 100);
    rev = b->a.rev;
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_PAUSE, 0, true, &b->now), TB_E_NOT_RUNNING);
    TB_EQ_INT(b->a.rev, rev);
}

TB_TEST(api_brightness_toast_names_the_light_tile)
{
    bench_t *b = bench_new();
    tb_settings_patch_t p = {0};
    p.has_brightness = true;
    p.v.display.brightness = 70;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    TB_EQ_STR(b->a.toast, "Light 70%");
}

/* ---------- Pairing (decisions.md "Pairing", proposed) ---------- */

static bench_t *pairing_on(bench_t *b, const char *who, tb_pair_kind_t kind)
{
    tb_app_pairing_show(&b->a, "482913", who, kind, &b->now);
    bench_drain(b);
    return b;
}

TB_TEST(pairing_label_and_kind)
{
    bench_t *b = pairing_on(bench_new(), NULL, TB_PAIR_KIND_PHONE);
    TB_EQ_STR(b->a.pairing.who, "Phone");
    TB_EQ_INT(b->a.pairing.kind, TB_PAIR_KIND_PHONE);
    b = pairing_on(bench_new(), "", TB_PAIR_KIND_SCRIPT);
    TB_EQ_STR(b->a.pairing.who, "Script");
    b = pairing_on(bench_new(), NULL, TB_PAIR_KIND_OTHER);
    TB_EQ_STR(b->a.pairing.who, "Device");
    b = pairing_on(bench_new(), "iPhone", TB_PAIR_KIND_PHONE);
    TB_EQ_STR(b->a.pairing.who, "iPhone");
}

/* The 2 minutes count from when the code appears: a code that waits for the splash starts its clock after it. */
TB_TEST(pairing_counts_from_when_the_code_appears)
{
    bench_t *b = bench_new_opts(true, false);           /* still on the splash */
    TB_TRUE(b->a.booting);
    pairing_on(b, "Mac", TB_PAIR_KIND_MAC);
    TB_FALSE(tb_app_pairing_visible(&b->a));
    TB_EQ_INT(b->a.pairing.shown_at, 0);
    bench_run(b, TB_BOOT_SPLASH_MS + 100);
    TB_TRUE(tb_app_pairing_visible(&b->a));
    TB_TRUE(b->a.pairing.shown_at > T0_MONO + TB_BOOT_SPLASH_MS - 100);
    TB_EQ_INT(b->a.pairing.expires, b->a.pairing.shown_at + TB_PAIR_MS);
    TB_EQ_STR(b->a.toast, "");                           /* "Ready" doesn't cover the code */
}

/* A flip cancels the pairing ("Pairing canceled · Focus started"), then does what a flip always does. */
TB_TEST(pairing_flip_cancels_then_flips)
{
    bench_t *b = bench_new();
    go_status(b, TB_ST_POMODORO);
    bench_run(b, TB_TOAST_MS + 100);
    pairing_on(b, "Mac", TB_PAIR_KIND_MAC);
    bench_clear_log(b);
    flip(b);
    TB_FALSE(tb_app_pairing_visible(&b->a));
    TB_FALSE(b->a.pairing.active);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_CANCELED), 1);
    TB_EQ_INT(fx_count(b, TB_FX_ROTATE), 1);
    TB_EQ_STR(b->a.toast, "Pairing canceled \xC2\xB7 Focus started");
    TB_TRUE(b->a.pomo.running);
    /* nothing waiting: just "Pairing canceled" */
    b = bench_new();
    pairing_on(b, "Mac", TB_PAIR_KIND_MAC);
    /* underneath, the timer starts from the Remote (a tap would cancel the code) */
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_START, 0, true, &b->now), TB_OK);
    flip(b);
    TB_EQ_STR(b->a.toast, "Pairing canceled");
}

/* A code that arrives while the alarm rings stops the chime and flash; the Pomodoro keeps waiting; when pairing ends
 * the waiting screen chimes and flashes once, with no repeats. */
TB_TEST(pairing_holds_a_ringing_alarm_and_rings_once_after)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, POMO_FOCUS_MS + 100);
    TB_TRUE(b->a.ringing);
    pairing_on(b, "Mac", TB_PAIR_KIND_MAC);
    TB_FALSE(b->a.ringing);
    TB_TRUE(b->a.alarm_held_by_pairing);
    bench_clear_log(b);
    bench_run(b, 10000);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 0);
    TB_TRUE(b->a.pomo.waiting);
    tb_app_pairing_end(&b->a, TB_PAIR_END_PAIRED, NULL, &b->now);
    bench_drain(b);
    TB_EQ_STR(b->a.toast, "Paired \xC2\xB7 Mac");
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 1);
    TB_EQ_INT(b->a.flash_at, b->now.mono);
    TB_FALSE(b->a.ringing);                             /* one chime, no repeats */
    bench_run(b, 10000);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 1);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
}

/* A phase that ends while a code is showing waits too, silently, and rings once when pairing ends. */
TB_TEST(pairing_phase_end_waits)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, POMO_FOCUS_MS - 5000);
    pairing_on(b, "Mac", TB_PAIR_KIND_MAC);
    bench_clear_log(b);
    bench_run(b, 10000);
    TB_TRUE(b->a.pomo.waiting);
    TB_FALSE(b->a.ringing);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 0);
    TB_EQ_INT(b->a.flash_at, 0);
    TB_TRUE(tb_app_pairing_visible(&b->a));
    tap(b);                                             /* cancel */
    TB_EQ_STR(b->a.toast, "Pairing canceled");
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 1);
    TB_FALSE(b->a.ringing);
    /* with auto-start the next phase starts on time under the code, silently */
    b = bench_new();
    b->a.set.pomodoro.auto_start = true;
    pomo_start(b);
    bench_run(b, POMO_FOCUS_MS - 5000);
    pairing_on(b, "Mac", TB_PAIR_KIND_MAC);
    bench_clear_log(b);
    bench_run(b, 10000);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(b->a.pomo.phase, TB_PH_SHORT);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 0);
    TB_TRUE(tb_app_pairing_visible(&b->a));
}

/* A PWR press drops a held alarm's chime: the waiting screen shows, silently, when the screen is woken. */
TB_TEST(pairing_pwr_drops_the_held_chime)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, POMO_FOCUS_MS + 100);
    pairing_on(b, "Mac", TB_PAIR_KIND_MAC);
    bench_clear_log(b);
    pwr_press(b);
    TB_FALSE(b->a.pairing.active);
    TB_TRUE(b->a.off);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_CANCELED), 1);
    TB_FALSE(b->a.alarm_held_by_pairing);
    bench_run(b, 5000);
    pwr_press(b);                                       /* wake */
    bench_run(b, 5000);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 0);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_TRUE(b->a.pomo.waiting);
}

/* USB pairing while another device's code is on the screen: the code stays, and the confirmation follows it. */
TB_TEST(pairing_usb_keeps_another_code_on_screen)
{
    bench_t *b = pairing_on(bench_new(), "iPhone", TB_PAIR_KIND_PHONE);
    tb_app_pairing_end(&b->a, TB_PAIR_END_PAIRED_USB, "Mac", &b->now);
    TB_TRUE(tb_app_pairing_visible(&b->a));
    TB_EQ_STR(b->a.toast, "");
    TB_EQ_STR(b->a.pending_toast, "Paired \xC2\xB7 Mac \xC2\xB7 over USB");
    tb_app_pairing_end(&b->a, TB_PAIR_END_PAIRED, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Paired \xC2\xB7 iPhone");
    bench_run(b, TB_TOAST_MS + 100);
    TB_EQ_STR(b->a.toast, "Paired \xC2\xB7 Mac \xC2\xB7 over USB");
}

/* A change made on the Remote while a code shows happens underneath; its toast follows the pairing toast. */
TB_TEST(pairing_remote_change_shows_after_pairing)
{
    bench_t *b = pairing_on(bench_new(), "iPhone", TB_PAIR_KIND_PHONE);
    TB_EQ_INT(tb_app_remote_message(&b->a, "Back at 3", true, &b->now), TB_OK);
    TB_EQ_INT(b->a.idx, TB_ST_MESSAGE);                  /* underneath */
    TB_TRUE(tb_app_pairing_visible(&b->a));
    tb_settings_patch_t p = {0};
    p.has_auto_start = true;
    p.v.pomodoro.auto_start = true;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    TB_EQ_STR(b->a.toast, "");
    tb_app_pairing_end(&b->a, TB_PAIR_END_TIMEOUT, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Pairing timed out");
    /* anything that comes in during the pairing toast waits behind it */
    tb_app_notify(&b->a, "Removed Mac", &b->now);
    TB_EQ_STR(b->a.toast, "Pairing timed out");
    bench_run(b, TB_TOAST_MS + 100);
    TB_EQ_STR(b->a.toast, "Removed Mac");
}

/* A touch that began before the code appeared is ignored, so a tap meant for the screen underneath can't cancel. */
TB_TEST(pairing_ignores_a_touch_that_began_before_it)
{
    bench_t *b = bench_new();
    tb_app_pointer(&b->a, true, 200, 80, TB_TILE_NONE, &b->now);
    bench_run(b, 100);
    pairing_on(b, "Mac", TB_PAIR_KIND_MAC);
    bench_run(b, 100);
    tb_app_pointer(&b->a, false, 200, 80, TB_TILE_NONE, &b->now);
    TB_TRUE(tb_app_pairing_visible(&b->a));
    /* and a hold that began before it */
    tb_app_pointer(&b->a, true, 200, 80, TB_TILE_NONE, &b->now);
    tb_app_pairing_end(&b->a, TB_PAIR_END_CANCELED, NULL, &b->now);
    bench_run(b, 50);
    pairing_on(b, "Mac", TB_PAIR_KIND_MAC);
    bench_run(b, 700);
    tb_app_pointer(&b->a, false, 200, 80, TB_TILE_NONE, &b->now);
    TB_TRUE(tb_app_pairing_visible(&b->a));
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    /* a fresh tap cancels */
    tap(b);
    TB_FALSE(b->a.pairing.active);
}

/* Power off or Restart ends the pairing (net is told before the power goes). Since the pairing fix round it isn't a
 * failed pairing: net hears TB_FX_PAIRING_RESET, which also clears the back-off (api.md 4.9). */
TB_TEST(pairing_ends_with_power_off_and_restart)
{
    bench_t *b = pairing_on(bench_new(), "Mac", TB_PAIR_KIND_MAC);
    pwr_hold(b, 3100);
    TB_FALSE(b->a.pairing.active);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_CANCELED), 0);
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_RESET), 1);
    bench_run(b, TB_POWERING_OFF_MS + 100);
    TB_EQ_INT(fx_count(b, TB_FX_POWER_OFF), 1);
    /* PWR released early returns to the code */
    b = pairing_on(bench_new(), "Mac", TB_PAIR_KIND_MAC);
    pwr_hold(b, 1000);
    TB_TRUE(tb_app_pairing_visible(&b->a));
    TB_EQ_INT(fx_count(b, TB_FX_PAIRING_CANCELED), 0);
}

/* ---------- QA's sweep: no dead ends ---------- */

typedef enum {
    ST_STATUS, ST_MESSAGE, ST_CLOCK, ST_POMO_READY, ST_POMO_RUNNING, ST_POMO_PAUSED, ST_ALARM, ST_WAITING,
    ST_CALL, ST_MEETING, ST_CALL_ASIDE, ST_DARK, ST_DARK_POMO, ST_SETUP_QR, ST_CONNECTING, ST_CONNECTED, ST_FAILED,
    ST_OFFLINE, ST_LINK_DOWN, ST_PAIRING, ST_MENU_QUICK, ST_MENU_TIMER, ST_MENU_TSET, ST_MENU_WIFI, ST_MENU_FORGET,
    ST_MENU_POWER, ST_MENU_SETUP, ST_KEEP_HOLDING, ST_COUNT
} sw_state_t;

static const char *const ST_NAMES[ST_COUNT] = {
    "status", "message", "clock", "pomo ready", "pomo running", "pomo paused", "alarm ringing", "waiting (silent)",
    "call", "meeting", "call set aside", "dark", "dark (pomo running)", "setup QR", "connecting", "connected", "failed",
    "offline", "link down", "pairing", "quick menu", "timer menu", "timer settings", "wifi menu", "forget all",
    "power menu", "setup menu", "keep holding"};

typedef enum { C_TAP, C_SWIPE_NEXT, C_SWIPE_PREV, C_HOLD, C_FLIP, C_BOOT, C_PWR, C_COUNT, C_TILE0 = C_COUNT, C_ALL } sw_ctl_t;
static const char *const C_NAMES[C_COUNT] = {"tap", "swipe next", "swipe prev", "hold", "flip", "BOOT", "PWR press"};

static bool is_menu(sw_state_t st)
{
    return st >= ST_MENU_QUICK && st <= ST_MENU_SETUP;
}

static bench_t *reach(sw_state_t st)
{
    bench_t *b;
    switch (st) {
    case ST_STATUS: b = bench_new(); break;
    case ST_MESSAGE:
        b = bench_new();
        TB_EQ_INT(tb_app_remote_message(&b->a, "Back at 3", true, &b->now), TB_OK);
        break;
    case ST_CLOCK: b = bench_new(); go_status(b, TB_ST_CLOCK); break;
    case ST_POMO_READY: b = bench_new(); go_status(b, TB_ST_POMODORO); break;
    case ST_POMO_RUNNING: b = bench_new(); pomo_start(b); bench_run(b, 60000); break;
    case ST_POMO_PAUSED: b = bench_new(); pomo_start(b); bench_run(b, 60000); tap(b); break;
    case ST_ALARM: b = bench_new(); pomo_start(b); bench_run(b, POMO_FOCUS_MS + 100); break;
    case ST_WAITING: b = bench_new(); pomo_start(b); bench_run(b, POMO_FOCUS_MS + 100 + 61000); break;
    case ST_CALL: b = bench_new(); call_start(b, "Slack"); break;
    case ST_MEETING: {
        b = bench_new();
        mt_t m = {0, 30, "Design review", NULL, false};
        cal_save(b, &m, 1);
        break;
    }
    case ST_CALL_ASIDE: b = bench_new(); call_start(b, "Slack"); tap(b); break;
    case ST_DARK: b = bench_new(); pwr_press(b); break;
    case ST_DARK_POMO: b = bench_new(); pomo_start(b); bench_run(b, 1000); pwr_press(b); break;
    case ST_SETUP_QR: b = bench_new_opts(false, true); break;
    case ST_CONNECTING: b = bench_new_opts(false, true); tb_app_wifi_connecting(&b->a, "Office-WiFi", &b->now); break;
    case ST_CONNECTED:
        b = bench_new_opts(false, true);
        tb_app_wifi_connecting(&b->a, "Office-WiFi", &b->now);
        tb_app_wifi_connected(&b->a, "Office-WiFi", "10.0.4.42", "minibar.local", &b->now);
        break;
    case ST_FAILED:
        b = bench_new_opts(false, true);
        tb_app_wifi_connecting(&b->a, "Office-WiFi", &b->now);
        tb_app_wifi_failed(&b->a, "Office-WiFi", "Wrong password", &b->now);
        break;
    case ST_OFFLINE: b = bench_new_opts(false, true); hold(b); tap_tile_named(b, TB_ACT_WIFI_SKIP); break;
    case ST_LINK_DOWN: b = bench_new(); tb_app_wifi_link(&b->a, false, NULL, NULL, &b->now); break;
    case ST_PAIRING: b = pairing_on(bench_new(), "Mac", TB_PAIR_KIND_MAC); break;
    case ST_MENU_QUICK: b = bench_new(); hold(b); break;
    case ST_MENU_TIMER: b = bench_new(); pomo_start(b); bench_run(b, 60000); hold(b); break;
    case ST_MENU_TSET:
        b = bench_new(); go_status(b, TB_ST_POMODORO); hold(b); tap_tile_named(b, TB_ACT_TIMER_SETTINGS); break;
    case ST_MENU_WIFI: b = bench_new(); hold(b); tap_tile_named(b, TB_ACT_WIFI); break;
    case ST_MENU_FORGET:
        b = bench_new();
        tb_app_set_paired(&b->a, 2, "iPhone, Mac");
        hold(b);
        tap_tile_named(b, TB_ACT_WIFI);
        tap_tile_named(b, TB_ACT_DEVICES);
        break;
    case ST_MENU_POWER: b = bench_new(); hold(b); tap_tile_named(b, TB_ACT_POWER); break;
    case ST_MENU_SETUP: b = bench_new_opts(false, true); hold(b); break;
    case ST_KEEP_HOLDING:
        b = bench_new();
        tb_app_button(&b->a, TB_BTN_PWR_DOWN, &b->now);
        bench_run(b, 600);
        TB_EQ_INT(b->a.hold, TB_HOLD_KEEP_HOLDING);
        break;
    default: return NULL;
    }
    if (!is_menu(st)) bench_run(b, TB_TOAST_MS + 100);
    bench_clear_log(b);
    return b;
}

static void apply(bench_t *b, sw_ctl_t c)
{
    switch (c) {
    case C_TAP: tap(b); break;
    case C_SWIPE_NEXT: swipe(b, -120); break;
    case C_SWIPE_PREV: swipe(b, 120); break;
    case C_HOLD: hold(b); break;
    case C_FLIP: flip(b); break;
    case C_BOOT: boot_btn(b); break;
    case C_PWR: pwr_press(b); break;
    case C_TILE0: tap_tile(b, 0); break;    /* a menu's first tile (the setup menu's Skip) */
    default: break;
    }
}

/* No dead ends: every control changes something visible (the screen, an overlay, a toast) or asks for an effect.
 * The one exception is the mock-up's own rule that a swipe on an open menu is ignored ("Only taps act on a menu"),
 * which decisions.md is asked to record (Proposed, 2026-10-04). */
TB_TEST(every_control_in_every_state_does_something)
{
    int dead = 0;
    for (int st = 0; st < ST_COUNT; st++) {
        for (int c = 0; c < C_COUNT; c++) {
            if (st == ST_KEEP_HOLDING && c == C_PWR) continue;     /* PWR is already down */
            if (is_menu((sw_state_t)st) && (c == C_SWIPE_NEXT || c == C_SWIPE_PREV)) continue;
            bench_t *b = reach((sw_state_t)st);
            uint32_t rev = b->a.rev;
            char toast[TB_TOAST_BYTES];
            tb_strlcpy_test(toast, b->a.toast, sizeof toast);
            apply(b, (sw_ctl_t)c);
            bool changed = b->a.rev != rev || b->nlog > 0 || strcmp(toast, b->a.toast);
            if (!changed) {
                dead++;
                printf("    dead end: %s on %s\n", C_NAMES[c], ST_NAMES[st]);
            }
        }
    }
    TB_EQ_INT(dead, 0);
}

/* Every non-status screen has a way back to a status screen within two controls. */
TB_TEST(every_screen_has_a_way_back)
{
    static const sw_state_t screens[] = {ST_ALARM, ST_CALL, ST_MEETING, ST_DARK, ST_SETUP_QR, ST_CONNECTING,
                                         ST_CONNECTED, ST_FAILED, ST_PAIRING, ST_MENU_QUICK, ST_MENU_TIMER,
                                         ST_MENU_TSET, ST_MENU_WIFI, ST_MENU_FORGET, ST_MENU_POWER, ST_MENU_SETUP};
    for (size_t i = 0; i < sizeof screens / sizeof screens[0]; i++) {
        bool back = false;
        for (int c1 = 0; c1 < C_ALL && !back; c1++) {
            for (int c2 = -1; c2 < C_ALL && !back; c2++) {
                bench_t *b = reach(screens[i]);
                apply(b, (sw_ctl_t)c1);
                if (c2 >= 0) apply(b, (sw_ctl_t)c2);
                bench_run(b, 3500);     /* the Connected screen's 3 s */
                back = !b->a.off && !b->a.ringing && !tb_app_on_wifi_screen(&b->a) && b->a.menu.kind == TB_MENU_NONE &&
                       !tb_app_pairing_visible(&b->a) && !b->a.powered_off && !b->a.booting;
            }
        }
        if (!back) printf("    no way back from %s\n", ST_NAMES[screens[i]]);
        TB_TRUE(back);
    }
}
