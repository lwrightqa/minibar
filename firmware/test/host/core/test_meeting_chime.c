/*
 * The meeting-start cue (decisions.md "Meeting-start sound (2026-10-07)"): a chime and one flash at the start of a
 * calendar meeting, the Meeting chime setting, and every quiet rule: Away, a call, another meeting, off, a dark screen,
 * a pairing code, a late meeting, a duplicate, the Pomodoro alarm (which yields), powered off and starting up.
 */
#include <string.h>

#include "core_fixture.h"

static int chimes(const bench_t *b) { return fx_count(b, TB_FX_MEETING_CHIME); }

/* A meeting by seconds from now (id given, so a test can send the same event twice). */
static tb_meeting_t mtg(const bench_t *b, uint32_t id, int start_s, int len_s)
{
    tb_meeting_t m;
    memset(&m, 0, sizeof m);
    m.id = id;
    m.start = b->now.wall + start_s;
    m.end = m.start + len_s;
    snprintf(m.title, sizeof m.title, "Design review");
    return m;
}

static void put(bench_t *b, const tb_meeting_t *m, int n)
{
    tb_app_set_meetings(&b->a, m, n, &b->now);
    bench_drain(b);
}

/* A bar whose calendar is saved (Calendar meetings on) with no meetings yet. */
static bench_t *bar_with_calendar(void)
{
    bench_t *b = bench_new();
    cal_save(b, NULL, 0);
    bench_run(b, TB_TOAST_MS + 100);
    bench_clear_log(b);
    return b;
}

TB_TEST(chime_default_on_and_fires_once_at_the_start)
{
    bench_t *b = bar_with_calendar();
    TB_TRUE(b->a.set.more.meeting_chime);
    tb_meeting_t m = mtg(b, 7, 120, 1800);
    put(b, &m, 1);
    bench_run(b, 119000);
    TB_EQ_INT(chimes(b), 0);
    TB_EQ_INT(b->a.flash_at, 0);
    bench_run(b, 2000);
    TB_EQ_INT(chimes(b), 1);                             /* at the start, not before */
    TB_EQ_INT(fx_last(b, TB_FX_MEETING_CHIME), 0);       /* 0: a meeting, not the tile's sample */
    TB_TRUE(b->a.flash_at != 0);
    TB_TRUE(b->a.flash_once);                            /* one pulse, not the alarm's three */
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_MEETING);
    bench_run(b, 120000);                                /* and never again for the same meeting */
    TB_EQ_INT(chimes(b), 1);
}

TB_TEST(chime_busy_chimes_away_only_flashes)
{
    bench_t *b = bar_with_calendar();
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    tb_meeting_t m = mtg(b, 7, 60, 1800);
    put(b, &m, 1);
    bench_run(b, 62000);
    TB_EQ_INT(chimes(b), 1);                             /* Busy is a cue for you: it chimes */
    b = bar_with_calendar();
    go_status(b, TB_ST_AWAY);
    m = mtg(b, 7, 60, 1800);
    put(b, &m, 1);
    bench_run(b, 62000);
    TB_EQ_INT(chimes(b), 0);                             /* nobody is at the desk: no chime... */
    TB_TRUE(b->a.flash_at != 0);                         /* ...but the flash */
    TB_TRUE(b->a.flash_once);
    for (tb_status_t st = TB_ST_AVAILABLE; st <= TB_ST_CLOCK; st++) {
        if (st == TB_ST_AWAY || st == TB_ST_POMODORO || st == TB_ST_MESSAGE) continue;   /* (Message needs a message first) */
        b = bar_with_calendar();
        go_status(b, st);
        m = mtg(b, 7, 60, 1800);
        put(b, &m, 1);
        bench_run(b, 62000);
        TB_EQ_INT(chimes(b), 1);                         /* Available, Meeting by hand and Clock chime */
    }
    b = bar_with_calendar();
    go_status(b, TB_ST_POMODORO);
    m = mtg(b, 7, 60, 1800);
    put(b, &m, 1);
    bench_run(b, 62000);
    TB_EQ_INT(chimes(b), 1);                             /* so does a Pomodoro that isn't ringing */
}

TB_TEST(chime_call_or_another_meeting_in_progress_flash_only)
{
    bench_t *b = bar_with_calendar();
    call_start(b, "Slack");
    tb_meeting_t m = mtg(b, 7, 60, 1800);
    put(b, &m, 1);
    bench_run(b, 62000);
    TB_EQ_INT(chimes(b), 0);                             /* a meeting that starts during a call flashes only */
    TB_TRUE(b->a.flash_at != 0);

    /* overlapping meetings: the later one flashes only, even when the earlier is set aside (the mic may be live) */
    b = bar_with_calendar();
    tb_meeting_t two[2] = {mtg(b, 1, 60, 1800), mtg(b, 2, 600, 1800)};
    put(b, two, 2);
    bench_run(b, 62000);
    TB_EQ_INT(chimes(b), 1);                             /* the first chimes */
    tap(b);                                              /* set it aside */
    bench_clear_log(b);
    bench_run(b, 560000);
    TB_EQ_INT(chimes(b), 0);
    TB_TRUE(b->a.flash_at != 0);

    /* back to back: the earlier one has ended at that moment, so each gets its own chime */
    b = bar_with_calendar();
    tb_meeting_t seq[2] = {mtg(b, 1, 60, 600), mtg(b, 2, 660, 600)};
    put(b, seq, 2);
    bench_run(b, 62000);
    TB_EQ_INT(chimes(b), 1);
    bench_run(b, 600000);
    TB_EQ_INT(chimes(b), 2);
}

TB_TEST(chime_off_turns_off_the_whole_cue)
{
    bench_t *b = bar_with_calendar();
    b->a.set.more.meeting_chime = false;
    tb_meeting_t m = mtg(b, 7, 60, 1800);
    put(b, &m, 1);
    bench_run(b, 62000);
    TB_EQ_INT(chimes(b), 0);
    TB_EQ_INT(b->a.flash_at, 0);                         /* no flash either */
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_MEETING);   /* the meeting itself still shows */
}

TB_TEST(chime_late_meetings_are_silent)
{
    /* learned after it began */
    bench_t *b = bar_with_calendar();
    tb_meeting_t m = mtg(b, 7, -300, 1800);
    put(b, &m, 1);
    bench_run(b, 30000);
    TB_EQ_INT(chimes(b), 0);
    TB_EQ_INT(b->a.flash_at, 0);

    /* a calendar back from a failed sync: the meeting left the list, and returns after its start */
    b = bar_with_calendar();
    m = mtg(b, 7, 90, 1800);
    put(b, &m, 1);
    bench_run(b, 30000);
    put(b, NULL, 0);                                     /* the calendar failed: its meetings are left out */
    bench_run(b, 70000);                                 /* the start passes while it's away */
    m.start = b->now.wall - 10;
    m.end = m.start + 1800;
    put(b, &m, 1);
    bench_run(b, 30000);
    TB_EQ_INT(chimes(b), 0);
    TB_EQ_INT(b->a.flash_at, 0);

    /* Calendar meetings switched off, and on again after the start: late. Switched back on before it, it still counts. */
    b = bar_with_calendar();
    m = mtg(b, 7, 60, 1800);
    put(b, &m, 1);
    tb_settings_patch_t p = {0};
    p.has_calendar = true;
    p.v.automatic.calendar = false;
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    bench_run(b, 90000);                                 /* it began while the calendar was off */
    p.v.automatic.calendar = true;
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    bench_run(b, 30000);
    TB_EQ_INT(chimes(b), 0);
    TB_EQ_INT(b->a.flash_at, 0);

    /* more than 20 s after the start the moment has gone (a clock that jumped, a stalled loop) */
    b = bar_with_calendar();
    m = mtg(b, 7, 60, 1800);
    put(b, &m, 1);
    bench_run(b, 30000);
    bench_jump(b, 40000);                                /* 70 s on: 10 s after the start, inside the window */
    bench_run(b, 50);
    TB_EQ_INT(chimes(b), 1);
    b = bar_with_calendar();
    m = mtg(b, 7, 60, 1800);
    put(b, &m, 1);
    bench_run(b, 30000);
    bench_jump(b, 75000);                                /* 105 s on: 45 s after the start */
    bench_run(b, 50);
    TB_EQ_INT(chimes(b), 0);
}

TB_TEST(chime_same_event_twice_sounds_once)
{
    bench_t *b = bar_with_calendar();
    tb_meeting_t two[2] = {mtg(b, 9, 60, 1800), mtg(b, 9, 60, 1800)};   /* one id: the same UID and start */
    put(b, two, 2);
    bench_run(b, 62000);
    TB_EQ_INT(chimes(b), 1);
}

TB_TEST(chime_dark_screen_wakes_for_the_flash_and_chimes)
{
    bench_t *b = bar_with_calendar();
    pwr_press(b);
    TB_TRUE(b->a.off);
    tb_meeting_t m = mtg(b, 7, 60, 1800);
    put(b, &m, 1);
    bench_run(b, 62000);
    TB_FALSE(b->a.off);                                  /* woken, and it stays on */
    TB_EQ_INT(chimes(b), 1);
    TB_TRUE(b->a.flash_at != 0);
}

TB_TEST(chime_pairing_code_on_screen_gets_nothing)
{
    bench_t *b = bar_with_calendar();
    tb_meeting_t m = mtg(b, 7, 60, 1800);
    put(b, &m, 1);
    tb_app_pairing_show(&b->a, "482913", "Mac", TB_PAIR_KIND_MAC, &b->now);
    bench_run(b, 62000);
    TB_EQ_INT(chimes(b), 0);
    TB_EQ_INT(b->a.flash_at, 0);
}

TB_TEST(chime_starting_up_and_powered_off_get_nothing)
{
    bench_t *b = bench_new_opts(true, false);            /* in the splash */
    TB_TRUE(b->a.booting);
    cal_save(b, NULL, 0);
    tb_meeting_t m = mtg(b, 7, 1, 1800);
    put(b, &m, 1);
    bench_run(b, 1200);                                  /* the meeting starts under the splash */
    TB_EQ_INT(chimes(b), 0);
    bench_run(b, 3000);
    TB_EQ_INT(chimes(b), 0);                             /* and isn't cued after it either: it began before the bar was up */
}

TB_TEST(chime_ringing_pomodoro_alarm_yields_to_the_meeting)
{
    bench_t *b = bar_with_calendar();
    pomo_start(b);
    TB_TRUE(b->a.pomo.running);
    tb_meeting_t m = mtg(b, 7, 25 * 60 + 20, 1800);      /* 20 s after the focus session ends */
    put(b, &m, 1);
    bench_run(b, 25 * 60000 + 1000);
    TB_TRUE(b->a.ringing);
    bench_clear_log(b);
    bench_run(b, 19000 + 2000);
    TB_EQ_INT(chimes(b), 1);                             /* the meeting chime plays... */
    TB_FALSE(b->a.ringing);                              /* ...and the meeting takes the alarm over, as it always has */
    TB_TRUE(b->a.pomo.waiting);
    TB_EQ_INT(b->a.pomo.held_alarm, TB_AUTO_MEETING);    /* it rings once after the meeting */
    int alarm = fx_count(b, TB_FX_CHIME);
    bench_run(b, 8000);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), alarm);          /* no alarm chime stacked on top */
}

TB_TEST(chime_tile_toggles_with_a_toast_and_a_sample)
{
    bench_t *b = bench_new();
    hold(b);
    tap_tile_named(b, TB_ACT_DISPLAY);
    const tb_tile_t *t = tile_with(b, TB_ACT_MEET_CHIME);
    TB_EQ_STR(t->label, "Chime");
    TB_EQ_STR(t->value, "On");
    TB_EQ_STR(t->foot, "at meeting start");
    TB_EQ_INT(tile_index(b, TB_ACT_MEET_CHIME), 1);       /* Light, Chime, Time, Back */
    bench_clear_log(b);
    tap_tile_named(b, TB_ACT_MEET_CHIME);                /* off: no sound */
    TB_FALSE(b->a.set.more.meeting_chime);
    TB_EQ_STR(b->a.toast, "Meeting chime off");
    TB_EQ_STR(tile_with(b, TB_ACT_MEET_CHIME)->value, "Off");
    TB_EQ_INT(b->a.menu.kind, TB_MENU_DISPLAY);          /* the menu stays open */
    TB_EQ_INT(chimes(b), 0);
    TB_TRUE(fx_count(b, TB_FX_SAVE_SETTINGS) >= 1);
    tap_tile_named(b, TB_ACT_MEET_CHIME);                /* on: the chime once, as a sample */
    TB_TRUE(b->a.set.more.meeting_chime);
    TB_EQ_STR(b->a.toast, "Meeting chime on");
    TB_EQ_INT(chimes(b), 1);
    TB_EQ_INT(fx_last(b, TB_FX_MEETING_CHIME), 1);
    /* during a call the sample stays quiet */
    call_start(b, "Slack");
    hold(b);
    tap_tile_named(b, TB_ACT_DISPLAY);
    bench_clear_log(b);
    tap_tile_named(b, TB_ACT_MEET_CHIME);
    tap_tile_named(b, TB_ACT_MEET_CHIME);
    TB_EQ_INT(chimes(b), 0);
}

TB_TEST(chime_from_the_remote_toasts_and_never_sounds)
{
    bench_t *b = bench_new();
    tb_settings_patch_t p = {0};
    p.has_meeting_chime = true;
    p.v.more.meeting_chime = false;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    TB_FALSE(b->a.set.more.meeting_chime);
    TB_EQ_STR(b->a.toast, "Meeting chime off");
    p.v.more.meeting_chime = true;
    bench_clear_log(b);
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    TB_EQ_STR(b->a.toast, "Meeting chime on");
    TB_EQ_INT(chimes(b), 0);                             /* only a tap on the bar plays the sample */
    bench_run(b, TB_TOAST_MS + 100);
    TB_EQ_STR(b->a.toast, "");
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    TB_EQ_STR(b->a.toast, "");                           /* the value it already has: no toast */
}

TB_TEST(theme_from_the_remote_changes_the_setting_and_toasts)
{
    bench_t *b = bench_new();
    TB_EQ_INT(b->a.set.theme, TB_THEME_BOLD);
    tb_settings_patch_t p = {0};
    p.has_theme = true;
    p.v.theme = TB_THEME_PIXEL;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    TB_EQ_INT(b->a.set.theme, TB_THEME_PIXEL);
    TB_EQ_STR(b->a.toast, "Theme Â· Low Glare Pixel");
    p.v.theme = TB_THEME_BOLD;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    TB_EQ_INT(b->a.set.theme, TB_THEME_BOLD);
}
