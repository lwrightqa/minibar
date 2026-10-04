/*
 * Ticking during focus (decisions.md, decided 2026-10-04): off by default; Soft or Medium; one tick a second only
 * while a focus session is actually running. Silent while paused, during breaks, while an alarm rings, during a
 * call or calendar meeting (it picks up again afterwards), on a dark screen (proposed) and when the bar is off.
 */
#include "core_fixture.h"

static bench_t *ticking_on(tb_tick_vol_t vol)
{
    bench_t *b = bench_new();
    tb_settings_patch_t p = {0};
    p.has_ticking = p.has_tick_volume = true;
    p.v.pomodoro.ticking = true;
    p.v.pomodoro.tick_volume = vol;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    bench_drain(b);
    TB_EQ_STR(b->a.toast, vol == TB_TICK_MEDIUM ? "Ticking on \xC2\xB7 Medium" : "Ticking on \xC2\xB7 Soft");
    TB_EQ_INT(fx_count(b, TB_FX_TICKING), 0);          /* nothing runs yet */
    return b;
}

TB_TEST(ticking_off_by_default)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, 5000);
    TB_EQ_INT(fx_count(b, TB_FX_TICKING), 0);
    TB_FALSE(tb_app_ticking(&b->a, &b->now));
    TB_EQ_INT(b->a.ticking_level, 0);
}

TB_TEST(ticking_only_while_focus_runs)
{
    bench_t *b = ticking_on(TB_TICK_SOFT);
    pomo_start(b);
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 1);
    TB_TRUE(tb_app_ticking(&b->a, &b->now));
    /* paused */
    tap(b);
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 0);
    tap(b);
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 1);
    /* another status shown: the timer still runs, so does the tick */
    swipe(b, -80);
    TB_EQ_INT(b->a.ticking_level, 1);
    /* dark screen (proposed): silent; woken: back */
    pwr_press(b);
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 0);
    pwr_press(b);
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 1);
    /* the alarm rings: silent; the break: silent */
    bench_run(b, POMO_FOCUS_MS);
    TB_TRUE(b->a.ringing);
    TB_EQ_INT(b->a.ticking_level, 0);
    tap(b);
    TB_EQ_INT(b->a.pomo.phase, TB_PH_SHORT);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(b->a.ticking_level, 0);
    bench_run(b, POMO_SHORT_MS);
    tap(b);                                             /* focus 2 */
    TB_EQ_INT(b->a.ticking_level, 1);
}

TB_TEST(ticking_silent_during_calls_and_meetings_and_picks_up_after)
{
    bench_t *b = ticking_on(TB_TICK_MEDIUM);
    pomo_start(b);
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 2);
    call_start(b, "Slack");
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 0);
    flip(b);                                            /* resume during the call, set aside */
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(b->a.ticking_level, 0);                   /* still quiet: the call is on */
    call_end(b, NULL);
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 2);            /* picks up again */
    mt_t m = {0, 15, "Standup", "", false};
    cal_save(b, &m, 1);
    TB_EQ_INT(b->a.ticking_level, 0);                   /* the meeting paused the timer */
}

TB_TEST(ticking_silent_while_powering_off)
{
    bench_t *b = ticking_on(TB_TICK_SOFT);
    pomo_start(b);
    TB_EQ_INT(b->a.ticking_level, 1);
    tb_app_button(&b->a, TB_BTN_PWR_DOWN, &b->now);
    bench_run(b, 3050);
    TB_TRUE(b->a.powering_off);
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 0);
}

TB_TEST(ticking_menu_cycles_off_soft_medium)
{
    bench_t *b = bench_new();
    pomo_start(b);
    hold(b);
    tap_tile_named(b, TB_ACT_TIMER_SETTINGS);
    TB_EQ_STR(tile_with(b, TB_ACT_TICK)->value, "Off");
    TB_EQ_STR(tile_with(b, TB_ACT_TICK)->foot, "during focus");
    tap_tile_named(b, TB_ACT_TICK);
    TB_EQ_STR(b->a.toast, "Ticking on \xC2\xB7 Soft");
    TB_EQ_STR(tile_with(b, TB_ACT_TICK)->value, "Soft");
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 1);
    tap_tile_named(b, TB_ACT_TICK);
    TB_EQ_STR(b->a.toast, "Ticking on \xC2\xB7 Medium");
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 2);
    tap_tile_named(b, TB_ACT_TICK);
    TB_EQ_STR(b->a.toast, "Ticking off");
    TB_EQ_INT(fx_last(b, TB_FX_TICKING), 0);
    TB_EQ_INT(b->a.set.pomodoro.tick_volume, TB_TICK_MEDIUM);   /* the volume is kept while off */
    TB_EQ_INT(b->a.menu.kind, TB_MENU_TIMER_SETTINGS);          /* the menu stays open to step through */
    TB_TRUE(fx_count(b, TB_FX_SAVE_SETTINGS) >= 1);
}

TB_TEST(ticking_remote_volume_while_off)
{
    bench_t *b = bench_new();
    tb_settings_patch_t p = {0};
    p.has_tick_volume = true;
    p.v.pomodoro.tick_volume = TB_TICK_MEDIUM;
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Ticking volume \xC2\xB7 Medium (ticking is off)");
    TB_FALSE(b->a.set.pomodoro.ticking);
    /* the same volume again: says ticking is off */
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Ticking off");
    tb_settings_patch_t q = {0};
    q.has_ticking = true;
    q.v.pomodoro.ticking = true;
    tb_app_remote_settings(&b->a, &q, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Ticking on \xC2\xB7 Medium");
    /* volume while on */
    p.v.pomodoro.tick_volume = TB_TICK_SOFT;
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Ticking on \xC2\xB7 Soft");
}
