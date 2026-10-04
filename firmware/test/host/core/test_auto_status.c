/*
 * Automatic status (decisions.md "Automatic status"): which source wins, set aside and Show again, the Pomodoro
 * during calls and meetings, the held alarm, the source switches, lost contact, offline, after a restart.
 */
#include <string.h>

#include "core_fixture.h"

static const mt_t DESIGN_NOW = {0, 30, "Design review", "Room 4", false};

TB_TEST(auto_call_takes_over_and_ends)
{
    bench_t *b = bench_new();
    call_start(b, "Slack");
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);
    TB_EQ_INT(tb_app_color_key(&b->a, &b->now), TB_KEY_CALL);
    TB_EQ_STR(b->a.toast, "Mac: call started");
    TB_TRUE(tb_app_quiet(&b->a, &b->now));
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);                    /* your own status is still there underneath */
    bench_run(b, 2000);
    call_end(b, NULL);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    TB_EQ_STR(b->a.toast, "Call ended \xC2\xB7 back to Busy");
    TB_FALSE(tb_app_quiet(&b->a, &b->now));
}

TB_TEST(auto_call_label)
{
    bench_t *b = bench_new();
    char lab[80];
    call_start(b, NULL);
    TB_EQ_STR(tb_app_call_label(&b->a, lab, sizeof lab), "");             /* "Mic or camera on" / no name */
    call_start(b, "Microsoft Teams (work or school)");
    TB_EQ_STR(tb_app_call_label(&b->a, lab, sizeof lab), "Microsoft Teams (work o\xE2\x80\xA6");
    TB_EQ_STR(b->a.call.app, "Microsoft Teams (work or school)");          /* status still reports it whole */
    call_start(b, "Huddle \xF0\x9F\x8E\xA7");
    TB_EQ_STR(tb_app_call_label(&b->a, lab, sizeof lab), "");             /* undrawable: "From your Mac" alone */
}

TB_TEST(auto_meeting_starts_and_ends_on_time)
{
    bench_t *b = bench_new();
    mt_t m = {2, 30, "Design review", "Room 4", false};
    cal_save(b, &m, 1);
    TB_TRUE(b->a.set.automatic.calendar);               /* a first address turns Calendar meetings on */
    TB_EQ_STR(b->a.toast, "Calendar synced \xC2\xB7 1 meeting left today");
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    bench_run(b, 119000);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    bench_run(b, 1050);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_MEETING);
    TB_EQ_STR(b->a.toast, "Calendar: meeting started");     /* titles off by default */
    TB_EQ_INT(tb_app_meetings_left(&b->a, &b->now), 1);      /* in progress counts */
    bench_run(b, 30 * 60000);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    TB_EQ_STR(b->a.toast, "Meeting ended \xC2\xB7 back to Busy");
    char left[48];
    TB_EQ_STR(tb_app_left_text(&b->a, &b->now, left, sizeof left), "no more meetings today");
}

TB_TEST(auto_meeting_titles_and_privacy)
{
    bench_t *b = bench_new();
    mt_t m[2] = {{1, 30, "Quarterly planning with the whole design team", "https://meet.google.com/abc", false},
                 {45, 30, "Secret", "Room 9", true}};
    cal_save(b, m, 2);
    tb_settings_patch_t p = {0};
    p.has_meeting_titles = true;
    p.v.automatic.meeting_titles = true;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    TB_EQ_STR(b->a.toast, "Meeting titles on");
    bench_run(b, 61000);
    /* withTitle(): cut to 24 characters, with the title's span marked for the ui */
    TB_EQ_STR(b->a.toast, "Calendar: Quarterly planning with\xE2\x80\xA6 started");   /* 23 + "…" */
    TB_EQ_INT(b->a.toast_title_off, 10);
    TB_EQ_INT(b->a.toast_title_len, strlen("Quarterly planning with\xE2\x80\xA6"));
    const tb_meeting_t *cur = tb_app_meet_now(&b->a, &b->now);
    TB_TRUE(cur != NULL);
    TB_EQ_STR(tb_app_place_of(&b->a, cur), "");           /* a web address isn't a place */
    const tb_meeting_t *nx = tb_app_next_meeting(&b->a, &b->now);
    TB_TRUE(nx != NULL);
    TB_EQ_STR(tb_app_title_of(&b->a, nx), "");            /* private: never a title */
    TB_EQ_STR(tb_app_place_of(&b->a, nx), "");
    /* HTTPS in capitals is a web address too */
    tb_meeting_t e = *cur;
    strcpy(e.location, "HTTPS://zoom.us/j/1");
    TB_EQ_STR(tb_app_place_of(&b->a, &e), "");
    strcpy(e.location, "Room 4");
    TB_EQ_STR(tb_app_place_of(&b->a, &e), "Room 4");
}

TB_TEST(auto_call_beats_meeting_and_hands_back)
{
    bench_t *b = bench_new();
    cal_save(b, &DESIGN_NOW, 1);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_MEETING);
    call_start(b, "Zoom");
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);
    TB_EQ_STR(b->a.toast, "Mac: call started");
    call_end(b, NULL);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_MEETING);
    TB_EQ_STR(b->a.toast, "Call ended \xC2\xB7 still in a meeting");
    /* with titles on, the title */
    b->a.set.automatic.meeting_titles = true;
    call_start(b, "Zoom");
    call_end(b, NULL);
    TB_EQ_STR(b->a.toast, "Call ended \xC2\xB7 still in Design review");
}

TB_TEST(auto_back_to_back_meetings_flow)
{
    bench_t *b = bench_new();
    mt_t m[2] = {{0, 30, "One", "", false}, {30, 30, "Two", "", false}};
    cal_save(b, m, 2);
    uint32_t first;
    TB_EQ_INT(tb_app_auto_top(&b->a, &b->now, &first), TB_AUTO_MEETING);
    tap(b);                                              /* set the first aside */
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    bench_run(b, 30 * 60000 + 100);
    uint32_t second;
    TB_EQ_INT(tb_app_auto_top(&b->a, &b->now, &second), TB_AUTO_MEETING);   /* the next one takes over again */
    TB_TRUE(first != second);
    TB_EQ_STR(b->a.toast, "Calendar: meeting started");
    TB_EQ_INT(tb_app_aside_kind(&b->a, &b->now), TB_AUTO_NONE);
}

TB_TEST(auto_set_aside_lasts_until_it_ends_and_a_new_call_takes_over)
{
    bench_t *b = bench_new();
    call_start(b, "Slack");
    boot_btn(b);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    TB_EQ_INT(tb_app_aside_kind(&b->a, &b->now), TB_AUTO_CALL);
    TB_TRUE(tb_app_quiet(&b->a, &b->now));               /* still quiet while set aside */
    /* the same call repeating (heartbeat with a new app name) stays aside */
    tb_call_t same = b->a.call;
    strcpy(same.app, "Slack huddle");
    tb_app_set_call(&b->a, &same, NULL, &b->now);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    /* a new call takes over */
    call_start(b, "Zoom");
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);
    TB_EQ_STR(b->a.toast, "Mac: call started");
}

TB_TEST(auto_show_again_from_the_quick_menu)
{
    bench_t *b = bench_new();
    call_start(b, "Slack");
    tap(b);
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);
    const tb_tile_t *t = tile_with(b, TB_ACT_SHOW_AGAIN);
    TB_TRUE(t != NULL);
    TB_EQ_INT(tile_index(b, TB_ACT_SHOW_AGAIN), 1);     /* in the Calendar tile's place */
    TB_EQ_STR(t->label, "Set aside");
    TB_EQ_STR(t->value, "Show again");
    TB_TRUE(t->value_two_lines);
    TB_EQ_STR(t->foot, "the call");
    tap_tile_named(b, TB_ACT_SHOW_AGAIN);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);
    TB_EQ_STR(b->a.toast, "Showing the call again");
}

TB_TEST(auto_show_again_in_the_timer_menu_and_remote)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, 1000);                                  /* (0 ms in, a paused timer still reads "ready") */
    cal_save(b, &DESIGN_NOW, 1);
    TB_FALSE(b->a.pomo.running);                         /* the meeting paused it */
    TB_EQ_INT(b->a.pomo.auto_paused, TB_AUTO_MEETING);
    TB_EQ_INT(tb_app_paused_by(&b->a, &b->now), TB_AUTO_MEETING);
    tap(b);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_EQ_STR(b->a.toast, "Meeting set aside \xC2\xB7 hold to show it again");
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_TIMER);
    TB_EQ_STR(tile_with(b, TB_ACT_SHOW_AGAIN)->foot, "the meeting");
    tap_tile_named(b, TB_ACT_CLOSE);
    /* the Remote's Show again; then Show again with nothing aside is an error */
    TB_EQ_INT(tb_app_remote_aside(&b->a, false, &b->now), TB_OK);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_MEETING);
    TB_EQ_STR(b->a.toast, "Showing the meeting again");
    TB_EQ_INT(tb_app_remote_aside(&b->a, false, &b->now), TB_E_NOTHING_SET_ASIDE);
    TB_EQ_INT(tb_app_remote_aside(&b->a, true, &b->now), TB_OK);
    TB_EQ_STR(b->a.toast, "Meeting set aside \xC2\xB7 hold to show it again");
    TB_EQ_INT(tb_app_remote_aside(&b->a, true, &b->now), TB_E_NOTHING_TO_SET_ASIDE);
}

TB_TEST(auto_nothing_set_aside_from_a_stale_tile)
{
    bench_t *b = bench_new();
    call_start(b, "Slack");
    tap(b);
    hold(b);
    int i = tile_index(b, TB_ACT_SHOW_AGAIN);
    /* the call ends while the menu is open; the tile refreshes to the Calendar tile */
    call_end(b, NULL);
    TB_EQ_INT(b->a.menu.tiles[i].action, TB_ACT_SYNC);
    TB_EQ_STR(b->a.menu.tiles[i].foot, "add it on the Remote");
}

TB_TEST(auto_pomodoro_pauses_and_stays_paused_after)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, 60000);
    call_start(b, "Slack");
    TB_FALSE(b->a.pomo.running);
    TB_EQ_INT(b->a.pomo.auto_paused, TB_AUTO_CALL);
    tb_ms_t left = b->a.pomo.remaining_ms;
    bench_run(b, 120000);
    TB_EQ_INT(b->a.pomo.remaining_ms, left);
    call_end(b, NULL);
    TB_EQ_STR(b->a.toast, "Call ended \xC2\xB7 back to Pomodoro");
    TB_FALSE(b->a.pomo.running);                        /* still paused: a tap or flip resumes */
    TB_EQ_INT(tb_app_paused_by(&b->a, &b->now), TB_AUTO_NONE);
    tap(b);
    TB_EQ_STR(b->a.toast, "Focus resumed");
    TB_EQ_INT(b->a.pomo.auto_paused, TB_AUTO_NONE);
}

TB_TEST(auto_held_alarm_chimes_once_when_the_call_ends)
{
    bench_t *b = bench_new();
    pomo_start(b);
    bench_run(b, POMO_FOCUS_MS + 100);
    TB_TRUE(b->a.ringing);
    bench_run(b, 2000);
    bench_clear_log(b);
    call_start(b, "Zoom");
    TB_FALSE(b->a.ringing);                              /* silenced */
    TB_EQ_INT(b->a.pomo.held_alarm, TB_AUTO_CALL);
    TB_TRUE(b->a.pomo.waiting);                          /* the Pomodoro keeps waiting */
    bench_run(b, 30000);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 0);
    tb_ms_t flashed = b->a.flash_at;
    call_end(b, NULL);
    TB_EQ_STR(b->a.toast, "Call ended");                 /* the waiting screen speaks for itself */
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 1);
    TB_TRUE(b->a.flash_at > flashed);
    TB_FALSE(b->a.ringing);                              /* one chime and flash, no repeats */
    bench_run(b, 20000);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 1);
    TB_EQ_INT(b->a.pomo.held_alarm, TB_AUTO_NONE);
}

TB_TEST(auto_held_alarm_waits_for_the_meeting_too)
{
    bench_t *b = bench_new();
    pomo_start(b);
    mt_t m = {POMO_FOCUS_MS / 60000 + 1, 10, "Sync", "", false};   /* starts a minute after the alarm */
    cal_save(b, &m, 1);
    bench_run(b, POMO_FOCUS_MS + 100);
    TB_TRUE(b->a.ringing);
    TB_EQ_INT(b->a.pomo.held_alarm, TB_AUTO_NONE);
    bench_run(b, 60000);                                 /* the meeting starts (the alarm had chimed meanwhile) */
    TB_EQ_INT(b->a.pomo.held_alarm, TB_AUTO_MEETING);
    TB_FALSE(b->a.ringing);
    bench_clear_log(b);
    tap(b);                                              /* set aside: still quiet, the alarm stays held */
    TB_EQ_INT(b->a.pomo.held_alarm, TB_AUTO_MEETING);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 0);
    bench_run(b, 10 * 60000);
    TB_EQ_INT(b->a.pomo.held_alarm, TB_AUTO_NONE);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 1);
}

TB_TEST(auto_no_chime_during_a_set_aside_call)
{
    bench_t *b = bench_new();
    pomo_start(b);
    call_start(b, "Slack");
    flip(b);                                             /* shows and resumes the Pomodoro, call aside */
    TB_TRUE(b->a.pomo.running);
    bench_clear_log(b);
    bench_run(b, POMO_FOCUS_MS);
    TB_TRUE(b->a.ringing);                               /* alarms flash without the chime */
    TB_TRUE(b->a.flash_at != 0);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 0);
    bench_run(b, 9000);
    TB_EQ_INT(fx_count(b, TB_FX_CHIME), 0);
}

TB_TEST(auto_sources_off_and_on)
{
    bench_t *b = bench_new();
    call_start(b, "Slack");
    tb_settings_patch_t p = {0};
    p.has_mac = true;
    p.v.automatic.mac = false;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    TB_EQ_STR(b->a.toast, "Calls from your Mac off \xC2\xB7 back to Busy");
    TB_FALSE(tb_app_quiet(&b->a, &b->now));
    TB_TRUE(b->a.call.active);                           /* still recorded */
    p.v.automatic.mac = true;
    TB_EQ_INT(tb_app_remote_settings(&b->a, &p, NULL, &b->now), TB_OK);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);
    TB_EQ_STR(b->a.toast, "Calls from your Mac on \xC2\xB7 On a call");
    /* a call set aside is forgotten when the source goes off, so it shows again when it's back on */
    tap(b);
    p.v.automatic.mac = false;
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Calls from your Mac off");
    p.v.automatic.mac = true;
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);

    /* Calendar meetings */
    b = bench_new();
    cal_save(b, &DESIGN_NOW, 1);
    tb_settings_patch_t q = {0};
    q.has_calendar = true;
    q.v.automatic.calendar = false;
    tb_app_remote_settings(&b->a, &q, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Calendar meetings off \xC2\xB7 back to Busy");
    TB_FALSE(tb_app_cal_data(&b->a));
    q.v.automatic.calendar = true;
    tb_app_remote_settings(&b->a, &q, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Calendar meetings on \xC2\xB7 In a meeting");
    /* with nothing on, just the switch */
    b = bench_new();
    q.v.automatic.calendar = false;
    tb_app_remote_settings(&b->a, &q, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Calendar meetings off");
}

TB_TEST(auto_lost_contact_with_the_mac)
{
    bench_t *b = bench_new();
    call_start(b, "Slack");
    call_end(b, "Lost contact with your Mac");
    TB_EQ_STR(b->a.toast, "Lost contact with your Mac \xC2\xB7 back to Busy");
    /* when the call was set aside, the toast still says so */
    call_start(b, "Slack");
    tap(b);
    call_end(b, "Lost contact with your Mac");
    TB_EQ_STR(b->a.toast, "Lost contact with your Mac \xC2\xB7 call ended");
    /* a Mac going quiet without a call says nothing (the Mac icon goes) */
    tb_app_set_mac_link(&b->a, TB_LINK_NONE, &b->now);
    bench_run(b, TB_TOAST_MS + 100);
    call_end(b, "Lost contact with your Mac");
    TB_EQ_STR(b->a.toast, "");
}

TB_TEST(auto_skipping_wifi_ends_the_meeting)
{
    bench_t *b = bench_new();
    cal_save(b, &DESIGN_NOW, 1);
    TB_TRUE(tb_app_cal_data(&b->a));
    b->a.wifi_mode = TB_WIFI_SETUP;                      /* Set up again, then Skip */
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_SETUP);
    tap_tile_named(b, TB_ACT_WIFI_SKIP);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OFFLINE);
    TB_FALSE(tb_app_cal_data(&b->a));
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    TB_EQ_STR(b->a.toast, "Offline, calendar off \xC2\xB7 back to Busy");
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SKIP), 1);
}

TB_TEST(auto_wifi_dropped_keeps_following_the_saved_copy)
{
    bench_t *b = bench_new();
    mt_t m = {5, 30, "Design review", "", false};
    cal_save(b, &m, 1);
    tb_app_wifi_link(&b->a, false, NULL, NULL, &b->now);
    TB_TRUE(tb_app_cal_data(&b->a));
    bench_run(b, 5 * 60000 + 100);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_MEETING);
}

TB_TEST(auto_never_interrupts_a_menu_or_setup_and_dark_stays_dark)
{
    bench_t *b = bench_new();
    hold(b);
    call_start(b, "Slack");
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);            /* the menu stays */
    TB_EQ_STR(b->a.toast, "");
    TB_EQ_STR(b->a.pending_toast, "Mac: call started");
    tap_tile_named(b, TB_ACT_CLOSE);
    bench_run(b, 100);
    TB_EQ_STR(b->a.toast, "Mac: call started");          /* shows once the menu closes */
    TB_EQ_STR(b->a.pending_toast, "");

    b = bench_new();
    pwr_press(b);
    call_start(b, "Slack");
    TB_TRUE(b->a.off);                                   /* a dark screen stays dark */
    TB_EQ_INT(fx_last(b, TB_FX_BACKLIGHT), 0);
    TB_EQ_STR(b->a.pending_toast, "Mac: call started");
    tap(b);
    bench_run(b, 100);
    TB_EQ_STR(b->a.toast, "Mac: call started");          /* shows the change when woken */

    b = bench_new_opts(false, true);                     /* the Wi-Fi setup screens */
    call_start(b, "Slack");
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_SETUP);
    TB_EQ_STR(b->a.pending_toast, "Mac: call started");
}

TB_TEST(auto_after_restart_picks_up_a_call)
{
    bench_t *b = bench_new();
    call_start(b, "Slack");
    tap(b);                                              /* set aside */
    hold(b);
    tap_tile_named(b, TB_ACT_POWER);
    tap_tile_named(b, TB_ACT_RESTART);
    bench_run(b, TB_BOOT_SPLASH_MS + 60);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);   /* set-aside is forgotten */
    TB_EQ_STR(b->a.toast, "Ready \xC2\xB7 On a call");
    b = bench_new();
    cal_save(b, &DESIGN_NOW, 1);
    hold(b);
    tap_tile_named(b, TB_ACT_POWER);
    tap_tile_named(b, TB_ACT_RESTART);
    bench_run(b, TB_BOOT_SPLASH_MS + 60);
    TB_EQ_STR(b->a.toast, "Ready \xC2\xB7 In a meeting");
}

TB_TEST(auto_remote_status_sets_aside_unless_asked_not_to)
{
    bench_t *b = bench_new();
    call_start(b, "Slack");
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_AVAILABLE, NULL, NULL, false, &b->now), TB_OK);
    TB_EQ_INT(b->a.idx, TB_ST_AVAILABLE);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_CALL);   /* an automation changed it in the background */
    TB_EQ_INT(tb_app_remote_status(&b->a, TB_ST_AWAY, NULL, NULL, true, &b->now), TB_OK);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    TB_EQ_STR(b->a.toast, "Call set aside \xC2\xB7 Away");
    /* Pomodoro controls on the Remote set it aside too */
    b = bench_new();
    cal_save(b, &DESIGN_NOW, 1);
    TB_EQ_INT(tb_app_remote_pomodoro(&b->a, TB_POMO_TOGGLE, 0, true, &b->now), TB_OK);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
    TB_EQ_INT(b->a.idx, TB_ST_POMODORO);
    TB_TRUE(b->a.pomo.running);
}

TB_TEST(auto_calendar_removed)
{
    bench_t *b = bench_new();
    cal_save(b, &DESIGN_NOW, 1);
    b->a.set.automatic.meeting_titles = true;
    tb_app_calendar_event(&b->a, TB_CALEV_REMOVED, &b->now);
    TB_FALSE(b->a.cal_saved);
    TB_FALSE(b->a.set.automatic.calendar);
    TB_FALSE(b->a.set.automatic.meeting_titles);
    TB_EQ_INT(b->a.n_meetings, 0);
    TB_EQ_STR(b->a.toast, "Calendar removed \xC2\xB7 back to Busy");
    /* with no meeting on, just the toast */
    b = bench_new();
    cal_save(b, NULL, 0);
    bench_run(b, TB_TOAST_MS + 100);
    tb_app_calendar_event(&b->a, TB_CALEV_REMOVED, &b->now);
    TB_EQ_STR(b->a.toast, "Calendar removed");
    /* a replaced address keeps the user's switch as it was */
    b = bench_new();
    cal_save(b, NULL, 0);
    b->a.set.automatic.calendar = false;
    tb_app_calendar_event(&b->a, TB_CALEV_SAVED, &b->now);
    TB_FALSE(b->a.set.automatic.calendar);
    TB_EQ_STR(b->a.toast, "Calendar synced \xC2\xB7 no more meetings today");
    /* the setup page's address that failed */
    tb_app_calendar_event(&b->a, TB_CALEV_SETUP_FAILED, &b->now);
    TB_EQ_STR(b->a.toast, "Calendar address didn't work \xC2\xB7 add it on the Remote");
}

TB_TEST(auto_meetings_today_only_and_clock_unknown)
{
    bench_t *b = bench_new();
    mt_t m[3] = {{60, 30, "a", "", false}, {120, 30, "b", "", false}, {16 * 60, 30, "tomorrow", "", false}};
    tb_epoch_t saved_at = b->now.wall;
    cal_save(b, m, 3);
    TB_EQ_INT(tb_app_meetings_left(&b->a, &b->now), 2);
    char left[48];
    TB_EQ_STR(tb_app_left_text(&b->a, &b->now, left, sizeof left), "2 meetings left today");
    TB_EQ_INT(tb_app_next_meeting(&b->a, &b->now)->start, saved_at + 3600);
    tb_clock_t unknown = b->now;
    unknown.valid = false;
    TB_TRUE(tb_app_next_meeting(&b->a, &unknown) == NULL);
    TB_TRUE(tb_app_current_meeting(&b->a, &unknown) == NULL);
    TB_EQ_INT(tb_app_meetings_left(&b->a, &unknown), 0);
}
