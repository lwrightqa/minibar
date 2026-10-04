/* Pomodoro scenarios on the whole bar: the cycle, the alarm, auto-start, skip, stop, +5, restart, power-off. */
#include <string.h>

#include "core_fixture.h"

/* One phase runs out; the alarm rings; a tap starts the next one. */
static void finish_phase_and_tap(bench_t *b, tb_ms_t len, tb_phase_t next, const char *toast)
{
    bench_run(b, len + 100);
    TB_TRUE(b->a.ringing);
    TB_TRUE(b->a.pomo.waiting);
    TB_EQ_INT(b->a.pomo.phase, next);
    tap(b);
    TB_FALSE(b->a.ringing);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_STR(b->a.toast, toast);
}

TB_TEST(scenario_full_pomodoro_cycle)
{
    bench_t *b = bench_new();
    pomo_start(b);
    TB_EQ_STR(b->a.toast, "Focus started");
    for (int r = 1; r <= 4; r++) {
        TB_EQ_INT(b->a.pomo.round, r);
        bool last = r == 4;
        finish_phase_and_tap(b, POMO_FOCUS_MS, last ? TB_PH_LONG : TB_PH_SHORT, last ? "Long break started" : "Short break started");
        TB_EQ_INT(b->a.pomo.done_today, r);
        TB_EQ_INT(b->a.pomo.just_ended, -1);
        finish_phase_and_tap(b, last ? POMO_LONG_MS : POMO_SHORT_MS, TB_PH_FOCUS, "Focus started");
    }
    TB_EQ_INT(b->a.pomo.round, 1);
    TB_EQ_INT(b->a.pomo.done_today, 4);
    /* focused time: four whole sessions (and a tap's worth of the fifth) */
    TB_TRUE(b->a.pomo.focused_ms >= 4 * (tb_ms_t)POMO_FOCUS_MS);
    TB_TRUE(b->a.pomo.focused_ms < 4 * (tb_ms_t)POMO_FOCUS_MS + 1000);
}

TB_TEST(scenario_alarm_chimes_every_4s_for_a_minute)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, POMO_FOCUS_MS - 1000);
    bench_clear_log(b);
    bench_run(b, 1050);                         /* the focus ends */
    TB_TRUE(b->a.ringing);
    TB_EQ_INT(b->a.pomo.just_ended, TB_PH_FOCUS);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 1);
    TB_EQ_INT(fx_last(b, TB_FX_CHIME), 1);      /* to a break: 784, 988, 1175 Hz */
    TB_TRUE(b->a.flash_at != 0);
    tb_ms_t first = b->a.flash_at;
    bench_run(b, 3900);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 1);
    bench_run(b, 200);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 2);     /* again 4 s later, with a flash */
    TB_TRUE(b->a.flash_at > first);
    bench_run(b, 60000);
    TB_FALSE(b->a.ringing);                     /* gave up after a minute */
    TB_TRUE(b->a.pomo.waiting);                 /* still waiting for a start */
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 16);    /* at 0, 4, 8 ... 60 s */
    bench_run(b, 10000);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 16);
    /* break over: the chime goes the other way */
    tap(b);
    bench_clear_log(b);
    bench_run(b, POMO_SHORT_MS + 100);
    TB_EQ_INT(b->a.pomo.just_ended, TB_PH_SHORT);
    TB_EQ_INT(fx_last(b, TB_FX_CHIME), 0);      /* back to focus: 1175, 988, 784 Hz */
}

TB_TEST(scenario_phase_end_jumps_back_wakes_and_closes_menu)
{
    bench_t *b = bench_new();
    pomo_start(b);
    swipe(b, -80);                              /* Away, the timer keeps running (the corner pill) */
    TB_EQ_INT(b->a.idx, TB_ST_AWAY);
    TB_TRUE(tb_pomo_active(&b->a.pomo, &b->a.set));
    bench_run(b, POMO_FOCUS_MS - 2000);
    pwr_press(b);                               /* dark */
    TB_TRUE(b->a.off);
    bench_run(b, 1000);
    TB_TRUE(b->a.off);
    bench_run(b, 1100);
    TB_FALSE(b->a.off);                         /* endPhase wakes it */
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);        /* and shows the Pomodoro */
    TB_TRUE(b->a.ringing);
    /* with a menu open, it closes */
    tap(b);
    swipe(b, -80);
    bench_run(b, POMO_SHORT_MS - 3000);
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);
    bench_run(b, 2000);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);   /* still open: under 8 s */
    bench_run(b, 1500);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);    /* the break ended: endPhase closed it */
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_TRUE(b->a.ringing);
}

TB_TEST(scenario_auto_start_chimes_once_and_carries_on)
{
    bench_t *b = bench_new();
    pomo_start(b);
    hold(b);
    tap_tile_named(b, TB_ACT_TIMER_SETTINGS);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_TIMER_SETTINGS);
    tap_tile_named(b, TB_ACT_AUTO);
    TB_TRUE(b->a.set.pomodoro.auto_start);
    TB_EQ_STR(b->a.toast, "Auto-start on");
    TB_EQ_STR(tile_with(b, TB_ACT_AUTO)->value, "On");
    TB_EQ_INT(fx_count(b, TB_FX_SAVE_SETTINGS), 1);
    tap_tile_named(b, TB_ACT_TIMER_MENU);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_TIMER);
    tap_tile_named(b, TB_ACT_CLOSE);
    bench_clear_log(b);
    bench_run(b, POMO_FOCUS_MS);
    TB_FALSE(b->a.ringing);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(b->a.pomo.phase, TB_PH_SHORT);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 1);
    TB_TRUE(b->a.flash_at != 0);
    bench_run(b, 20000);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 1);
}

TB_TEST(scenario_chime_off_flashes_without_sound)
{
    bench_t *b = bench_new();
    tb_settings_patch_t p = {0};
    p.has_chime = true;
    p.v.pomodoro.chime = false;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    TB_EQ_STR(b->a.toast, "Chime off");
    pomo_start(b);
    bench_clear_log(b);
    bench_run(b, POMO_FOCUS_MS + 9000);
    TB_TRUE(b->a.ringing);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 0);
}

TB_TEST(scenario_skip_stop_and_add_time)
{
    bench_t *b = bench_new();
    go_status(b, TB_ST_AVAILABLE);
    pomo_start(b);
    bench_run(b, 60000);
    hold(b);
    TB_EQ_STR(tile_with(b, TB_ACT_SKIP)->foot, "to short break");
    TB_EQ_STR(tile_with(b, TB_ACT_ADD)->foot, "min to this session");
    tb_ms_t left = b->a.pomo.remaining_ms;
    tap_tile_named(b, TB_ACT_ADD);
    TB_EQ_STR(b->a.toast, "+5 min");
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    TB_TRUE(b->a.pomo.remaining_ms > left + 5 * 60000 - 200);
    /* Skip: the focus ends without a tomato, and the break starts running. */
    hold(b);
    tap_tile_named(b, TB_ACT_SKIP);
    TB_EQ_INT(b->a.pomo.phase, TB_PH_SHORT);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(b->a.pomo.done_today, 0);
    TB_EQ_INT(b->a.pomo.skipped_mask, 1);
    hold(b);
    TB_EQ_STR(tile_with(b, TB_ACT_ADD)->foot, "min to this break");
    TB_EQ_STR(tile_with(b, TB_ACT_SKIP)->foot, "to focus");
    /* Stop: back to the status before, the run over, tomatoes kept. */
    b->a.pomo.done_today = 3;
    tap_tile_named(b, TB_ACT_STOP);
    TB_EQ_INT(b->a.idx, TB_ST_AVAILABLE);
    TB_EQ_STR(b->a.toast, "Pomodoro stopped");
    TB_TRUE(tb_pomo_is_ready(&b->a.pomo, &b->a.set));
    TB_EQ_INT(b->a.pomo.done_today, 3);
    TB_FALSE(tb_pomo_active(&b->a.pomo, &b->a.set));
}

TB_TEST(scenario_timer_menu_on_the_fourth_focus_says_long_break)
{
    bench_t *b = bench_new();
    pomo_start(b);
    b->a.pomo.round = 4;
    hold(b);
    TB_EQ_STR(tile_with(b, TB_ACT_SKIP)->foot, "to long break");
}

TB_TEST(scenario_ready_timer_menu_has_no_plus_five)
{
    bench_t *b = bench_new();
    go_status(b, TB_ST_POMODORO);
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_TIMER);
    TB_TRUE(tile_with(b, TB_ACT_ADD) == NULL);
    TB_EQ_INT(b->a.menu.n, 4);                  /* Skip, Stop, Settings, Done */
    /* the tile list: Skip "Next", Stop "End", Settings "More", Done */
    TB_EQ_STR(b->a.menu.tiles[0].label, "Skip");
    TB_EQ_STR(b->a.menu.tiles[0].value, "Next");
    TB_EQ_STR(b->a.menu.tiles[1].value, "End");
    TB_EQ_STR(b->a.menu.tiles[1].foot, "keeps tomatoes");
    TB_EQ_STR(b->a.menu.tiles[2].value, "More");
    TB_EQ_STR(b->a.menu.tiles[2].foot, "auto-start, ticking");
    TB_EQ_INT(b->a.menu.tiles[3].style, TB_TILE_DONE);
}

TB_TEST(scenario_restart_resets_the_run_and_comes_back_ready)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, POMO_FOCUS_MS + 100);
    tap(b);                                     /* short break running */
    TB_EQ_INT(b->a.pomo.done_today, 1);
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_TIMER);
    hold(b);                                    /* holding on a menu reopens the base menu */
    TB_EQ_INT(b->a.menu.kind, TB_MENU_TIMER);
    tap_tile_named(b, TB_ACT_CLOSE);
    swipe(b, -80);                              /* to Away: the quick menu's Power tile */
    hold(b);
    tap_tile_named(b, TB_ACT_POWER);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_POWER);
    tap_tile_named(b, TB_ACT_RESTART);
    TB_EQ_INT(fx_count(b, TB_FX_RESTART), 1);
    TB_TRUE(b->a.booting);
    TB_TRUE(tb_pomo_is_ready(&b->a.pomo, &b->a.set));
    TB_EQ_INT(b->a.pomo.done_today, 1);
    TB_EQ_INT(tb_app_color_key(&b->a, &b->now), TB_KEY_CLOCK);   /* the splash */
    bench_run(b, TB_BOOT_SPLASH_MS + 60);
    TB_FALSE(b->a.booting);
    TB_EQ_STR(b->a.toast, "Ready");
    TB_EQ_INT(b->a.idx, TB_ST_AWAY);
}

TB_TEST(scenario_boot_falls_back_from_pomodoro_to_last_status)
{
    bench_t *b = bench_new_opts(true, false);
    tb_app_restore(&b->a, TB_ST_POMODORO, TB_ST_BUSY, "Back at 3", 1791130000, 2, 3000000, tb_local_yyyymmdd(T0_WALL));
    TB_TRUE(b->a.booting);
    /* during the splash nothing moves: no timer, no inputs */
    tap(b);
    boot_btn(b);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_AWAY, NULL, NULL, true, &b->now), TB_E_POWERED_OFF);
    bench_run(b, TB_BOOT_SPLASH_MS);
    TB_FALSE(b->a.booting);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    TB_EQ_STR(b->a.message, "Back at 3");
    TB_EQ_INT(b->a.pomo.done_today, 2);
    TB_EQ_STR(b->a.toast, "Ready");
    TB_EQ_INT(fx_count(b, TB_FX_SAVE_STATE), 1);        /* the status changed: saved once */
}

TB_TEST(scenario_first_boot_is_the_clock)
{
    bench_t *b = bench_new_opts(true, false);
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    tb_app_init(&b->a, &s, true, &b->now);
    TB_EQ_INT(b->a.idx, TB_ST_CLOCK);
    TB_EQ_INT(b->a.last_status, TB_ST_AVAILABLE);
    bench_run(b, TB_BOOT_SPLASH_MS + 60);
    TB_EQ_INT(b->a.idx, TB_ST_CLOCK);
    TB_EQ_INT(tb_app_color_key(&b->a, &b->now), TB_KEY_CLOCK);
}

TB_TEST(scenario_midnight_resets_the_tallies_not_the_run)
{
    bench_t *b = bench_new();
    pomo_start(b);
    b->a.pomo.done_today = 5;
    /* jump to 23:59:30 the same day and run across midnight */
    b->now.mono += (1791183570 - T0_WALL) * 1000;
    b->now.wall = 1791183570;
    b->a.last_mono = b->now.mono;               /* as if the loop had kept running (no 14-hour dt) */
    b->a.pomo.remaining_ms = 20 * 60000;
    bench_run(b, 20000);
    TB_EQ_INT(b->a.pomo.done_today, 5);
    bench_run(b, 20000);
    TB_EQ_INT(b->a.pomo.done_today, 0);
    TB_EQ_INT(b->a.pomo.day, 20261005);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(b->a.pomo.round, 1);
}

TB_TEST(scenario_pause_resume_and_waiting_copy)
{
    bench_t *b = bench_new();
    pomo_start(b);
    TB_EQ_INT(tb_pomo_state(&b->a.pomo, &b->a.set), TB_POMO_ST_RUNNING);
    tap(b);
    TB_EQ_INT(tb_pomo_state(&b->a.pomo, &b->a.set), TB_POMO_ST_PAUSED);
    TB_EQ_INT(tb_app_paused_by(&b->a, &b->now), TB_AUTO_NONE);      /* paused by you */
    tb_ms_t left = b->a.pomo.remaining_ms;
    bench_run(b, 30000);
    TB_EQ_INT(b->a.pomo.remaining_ms, left);
    tap(b);
    TB_EQ_STR(b->a.toast, "Focus resumed");
}
