/* Toasts, pending toasts, rev, saving, backlight, flips at boot, press lost, the effect queue, the clock. */
#include <string.h>

#include "core_fixture.h"

/* core-private (src/tb_internal.h): queue an effect */
void tb_fx(tb_app_t *a, tb_effect_kind_t kind, int32_t arg);

TB_TEST(toast_lasts_1600ms)
{
    bench_t *b = bench_new();
    tb_app_notify(&b->a, "Removed Mac", &b->now);
    TB_EQ_STR(b->a.toast, "Removed Mac");
    bench_run(b, 1550);
    TB_EQ_STR(b->a.toast, "Removed Mac");
    bench_run(b, 100);
    TB_EQ_STR(b->a.toast, "");
}

TB_TEST(toast_pending_rules)
{
    /* notify() waits for a free screen and replaces what's waiting */
    bench_t *b = bench_new();
    hold(b);
    tb_app_notify(&b->a, "First", &b->now);
    tb_app_notify(&b->a, "Second", &b->now);
    TB_EQ_STR(b->a.toast, "");
    TB_EQ_STR(b->a.pending_toast, "Second");
    tap_tile_named(b, TB_ACT_CLOSE);
    bench_run(b, 100);
    TB_EQ_STR(b->a.toast, "Second");
    /* a pending toast waits for the toast on screen to go */
    hold(b);
    tap_tile_named(b, TB_ACT_BRIGHT);                    /* (no toast) */
    tb_app_notify(&b->a, "Third", &b->now);
    tap_tile_named(b, TB_ACT_CLOSE);
    tb_app_notify(&b->a, "Now", &b->now);                /* free: shows at once */
    TB_EQ_STR(b->a.toast, "Now");
    TB_EQ_STR(b->a.pending_toast, "Third");
    bench_run(b, TB_TOAST_MS + 100);
    TB_EQ_STR(b->a.toast, "Third");
    /* toast() on a dark screen keeps the first one waiting */
    b = bench_new();
    pwr_press(b);
    tb_app_calendar_event(&b->a, TB_CALEV_SYNCED, &b->now);        /* toast() */
    tb_app_calendar_event(&b->a, TB_CALEV_SYNC_FAILED, &b->now);   /* toast() again */
    TB_EQ_STR(b->a.pending_toast, "Calendar synced");
    /* ...but an automatic change replaces it (notify) */
    call_start(b, NULL);
    TB_EQ_STR(b->a.pending_toast, "Mac: call started");
}

TB_TEST(toast_powered_off_drops_everything)
{
    bench_t *b = bench_new();
    pwr_press(b);
    tb_app_notify(&b->a, "x", &b->now);
    pwr_press(b);
    pwr_hold(b, 3100);
    bench_run(b, TB_POWERING_OFF_MS + 50);
    TB_EQ_STR(b->a.pending_toast, "");
    tb_app_notify(&b->a, "y", &b->now);
    TB_EQ_STR(b->a.toast, "");
}

TB_TEST(rev_moves_on_changes_but_not_on_countdowns)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, TB_TOAST_MS + 100);                     /* "Focus started" is gone */
    uint32_t r = b->a.rev;
    bench_run(b, 30000);                                 /* only the timer counts down */
    TB_EQ_INT(b->a.rev, r);
    tap(b);                                              /* paused: visible */
    TB_TRUE(b->a.rev != r);
    r = b->a.rev;
    tb_app_set_mac_link(&b->a, TB_LINK_WIFI, &b->now);
    TB_TRUE(b->a.rev != r);
    r = b->a.rev;
    tb_app_set_mac_link(&b->a, TB_LINK_WIFI, &b->now);   /* no change */
    TB_EQ_INT(b->a.rev, r);
    tb_app_set_meetings(&b->a, NULL, 0, &b->now);
    TB_TRUE(b->a.rev != r);
}

TB_TEST(saves_follow_changes)
{
    bench_t *b = bench_new();
    pomo_start(b);
    int st = fx_count(b, TB_FX_SAVE_STATE);
    TB_TRUE(st >= 1);
    bench_run(b, 120000);
    TB_EQ_INT(fx_count(b, TB_FX_SAVE_STATE), st);       /* no writes while the timer just runs */
    TB_EQ_INT(fx_count(b, TB_FX_SAVE_SETTINGS), 0);
    bench_run(b, POMO_FOCUS_MS);
    TB_EQ_INT(fx_count(b, TB_FX_SAVE_STATE), st + 1);   /* a tomato: saved */
    tb_app_set_time_zone(&b->a, "America/New_York", &b->now);
    bench_drain(b);
    TB_EQ_INT(fx_count(b, TB_FX_SAVE_SETTINGS), 1);
}

TB_TEST(flip_at_boot_only_turns_the_layout)
{
    bench_t *b = bench_new_opts(true, false);
    tb_app_flip(&b->a, true, true, &b->now);             /* initial */
    bench_drain(b);
    TB_TRUE(b->a.flipped);
    TB_EQ_INT(fx_last(b, TB_FX_ROTATE), 1);
    TB_FALSE(b->a.pomo.running);
    TB_EQ_STR(b->a.toast, "");
    tb_app_flip(&b->a, false, false, &b->now);           /* during the splash: layout only */
    bench_drain(b);
    TB_FALSE(b->a.flipped);
    TB_FALSE(b->a.pomo.running);
    bench_run(b, TB_BOOT_SPLASH_MS + 60);
    bench_clear_log(b);
    tb_app_flip(&b->a, false, false, &b->now);           /* the same orientation: nothing */
    bench_drain(b);
    TB_EQ_INT(b->nlog, 0);
    TB_FALSE(b->a.pomo.running);
}

TB_TEST(press_lost_does_nothing)
{
    bench_t *b = bench_new();
    tb_app_pointer(&b->a, true, 200, 80, TB_TILE_NONE, &b->now);
    bench_run(b, 100);
    tb_app_pointer(&b->a, false, 200, 80, TB_TILE_LOST, &b->now);
    bench_run(b, 1000);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    /* a press lost while held long enough for a hold, before the hold fired, opens nothing either */
    tb_app_pointer(&b->a, true, 200, 80, TB_TILE_NONE, &b->now);
    bench_jump(b, 600);
    tb_app_pointer(&b->a, false, 200, 80, TB_TILE_LOST, &b->now);
    bench_run(b, 100);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
}

TB_TEST(late_hold_sample_opens_the_menu_not_a_tap)
{
    bench_t *b = bench_new();
    tb_app_pointer(&b->a, true, 200, 80, TB_TILE_NONE, &b->now);
    bench_jump(b, 700);                                  /* the loop didn't poll in time */
    tb_app_pointer(&b->a, false, 200, 80, TB_TILE_NONE, &b->now);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
}

TB_TEST(effects_queue_overflow_is_counted)
{
    bench_t *b = bench_new();
    for (int i = 0; i < TB_EFFECTS_MAX + 4; i++) tb_fx(&b->a, TB_FX_CHIME, 1);
    TB_EQ_INT(b->a.n_fx, TB_EFFECTS_MAX);
    TB_EQ_INT(b->a.fx_dropped, 4);
    tb_effect_t out[4];
    TB_EQ_INT(tb_app_take_effects(&b->a, out, 4), 4);
    TB_EQ_INT(b->a.n_fx, TB_EFFECTS_MAX - 4);
}

TB_TEST(clock_unknown_then_known)
{
    bench_t *b = bench_new();
    b->now.valid = false;
    go_status(b, TB_ST_AVAILABLE);
    TB_EQ_INT(b->a.since, 0);
    bench_run(b, 90000);
    b->now.valid = true;
    bench_run(b, 50);
    TB_TRUE(b->a.since >= b->now.wall - 91 && b->a.since <= b->now.wall - 89);   /* filled in from the monotonic clock */
    b->now.valid = false;
    TB_EQ_INT(tb_app_remote_message(&b->a, "hi", true, &b->now), TB_OK);
    TB_EQ_INT(b->a.message_at, 0);
}

TB_TEST(status_names_and_color_keys)
{
    bench_t *b = bench_new();
    TB_EQ_STR(tb_status_name(TB_ST_MEETING), "In a meeting");
    TB_EQ_STR(tb_status_name(TB_ST_COUNT), "");
    const tb_color_key_t keys[TB_ST_COUNT] = {TB_KEY_AVAILABLE, TB_KEY_BUSY, TB_KEY_MEETING, TB_KEY_FOCUS,
                                              TB_KEY_AWAY, TB_KEY_MESSAGE, TB_KEY_CLOCK};
    for (int i = 0; i < TB_ST_COUNT; i++) {
        b->a.idx = (tb_status_t)i;
        TB_EQ_INT(tb_app_color_key(&b->a, &b->now), keys[i]);
    }
    b->a.idx = TB_ST_POMODORO;
    b->a.pomo.phase = TB_PH_SHORT;
    TB_EQ_INT(tb_app_color_key(&b->a, &b->now), TB_KEY_SHORT);
    b->a.pomo.phase = TB_PH_LONG;
    TB_EQ_INT(tb_app_color_key(&b->a, &b->now), TB_KEY_LONG);
}

TB_TEST(moved_tap_is_a_tap_on_a_status_screen_but_not_on_a_menu)
{
    bench_t *b = bench_new();
    swipe(b, -25);                                       /* 25 px: more than the slop, less than a swipe */
    TB_EQ_INT(b->a.idx, TB_ST_MEETING);                  /* a tap: next status */
    hold(b);
    int i = tile_index(b, TB_ACT_POWER);
    tb_app_pointer(&b->a, true, 300, 80, (int8_t)i, &b->now);
    bench_run(b, 40);
    tb_app_pointer(&b->a, false, 320, 80, (int8_t)i, &b->now);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);           /* ignored: only a still tap acts on a menu */
}
