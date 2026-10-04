/* The Remote and the API (api.md sections 8 to 10): status, message, aside, Pomodoro actions, settings PATCH. */
#include <string.h>

#include "core_fixture.h"

TB_TEST(remote_status_basics)
{
    bench_t *b = bench_new();
    bench_run(b, 60000);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_AVAILABLE, NULL, NULL, true, &b->now), TB_OK);
    TB_EQ_INT(b->a.idx, TB_ST_AVAILABLE);
    TB_EQ_INT(b->a.last_status, TB_ST_AVAILABLE);
    TB_EQ_INT(b->a.since, b->now.wall);
    bench_drain(b);
    TB_EQ_INT(fx_count(b, TB_FX_SAVE_STATE), 1);
    /* Pomodoro and Clock aren't remembered as the status Stop returns to */
    tb_app_remote_status(&b->a, TB_ST_CLOCK, NULL, NULL, true, &b->now);
    tb_app_remote_status(&b->a, TB_ST_POMODORO, NULL, NULL, true, &b->now);
    TB_EQ_INT(b->a.last_status, TB_ST_AVAILABLE);
    TB_FALSE(b->a.pomo.running);                        /* "pomodoro" shows it without starting it */
    /* errors change nothing */
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_COUNT, NULL, NULL, true, &b->now), TB_E_BAD_VALUE);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_MESSAGE, NULL, NULL, true, &b->now), TB_E_NO_MESSAGE);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_BUSY, "13:30", NULL, true, &b->now), TB_E_BAD_VALUE);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_AWAY, "24:00", NULL, true, &b->now), TB_E_BAD_VALUE);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_AWAY, "1:30", NULL, true, &b->now), TB_E_BAD_VALUE);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_AWAY, NULL, "   ", true, &b->now), TB_E_BAD_VALUE);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
}

TB_TEST(remote_status_away_back_at_and_note)
{
    bench_t *b = bench_new();
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_AWAY, "13:30", "Grabbing lunch\xE2\x80\xA6", true, &b->now), TB_OK);
    TB_EQ_STR(b->a.away_back_at, "13:30");
    TB_EQ_STR(b->a.away_note, "Grabbing lunch...");
    /* without them, the plain Away screen */
    tb_app_remote_status(&b->a, TB_ST_AWAY, NULL, NULL, true, &b->now);
    TB_EQ_STR(b->a.away_back_at, "");
    TB_EQ_STR(b->a.away_note, "");
    char long_note[64];
    memset(long_note, 'x', 41);
    long_note[41] = '\0';
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_AWAY, NULL, long_note, true, &b->now), TB_E_BAD_VALUE);
}

TB_TEST(remote_status_wakes_a_dark_screen)
{
    bench_t *b = bench_new();
    pwr_press(b);
    tb_app_remote_status(&b->a, TB_ST_AVAILABLE, NULL, NULL, true, &b->now);
    bench_drain(b);
    TB_FALSE(b->a.off);
    TB_EQ_INT(fx_last(b, TB_FX_BACKLIGHT), 70);
}

TB_TEST(remote_message)
{
    bench_t *b = bench_new();
    pwr_press(b);
    TB_EQ_INT(tb_app_remote_message(&b->a, "  On a deadline until 3 PM, message me instead  ", true, &b->now), TB_OK);
    TB_EQ_STR(b->a.message, "On a deadline until 3 PM, message me instead");
    TB_EQ_INT(b->a.message_at, b->now.wall);
    TB_EQ_INT(b->a.idx, TB_ST_MESSAGE);
    TB_FALSE(b->a.off);
    TB_EQ_INT(tb_app_remote_message(&b->a, "Don\xE2\x80\x99t interrupt", true, &b->now), TB_OK);
    TB_EQ_STR(b->a.message, "Don't interrupt");
    TB_EQ_INT(tb_app_remote_message(&b->a, " \t ", true, &b->now), TB_E_BAD_VALUE);
    char m81[90];
    memset(m81, 'a', 81);
    m81[81] = '\0';
    TB_EQ_INT(tb_app_remote_message(&b->a, m81, true, &b->now), TB_E_BAD_VALUE);
    m81[80] = '\0';
    TB_EQ_INT(tb_app_remote_message(&b->a, m81, true, &b->now), TB_OK);
    /* 80 two-byte characters fit the buffer too */
    char wide[200] = "";
    for (int i = 0; i < 80; i++) strcat(wide, "\xC3\xA9");
    TB_EQ_INT(tb_app_remote_message(&b->a, wide, true, &b->now), TB_OK);
    TB_EQ_INT(strlen(b->a.message), 160);
    /* anything net let through that the fonts can't draw shows as "?" */
    TB_EQ_INT(tb_app_remote_message(&b->a, "Pizza \xF0\x9F\x8D\x95", true, &b->now), TB_OK);
    TB_EQ_STR(b->a.message, "Pizza ?");
    /* now "message" can be picked as a status */
    tb_app_remote_status(&b->a, TB_ST_BUSY, NULL, NULL, true, &b->now);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_MESSAGE, NULL, NULL, true, &b->now), TB_OK);
    TB_EQ_STR(b->a.message, "Pizza ?");
}

TB_TEST(remote_in_setup_and_starting_up)
{
    bench_t *b = bench_new_opts(false, true);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_BUSY, NULL, NULL, true, &b->now), TB_E_IN_SETUP);
    TB_EQ_INT(tb_app_remote_message(&b->a, "hi", true, &b->now), TB_E_IN_SETUP);
    TB_EQ_INT(tb_app_remote_aside(&b->a, true, &b->now), TB_E_IN_SETUP);
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_START, 0, true, &b->now), TB_E_IN_SETUP);
    tb_settings_patch_t p = {0};
    p.has_chime = true;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);    /* settings work during setup */
    b = bench_new_opts(true, false);
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_BUSY, NULL, NULL, true, &b->now), TB_E_POWERED_OFF);
    const char *field = "x";
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, &field, &b->now), TB_E_POWERED_OFF);
    TB_TRUE(field == NULL);
}

TB_TEST(remote_pomodoro_actions)
{
    bench_t *b = bench_new();
    pwr_press(b);
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_PAUSE, 0, true, &b->now), TB_E_NOT_RUNNING);
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_EXTEND, 5, true, &b->now), TB_E_NOTHING_TO_EXTEND);
    TB_TRUE(b->a.off);                                  /* errors change nothing, not even the screen */
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_START, 0, true, &b->now), TB_OK);
    TB_FALSE(b->a.off);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_STR(b->a.toast, "Focus started");
    bench_run(b, TB_TOAST_MS + 100);
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_START, 0, true, &b->now), TB_OK);   /* already running */
    TB_EQ_STR(b->a.toast, "");
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_PAUSE, 0, true, &b->now), TB_OK);
    TB_EQ_STR(b->a.toast, "Paused");
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_TOGGLE, 0, true, &b->now), TB_OK);
    TB_EQ_STR(b->a.toast, "Focus resumed");
    tb_ms_t left = b->a.pomo.remaining_ms;
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_EXTEND, 0, true, &b->now), TB_E_BAD_VALUE);
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_EXTEND, 61, true, &b->now), TB_E_BAD_VALUE);
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_EXTEND, 10, true, &b->now), TB_OK);
    TB_EQ_INT(b->a.pomo.remaining_ms, left + 10 * 60000);
    TB_EQ_STR(b->a.toast, "+10 min");
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_SKIP, 0, true, &b->now), TB_OK);
    TB_EQ_INT(b->a.pomo.phase, TB_PH_SHORT);
    TB_TRUE(b->a.pomo.running);
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_STOP, 0, true, &b->now), TB_OK);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    TB_EQ_STR(b->a.toast, "Pomodoro stopped");
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, (tb_pomo_action_t)99, 0, true, &b->now), TB_E_BAD_VALUE);
}

TB_TEST(remote_pomodoro_start_answers_the_alarm)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, POMO_FOCUS_MS + 100);
    TB_TRUE(b->a.ringing);
    tb_app_remote_pomodoro(&b->a, TB_POMO_START, 0, true, &b->now);
    TB_FALSE(b->a.ringing);
    TB_EQ_STR(b->a.toast, "Short break started");
}

TB_TEST(remote_settings_lengths_show_the_pomodoro)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, 60000);
    tap(b);                                             /* paused */
    swipe(b, -80);                                      /* on Away */
    pwr_press(b);
    tb_settings_patch_t p = {0};
    p.has_focus_min = true;
    p.v.pomodoro.focus_min = 50;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    bench_drain(b);
    TB_FALSE(b->a.off);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_EQ_INT(b->a.pomo.remaining_ms, 50 * 60000);      /* the running phase restarts at its new length */
    TB_EQ_STR(b->a.toast, "Focus set to 50 min");
    TB_EQ_INT(fx_count(b, TB_FX_SAVE_SETTINGS), 1);
    /* the same PATCH again doesn't restart it */
    tap(b);
    bench_run(b, 60000);
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    TB_EQ_INT(b->a.pomo.remaining_ms, 49 * 60000);
    /* the others' copy */
    tb_settings_patch_t q = {0};
    q.has_short_min = true;
    q.v.pomodoro.short_min = 10;
    tb_app_remote_settings(&b->a, &q, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Short break set to 10 min");
    memset(&q, 0, sizeof q);
    q.has_long_min = true;
    q.v.pomodoro.long_min = 20;
    tb_app_remote_settings(&b->a, &q, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Long break set to 20 min");
    memset(&q, 0, sizeof q);
    q.has_long_every = true;
    q.v.pomodoro.long_every = 3;
    tb_app_remote_settings(&b->a, &q, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Long break after 3 sessions");
    /* during a call, a length change doesn't pull the bar off the call screen */
    go_status(b, TB_ST_BUSY);
    call_start(b, "Slack");
    q.v.pomodoro.long_every = 4;
    tb_app_remote_settings(&b->a, &q, NULL, &b->now);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);
}

TB_TEST(remote_settings_all_or_nothing)
{
    bench_t *b = bench_new();
    tb_settings_patch_t p = {0};
    p.has_focus_min = p.has_brightness = true;
    p.v.pomodoro.focus_min = 50;
    p.v.display.brightness = 101;
    const char *field = NULL;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, &field, &b->now), TB_E_BAD_VALUE);
    TB_EQ_STR(field, "display.brightness");
    TB_EQ_INT(b->a.set.pomodoro.focus_min, 25);
    TB_EQ_INT(fx_count(b, TB_FX_SAVE_SETTINGS), 0);
    tb_settings_patch_t q = {0};
    q.has_meeting_titles = true;
    q.v.automatic.meeting_titles = true;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &q, &field, &b->now), TB_E_NO_CALENDAR);
    TB_EQ_STR(field, "automatic.meeting_titles");
}

TB_TEST(remote_settings_misc_toasts)
{
    bench_t *b = bench_new();
    tb_settings_patch_t p = {0};
    p.has_auto_start = true;
    p.v.pomodoro.auto_start = true;
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Auto-start on");
    memset(&p, 0, sizeof p);
    p.has_chime = true;
    p.v.pomodoro.chime = true;
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Chime on");
    memset(&p, 0, sizeof p);
    p.has_name = true;
    strcpy(p.v.device.name, "Lisa's bar");
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    TB_EQ_STR(b->a.set.device.name, "Lisa's bar");
    memset(&p, 0, sizeof p);
    p.has_brightness = true;
    p.v.display.brightness = 40;
    pwr_press(b);
    bench_clear_log(b);
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    bench_drain(b);
    TB_EQ_INT(fx_count(b, TB_FX_BACKLIGHT), 0);          /* dark stays dark; the new level comes with the wake */
    TB_EQ_STR(b->a.pending_toast, "Brightness 40%");
    pwr_press(b);
    TB_EQ_INT(fx_last(b, TB_FX_BACKLIGHT), 40);
}

TB_TEST(remote_time_zone_only_when_none)
{
    bench_t *b = bench_new();
    tb_app_set_time_zone(&b->a, "America/Los_Angeles", &b->now);
    bench_drain(b);
    TB_EQ_STR(b->a.set.device.time_zone, "America/Los_Angeles");
    TB_EQ_INT(fx_count(b, TB_FX_SAVE_SETTINGS), 1);
    TB_EQ_STR(b->a.toast, "");
    tb_app_set_time_zone(&b->a, "Europe/Paris", &b->now);
    TB_EQ_STR(b->a.set.device.time_zone, "America/Los_Angeles");
    /* PATCH changes it */
    tb_settings_patch_t p = {0};
    p.has_time_zone = true;
    strcpy(p.v.device.time_zone, "Europe/Paris");
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    TB_EQ_STR(b->a.set.device.time_zone, "Europe/Paris");
}
