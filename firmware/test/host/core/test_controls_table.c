/*
 * The mock-up's controls table ("How the controls work"), one test per row, every column:
 *   Status screen | Pomodoro screen | Call or meeting (automatic) | Alarm ringing | Screen dark | Powered off
 * Plus the copy each one shows.
 */
#include <string.h>

#include "core_fixture.h"

/* ---- helpers to reach each column ---- */

static bench_t *on_status(void)
{
    bench_t *b = bench_new();
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    return b;
}

static bench_t *on_pomodoro_running(void)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, 60000);
    bench_clear_log(b);
    return b;
}

static bench_t *on_call(void)
{
    bench_t *b = bench_new();
    call_start(b, "Slack");
    bench_run(b, TB_TOAST_MS + 100);
    bench_clear_log(b);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);
    return b;
}

static bench_t *on_meeting(void)
{
    bench_t *b = bench_new();
    mt_t m = {0, 30, "Design review", "Room 4", false};
    cal_save(b, &m, 1);
    bench_run(b, TB_TOAST_MS + 100);
    bench_clear_log(b);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_MEETING);
    return b;
}

static bench_t *ringing(void)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, POMO_FOCUS_MS + 100);
    TB_TRUE(b->a.ringing);
    TB_TRUE(b->a.pomo.waiting);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    bench_clear_log(b);
    return b;
}

static bench_t *dark(void)
{
    bench_t *b = bench_new();
    pwr_press(b);
    TB_TRUE(b->a.off);
    TB_EQ_INT(fx_last(b, TB_FX_BACKLIGHT), 0);
    bench_clear_log(b);
    return b;
}

static bench_t *powered_off(void)
{
    bench_t *b = bench_new();
    pwr_hold(b, 3100);
    bench_run(b, TB_POWERING_OFF_MS + 100);
    TB_TRUE(b->a.powered_off);
    TB_EQ_INT(fx_count(b, TB_FX_POWER_OFF), 1);
    bench_clear_log(b);
    return b;
}

static void expect_nothing_after_power_off(bench_t *b)
{
    uint32_t rev = b->a.rev;
    tb_status_t idx = b->a.idx;
    TB_EQ_INT(b->nlog, 0);
    TB_EQ_INT(b->a.idx, idx);
    TB_EQ_INT(b->a.rev, rev);
    TB_FALSE(b->a.menu.kind);
}

/* ---- Tap ---- */

TB_TEST(table_tap)
{
    /* Status screen: next status. */
    bench_t *b = on_status();
    tap(b);
    TB_EQ_INT(b->a.idx, TB_ST_MEETING);
    TB_EQ_INT(b->a.last_status, TB_ST_MEETING);
    TB_EQ_STR(b->a.toast, "");              /* the new screen is the confirmation */
    go_status(b, TB_ST_CLOCK);
    tap(b);
    TB_EQ_INT(b->a.idx, TB_ST_AVAILABLE);   /* Clock wraps to Available */

    /* Pomodoro screen: start or pause. */
    b = bench_new();
    go_status(b, TB_ST_POMODORO);
    tap(b);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_STR(b->a.toast, "Focus started");
    bench_run(b, 5000);
    tap(b);
    TB_FALSE(b->a.pomo.running);
    TB_EQ_STR(b->a.toast, "Paused");
    tap(b);
    TB_EQ_STR(b->a.toast, "Focus resumed");

    /* Call or meeting: back to your status, set aside. */
    b = on_call();
    tap(b);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    TB_EQ_INT(tb_app_aside_kind(&b->a, &b->now), TB_AUTO_CALL);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    TB_EQ_STR(b->a.toast, "Call set aside \xC2\xB7 hold to show it again");
    b = on_meeting();
    tap(b);
    TB_EQ_INT(tb_app_aside_kind(&b->a, &b->now), TB_AUTO_MEETING);
    TB_EQ_STR(b->a.toast, "Meeting set aside \xC2\xB7 hold to show it again");

    /* Alarm ringing, on the Pomodoro screen: silences it and starts the next phase. */
    b = ringing();
    tap(b);
    TB_FALSE(b->a.ringing);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(b->a.pomo.phase, TB_PH_SHORT);
    TB_EQ_STR(b->a.toast, "Short break started");
    /* ...and on a status screen (only reachable by the mock-up's tap rule): silences it only. */
    b = on_status();
    b->a.ringing = true;
    b->a.ring_until = b->now.mono + TB_ALARM_LIMIT_MS;
    b->a.next_ring = b->now.mono + TB_ALARM_REPEAT_MS;
    tap(b);
    TB_FALSE(b->a.ringing);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    TB_EQ_STR(b->a.toast, "Alarm off");

    /* Screen dark: wakes the screen, nothing else. */
    b = dark();
    tap(b);
    TB_FALSE(b->a.off);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    TB_EQ_INT(fx_last(b, TB_FX_BACKLIGHT), 70);

    /* Powered off: nothing. */
    b = powered_off();
    tap(b);
    expect_nothing_after_power_off(b);
}

/* ---- Swipe ---- */

TB_TEST(table_swipe)
{
    bench_t *b = on_status();
    swipe(b, -80);
    TB_EQ_INT(b->a.idx, TB_ST_MEETING);    /* towards the left: next */
    swipe(b, 80);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);       /* towards the right: previous */
    go_status(b, TB_ST_AVAILABLE);
    swipe(b, 80);
    TB_EQ_INT(b->a.idx, TB_ST_CLOCK);      /* wraps backwards */

    /* Pomodoro screen: changes status; the timer keeps running. */
    b = on_pomodoro_running();
    swipe(b, -80);
    TB_EQ_INT(b->a.idx, TB_ST_AWAY);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(b->a.last_status, TB_ST_AWAY);

    /* Call: previous or next counted from your status, and the call is set aside. */
    b = on_call();
    swipe(b, -80);
    TB_EQ_INT(b->a.idx, TB_ST_MEETING);
    TB_EQ_INT(tb_app_aside_kind(&b->a, &b->now), TB_AUTO_CALL);
    TB_EQ_STR(b->a.toast, "Call set aside \xC2\xB7 In a meeting");
    b = on_meeting();
    swipe(b, 80);
    TB_EQ_INT(b->a.idx, TB_ST_AVAILABLE);
    TB_EQ_STR(b->a.toast, "Meeting set aside \xC2\xB7 Available");

    /* Alarm: silences it and changes status. */
    b = ringing();
    swipe(b, -80);
    TB_FALSE(b->a.ringing);
    TB_EQ_INT(b->a.idx, TB_ST_AWAY);
    TB_TRUE(b->a.pomo.waiting);            /* the next phase still waits */

    /* Dark: wakes only. */
    b = dark();
    swipe(b, -80);
    TB_FALSE(b->a.off);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);

    b = powered_off();
    swipe(b, -80);
    expect_nothing_after_power_off(b);
}

/* ---- Hold ---- */

TB_TEST(table_hold)
{
    bench_t *b = on_status();
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);

    /* Pomodoro screen: the timer menu. */
    b = on_pomodoro_running();
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_TIMER);
    TB_TRUE(tile_with(b, TB_ACT_TIMER_SETTINGS) != NULL);
    TB_TRUE(tile_with(b, TB_ACT_ADD) != NULL);     /* mid-phase */

    /* ...with a call set aside, Show again replaces Settings (still five tiles). */
    b = on_pomodoro_running();
    call_start(b, NULL);
    tap(b);                                 /* back to your status: the Pomodoro, paused by the call */
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_TIMER);
    TB_TRUE(tile_with(b, TB_ACT_TIMER_SETTINGS) == NULL);
    TB_TRUE(tile_with(b, TB_ACT_SHOW_AGAIN) != NULL);
    TB_EQ_INT(b->a.menu.n, 5);

    /* Call or meeting: the quick menu, even when your status is the Pomodoro. */
    b = on_pomodoro_running();
    call_start(b, "Zoom");
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);   /* holding doesn't set it aside */

    /* Alarm: silences it and opens the menu (the timer menu, on the Pomodoro screen). */
    b = ringing();
    hold(b);
    TB_FALSE(b->a.ringing);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_TIMER);
    TB_TRUE(b->a.pomo.waiting);
    TB_TRUE(tile_with(b, TB_ACT_ADD) == NULL);     /* no +5 while waiting */

    /* Dark: wakes only. */
    b = dark();
    hold(b);
    TB_FALSE(b->a.off);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);

    b = powered_off();
    hold(b);
    expect_nothing_after_power_off(b);
}

/* ---- Flip ---- */

TB_TEST(table_flip)
{
    /* Status screen: turns the layout and starts what the Pomodoro is waiting for (a fresh session). */
    bench_t *b = on_status();
    flip(b);
    TB_TRUE(b->a.flipped);
    TB_EQ_INT(fx_last(b, TB_FX_ROTATE), 1);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_EQ_STR(b->a.toast, "Focus started");
    /* With the timer already running on another status: left alone, the status stays. */
    swipe(b, -80);
    TB_EQ_INT(b->a.idx, TB_ST_AWAY);
    tb_ms_t left = b->a.pomo.remaining_ms;
    flip(b);
    TB_FALSE(b->a.flipped);
    TB_EQ_INT(fx_last(b, TB_FX_ROTATE), 0);
    TB_EQ_INT(b->a.idx, TB_ST_AWAY);
    TB_EQ_INT(b->a.pomo.remaining_ms, left);
    TB_EQ_STR(b->a.toast, "Focus already running");

    /* Pomodoro screen: the same; a running timer is left alone (a flip never skips). */
    b = on_pomodoro_running();
    left = b->a.pomo.remaining_ms;
    flip(b);
    TB_EQ_INT(b->a.pomo.remaining_ms, left);
    TB_EQ_INT(b->a.pomo.phase, TB_PH_FOCUS);
    TB_EQ_STR(b->a.toast, "Focus already running");
    /* A paused timer resumes. */
    tap(b);
    flip(b);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_STR(b->a.toast, "Focus resumed");

    /* Call: turns the layout, starts or resumes the Pomodoro and shows it (sets the call aside). */
    b = on_pomodoro_running();
    call_start(b, "Slack");
    TB_FALSE(b->a.pomo.running);            /* the call paused it */
    flip(b);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_EQ_INT(tb_app_aside_kind(&b->a, &b->now), TB_AUTO_CALL);
    TB_EQ_STR(b->a.toast, "Focus resumed");
    b = on_meeting();
    flip(b);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    TB_EQ_STR(b->a.toast, "Focus started");
    /* A call with the Pomodoro already running and set aside: shows the Pomodoro anyway. */

    /* Alarm: silences it and starts the next phase. */
    b = ringing();
    flip(b);
    TB_FALSE(b->a.ringing);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(b->a.pomo.phase, TB_PH_SHORT);
    TB_EQ_STR(b->a.toast, "Alarm off \xC2\xB7 Short break started");

    /* Dark: wakes the screen, then the same. */
    b = dark();
    flip(b);
    TB_FALSE(b->a.off);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_STR(b->a.toast, "Focus started");

    b = powered_off();
    flip(b);
    expect_nothing_after_power_off(b);
}

/* ---- BOOT ---- */

TB_TEST(table_boot)
{
    bench_t *b = on_status();
    boot_btn(b);
    TB_EQ_INT(b->a.idx, TB_ST_MEETING);

    /* Pomodoro screen: next status (the timer keeps running). */
    b = on_pomodoro_running();
    boot_btn(b);
    TB_EQ_INT(b->a.idx, TB_ST_AWAY);
    TB_TRUE(b->a.pomo.running);

    /* Call or meeting: back to your status, set aside. */
    b = on_call();
    boot_btn(b);
    TB_EQ_INT(tb_app_aside_kind(&b->a, &b->now), TB_AUTO_CALL);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    TB_EQ_STR(b->a.toast, "Call set aside \xC2\xB7 hold to show it again");

    /* Alarm: silences it, nothing else (the first press only answers the alarm). */
    b = ringing();
    boot_btn(b);
    TB_FALSE(b->a.ringing);
    TB_TRUE(b->a.pomo.waiting);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_EQ_STR(b->a.toast, "Alarm off");

    /* Dark: wakes only. */
    b = dark();
    boot_btn(b);
    TB_FALSE(b->a.off);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);

    b = powered_off();
    boot_btn(b);
    expect_nothing_after_power_off(b);
}

/* ---- PWR press ---- */

TB_TEST(table_pwr_press)
{
    bench_t *b = on_status();
    tb_app_notify(&b->a, "Something", &b->now);
    TB_EQ_STR(b->a.toast, "Something");
    pwr_press(b);
    TB_TRUE(b->a.off);
    TB_EQ_INT(fx_last(b, TB_FX_BACKLIGHT), 0);
    TB_EQ_STR(b->a.toast, "");              /* going dark hides the toast */
    TB_EQ_INT(b->a.hold, TB_HOLD_NONE);

    b = on_pomodoro_running();
    pwr_press(b);
    TB_TRUE(b->a.off);
    TB_TRUE(b->a.pomo.running);             /* a dark bar keeps following the timer */

    b = on_call();
    pwr_press(b);
    TB_TRUE(b->a.off);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);

    /* Alarm: silences it; the screen stays on. */
    b = ringing();
    pwr_press(b);
    TB_FALSE(b->a.ringing);
    TB_FALSE(b->a.off);
    TB_EQ_STR(b->a.toast, "Alarm off");

    /* Dark: screen on. */
    b = dark();
    pwr_press(b);
    TB_FALSE(b->a.off);
    TB_EQ_INT(fx_last(b, TB_FX_BACKLIGHT), 70);

    /* Powered off: "Starts up" is the hardware waking from deep sleep into a fresh boot (board_power_off()); core
     * itself does nothing. */
    b = powered_off();
    pwr_press(b);
    expect_nothing_after_power_off(b);
}

/* ---- PWR hold 3 s ---- */

static void expect_power_off_by_hold(bench_t *b)
{
    tb_app_button(&b->a, TB_BTN_PWR_DOWN, &b->now);
    bench_run(b, 350);
    TB_EQ_INT(b->a.hold, TB_HOLD_NONE);
    bench_run(b, 100);
    TB_EQ_INT(b->a.hold, TB_HOLD_KEEP_HOLDING);        /* "Keep holding" from 400 ms */
    bench_run(b, 2600);
    TB_EQ_INT(b->a.hold, TB_HOLD_POWERING_OFF);        /* "Powering off" at 3 s */
    TB_FALSE(b->a.ringing);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    tb_app_button(&b->a, TB_BTN_PWR_UP, &b->now);       /* the release no longer matters */
    bench_drain(b);
    TB_EQ_INT(fx_count(b, TB_FX_POWER_OFF), 0);
    bench_run(b, TB_POWERING_OFF_MS + 50);
    TB_EQ_INT(fx_count(b, TB_FX_POWER_OFF), 1);
    TB_TRUE(b->a.powered_off);
    TB_EQ_INT(b->a.hold, TB_HOLD_NONE);
}

TB_TEST(table_pwr_hold)
{
    bench_t *b = on_status();
    expect_power_off_by_hold(b);

    /* Pomodoro: the run stops when the power is cut; today's tomatoes stay. */
    b = on_pomodoro_running();
    b->a.pomo.done_today = 2;
    expect_power_off_by_hold(b);
    TB_TRUE(tb_pomo_is_ready(&b->a.pomo, &b->a.set));
    TB_EQ_INT(b->a.pomo.done_today, 2);
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), -999);        /* ticking was off by default */

    b = on_call();
    expect_power_off_by_hold(b);
    b = ringing();
    expect_power_off_by_hold(b);
    b = dark();
    expect_power_off_by_hold(b);
    /* With a menu open, it closes. */
    b = on_status();
    hold(b);
    expect_power_off_by_hold(b);
}

TB_TEST(table_pwr_released_early)
{
    bench_t *b = on_status();
    pwr_hold(b, 1500);
    TB_FALSE(b->a.off);                     /* "released early, still on": not a press either */
    TB_EQ_INT(b->a.hold, TB_HOLD_NONE);
    TB_FALSE(b->a.powering_off);
    pwr_hold(b, 399);
    TB_TRUE(b->a.off);                      /* under 400 ms is a press */
}

TB_TEST(table_pwr_late_release_still_powers_off)
{
    /* The 3 s timer was due before the release reached core: it powers off, as the mock-up's timer would have. */
    bench_t *b = on_status();
    tb_app_button(&b->a, TB_BTN_PWR_DOWN, &b->now);
    bench_jump(b, 3200);
    tb_app_button(&b->a, TB_BTN_PWR_UP, &b->now);
    TB_TRUE(b->a.powering_off);
}

TB_TEST(table_powered_off_ignores_the_remote)
{
    bench_t *b = powered_off();
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_AVAILABLE, NULL, NULL, true, &b->now), TB_E_POWERED_OFF);
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_START, 5, true, &b->now), TB_E_POWERED_OFF);
    tb_app_set_call(&b->a, &(tb_call_t){.active = true, .id = 9}, NULL, &b->now);
    tb_app_tick(&b->a, &b->now);
    bench_drain(b);
    TB_EQ_INT(b->nlog, 0);
    TB_FALSE(b->a.call.active);
}
