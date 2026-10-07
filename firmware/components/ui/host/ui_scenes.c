/* ui_scenes.c: see ui_scenes.h. Owner: ui builder. */
#include <stdio.h>
#include <string.h>

#include "tb_fmt.h"
#include "tb_jira.h"
#include "tb_text.h"
#include "ui_scenes.h"

#define REF UI_SCENE_WALL
#define MIN (60 * 1000)

/* ---------- driving the bar ---------- */

static void run(tb_app_t *a, tb_clock_t *now, tb_ms_t ms)
{
    while (ms > 0) {
        tb_ms_t d = ms < 50 ? ms : 50;
        now->mono += d;
        ms -= d;
        tb_app_pointer_poll(a, now);
        tb_app_tick(a, now);
    }
    tb_effect_t fx[TB_EFFECTS_MAX];
    while (tb_app_take_effects(a, fx, TB_EFFECTS_MAX) > 0) {
    }
}

static void quiet_toast(tb_app_t *a)
{
    a->toast[0] = '\0';
    a->pending_toast[0] = '\0';
    a->toast_title_off = a->toast_title_len = 0;
}

/* A bar at 2:04 PM, Wi-Fi joined, past the splash, showing st, picked 12 minutes ago. One Pomodoro done today
 * (31 minutes focused), the Pomodoro itself ready. */
static void base_opts(tb_app_t *a, tb_clock_t *now, tb_status_t st, bool wifi)
{
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    *now = (tb_clock_t){.mono = 60 * MIN, .wall = REF, .valid = true};
    tb_app_init(a, &s, wifi ? TB_WIFI_OK : TB_WIFI_SETUP, now);
    tb_app_restore(a, st, st == TB_ST_POMODORO || st == TB_ST_CLOCK ? TB_ST_BUSY : st,
                   "On a deadline until 3 PM, message me instead", REF, 1, 31 * MIN + 18000, tb_local_yyyymmdd(REF));
    if (wifi) {
        tb_strlcpy(a->wifi_ssid, "Office-WiFi", sizeof a->wifi_ssid);
        tb_app_wifi_link(a, true, "10.0.4.42", "minibar.local", now);
    }
    run(a, now, TB_BOOT_SPLASH_MS + 100);
    quiet_toast(a);
    a->idx = st;        /* the splash's end turns a restored Pomodoro back into the last status; scenes want st */
    a->rev++;
    now->wall = REF;
    a->since_ms = now->mono - 12 * MIN;
    a->since = REF - 12 * 60;
}

static void base(tb_app_t *a, tb_clock_t *now, tb_status_t st)
{
    base_opts(a, now, st, true);
}

/* The Pomodoro: phase, round, seconds left, running; waiting after a phase ended (just_ended). */
static void pomo(tb_app_t *a, tb_phase_t ph, int round, int secs, bool running)
{
    a->pomo.phase = ph;
    a->pomo.round = (uint8_t)round;
    a->pomo.remaining_ms = (tb_ms_t)secs * 1000;
    a->pomo.running = running;
    a->pomo.waiting = false;
    a->pomo.just_ended = -1;
    a->rev++;
}

static void pomo_waiting(tb_app_t *a, tb_phase_t ended, tb_phase_t next, int round)
{
    a->pomo.phase = next;
    a->pomo.round = (uint8_t)round;
    a->pomo.remaining_ms = tb_pomo_phase_len(&a->set, next);
    a->pomo.running = false;
    a->pomo.waiting = true;
    a->pomo.just_ended = (int8_t)ended;
    a->rev++;
}

typedef struct {
    int start_min, len_min;     /* from 2:04 PM */
    const char *title, *loc;
    bool priv;
} mt_t;

/* The mock-up's sampleEvents() at 2:04 PM: Design review 3:00 (Room 4), 1:1 with Sam 4:00, Team retro 5:30 (Room 2). */
static const mt_t SAMPLE[] = {
    {56, 45, "Design review", "Room 4", false},
    {116, 30, "1:1 with Sam", "", false},
    {206, 60, "Team retro", "Room 2", false},
};
/* A meeting from 1:45 to 2:30 PM, then the 1:1 at 3:00. */
static const mt_t NOW_MEETING[] = {
    {-19, 45, "Design review", "Room 4", false},
    {56, 30, "1:1 with Sam", "", false},
};

static void calendar(tb_app_t *a, tb_clock_t *now, bool titles, const mt_t *m, int n)
{
    tb_meeting_t list[TB_MEETINGS_MAX];
    memset(list, 0, sizeof list);
    for (int i = 0; i < n; i++) {
        list[i].id = (uint32_t)(100 + i);
        list[i].start = REF + m[i].start_min * 60;
        list[i].end = list[i].start + m[i].len_min * 60;
        tb_strlcpy(list[i].title, m[i].title, sizeof list[i].title);
        tb_strlcpy(list[i].location, m[i].loc, sizeof list[i].location);
        list[i].priv = m[i].priv;
    }
    a->set.automatic.calendar = true;
    a->set.automatic.meeting_titles = titles;
    tb_app_set_calendar(a, true, false, REF - 120, now);
    tb_app_set_meetings(a, list, n, now);
    quiet_toast(a);
}

static void call(tb_app_t *a, tb_clock_t *now, const char *app)
{
    tb_call_t c = {.active = true, .id = 7, .via = TB_LINK_USB, .since_ms = now->mono - 12 * MIN, .since = REF - 12 * 60};
    tb_strlcpy(c.app, app ? app : "", sizeof c.app);
    tb_app_set_mac_link(a, TB_LINK_USB, now);
    tb_app_set_call(a, &c, NULL, now);
    quiet_toast(a);
}

static void hold_menu(tb_app_t *a, tb_clock_t *now)
{
    tb_app_pointer(a, true, 200, 80, TB_TILE_NONE, now);
    run(a, now, 650);
    tb_app_pointer(a, false, 200, 80, TB_TILE_NONE, now);
    run(a, now, 50);
}

static void tap_action(tb_app_t *a, tb_clock_t *now, tb_action_t act)
{
    for (int i = 0; i < a->menu.n; i++)
        if (a->menu.tiles[i].action == act) {
            tb_app_pointer(a, true, 100, 80, (int8_t)i, now);
            run(a, now, 60);
            tb_app_pointer(a, false, 100, 80, (int8_t)i, now);
            run(a, now, 50);
            return;
        }
}

/* ---------- scenes ---------- */

static void s_available(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_AVAILABLE); }
static void s_available_free(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_AVAILABLE); calendar(a, n, false, SAMPLE, 3); }
static void s_available_free_titles(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_AVAILABLE); calendar(a, n, true, SAMPLE, 3); }
static void s_available_rest(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_AVAILABLE); calendar(a, n, false, NULL, 0); }
static void s_busy(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_BUSY); }
static void s_meeting_manual(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_MEETING); }
static void s_meeting_manual_next(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_MEETING); calendar(a, n, true, SAMPLE, 3); }
static void s_away(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_AWAY);
    a->since_ms = n->mono - 8 * MIN;
    a->since = REF - 8 * 60;
}
static void s_away_back(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_AWAY);
    tb_app_remote_status(a, TB_ST_AWAY, "14:30", "Grabbing lunch", true, n);
    quiet_toast(a);
    a->since_ms = n->mono - 8 * MIN;
    a->since = REF - 8 * 60;
}
static void s_message_short(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_MESSAGE);
    tb_app_remote_message(a, "Out to lunch", true, n);
    quiet_toast(a);
}
static void s_message_long(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_MESSAGE); }
static void s_clock(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_CLOCK); calendar(a, n, false, SAMPLE, 3); }
static void s_clock_titles(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_CLOCK); calendar(a, n, true, SAMPLE, 3); }
static void s_clock_nothing(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_CLOCK); calendar(a, n, false, NULL, 0); }
static void s_clock_notsetup(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_CLOCK); }
static void s_clock_caloff(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_CLOCK);
    calendar(a, n, false, SAMPLE, 3);
    a->set.automatic.calendar = false;
    a->rev++;
}
static void s_clock_offline(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_CLOCK);
    calendar(a, n, false, SAMPLE, 3);
    a->wifi_mode = TB_WIFI_OFFLINE;
    a->wifi_link_up = false;
    a->rev++;
}
static void s_clock_unset(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_CLOCK);
    n->valid = false;
}
static void s_pomo_ready(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_POMODORO); }
static void s_pomo_focus(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_POMODORO); pomo(a, TB_PH_FOCUS, 2, 18 * 60 + 42, true); }
static void s_pomo_paused(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_POMODORO); pomo(a, TB_PH_FOCUS, 2, 18 * 60 + 42, false); }
static void s_pomo_short(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_POMODORO); pomo(a, TB_PH_SHORT, 2, 3 * 60 + 10, true); }
static void s_pomo_long(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_POMODORO);
    pomo(a, TB_PH_LONG, 4, 12 * 60, true);
    a->pomo.done_today = 4;
}
/* The longest real copy in the small slots: a full day's foot, and the longest paused kickers. */
static void s_pomo_bigfoot(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_POMODORO);
    pomo(a, TB_PH_FOCUS, 2, 18 * 60 + 42, true);
    a->pomo.done_today = 12;
    a->pomo.focused_ms = (5 * 60 + 10) * MIN;
}
static void s_pomo_paused_long(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_POMODORO);
    pomo(a, TB_PH_LONG, 4, 12 * 60, false);
    a->pomo.done_today = 4;
}
static void s_pomo_paused_short(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_POMODORO); pomo(a, TB_PH_SHORT, 2, 3 * 60 + 10, false); }
static void s_pomo_break_time(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_POMODORO);
    pomo_waiting(a, TB_PH_FOCUS, TB_PH_SHORT, 2);
    a->pomo.done_today = 2;
}
static void s_pomo_back_to_it(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_POMODORO);
    pomo_waiting(a, TB_PH_SHORT, TB_PH_FOCUS, 3);
    a->pomo.done_today = 2;
}
static void s_pill_focus(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_BUSY); pomo(a, TB_PH_FOCUS, 2, 18 * 60 + 42, true); }
static void s_pill_paused(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_AVAILABLE); pomo(a, TB_PH_FOCUS, 2, 18 * 60 + 42, false); }
static void s_pill_done(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_BUSY); pomo_waiting(a, TB_PH_FOCUS, TB_PH_SHORT, 2); }
static void s_pill_short(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_BUSY); pomo(a, TB_PH_SHORT, 2, 3 * 60 + 10, true); }
static void s_call(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_BUSY); call(a, n, "Slack"); }
static void s_call_noapp(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_BUSY); call(a, n, NULL); }
static void s_call_longname(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_BUSY); call(a, n, "Microsoft Teams (work or school)"); }
static void s_call_paused(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_POMODORO);
    pomo(a, TB_PH_FOCUS, 2, 18 * 60 + 42, true);
    call(a, n, "Slack");        /* the call pauses the running timer: the pill says Paused */
}
static void s_call_meeting(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_BUSY); calendar(a, n, false, NOW_MEETING, 2); call(a, n, "Zoom"); }
static void s_call_meeting_titles(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_BUSY); calendar(a, n, true, NOW_MEETING, 2); call(a, n, "Zoom"); }
static void s_meeting_cal(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_BUSY); calendar(a, n, false, NOW_MEETING, 2); }
static void s_meeting_cal_titles(tb_app_t *a, tb_clock_t *n) { base(a, n, TB_ST_BUSY); calendar(a, n, true, NOW_MEETING, 2); }
static void s_meeting_cal_private(tb_app_t *a, tb_clock_t *n)
{
    static const mt_t M[] = {{-19, 45, "Doctor", "Clinic", true}, {56, 30, "1:1 with Sam", "", false}};
    base(a, n, TB_ST_BUSY);
    calendar(a, n, true, M, 2);
}
static void s_meeting_cal_long(tb_app_t *a, tb_clock_t *n)
{
    static const mt_t M[] = {{-19, 45, "Quarterly planning with the platform and infrastructure teams", "", false}};
    base(a, n, TB_ST_BUSY);
    calendar(a, n, true, M, 1);
}
static void s_aside_call(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_BUSY);
    call(a, n, "Slack");
    tb_app_remote_aside(a, true, n);
    quiet_toast(a);
}
static void s_aside_meeting_pill(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_BUSY);
    pomo(a, TB_PH_FOCUS, 2, 18 * 60 + 42, true);
    calendar(a, n, false, NOW_MEETING, 2);     /* takes over and pauses the timer */
    tb_app_set_mac_link(a, TB_LINK_USB, n);
    tb_app_remote_aside(a, true, n);
    quiet_toast(a);
}
static void s_mac_icon(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_BUSY);
    tb_app_set_mac_link(a, TB_LINK_USB, n);
}
static void s_setup_qr(tb_app_t *a, tb_clock_t *n) { base_opts(a, n, TB_ST_CLOCK, false); }
static void s_setup_connecting(tb_app_t *a, tb_clock_t *n)
{
    base_opts(a, n, TB_ST_CLOCK, false);
    tb_app_wifi_connecting(a, "Office-WiFi", n);
    quiet_toast(a);
}
static void s_setup_connected(tb_app_t *a, tb_clock_t *n)
{
    base_opts(a, n, TB_ST_CLOCK, false);
    tb_app_wifi_connected(a, "Office-WiFi", "10.0.4.42", "minibar.local", n);
    quiet_toast(a);
}
static void s_setup_failed(tb_app_t *a, tb_clock_t *n)
{
    base_opts(a, n, TB_ST_CLOCK, false);
    tb_app_wifi_failed(a, "Office-WiFi", "Wrong password", n);
    quiet_toast(a);
}
static void s_menu_quick(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_AVAILABLE);
    calendar(a, n, false, SAMPLE, 3);
    hold_menu(a, n);
}
/* Firmware only (the mock-up's Display menu also has Theme and Tap sound, which the bar doesn't have yet). */
static void s_menu_display(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_AVAILABLE);
    calendar(a, n, false, SAMPLE, 3);
    hold_menu(a, n);
    tap_action(a, n, TB_ACT_DISPLAY);
}
static void s_menu_display_24h(tb_app_t *a, tb_clock_t *n)
{
    s_menu_display(a, n);
    tap_action(a, n, TB_ACT_TIME_FMT);
}
/* Time format 24-hour: the clock, and Away's longest headline (Bold Signal steps down to 62 px for it). */
static void s_clock_24h(tb_app_t *a, tb_clock_t *n)
{
    s_clock(a, n);
    a->set.more.time_24h = true;
    a->rev++;
}
static void s_away_back_24h(tb_app_t *a, tb_clock_t *n)
{
    s_away_back(a, n);
    snprintf(a->away_back_at, sizeof a->away_back_at, "03:40");
    a->set.more.time_24h = true;
    a->rev++;
}
/* The Jira screen (decisions.md "Jira issue count"): a count answered 22 minutes ago, unless the scene says otherwise. */
static void jira_scene(tb_app_t *a, tb_clock_t *n, tb_jira_state_t st, int32_t count, int alert, int64_t ok_ago_s)
{
    base(a, n, TB_ST_JIRA);
    tb_jira_t j;
    tb_jira_init(&j);
    tb_jira_configure(&j, "Open bugs", alert);
    if (st != TB_JIRA_LOADING) tb_jira_apply(&j, TB_JIRA_RES_OK, count < 0 ? 0 : count, REF - ok_ago_s);
    if (st == TB_JIRA_UNREACHABLE) tb_jira_apply(&j, TB_JIRA_RES_UNREACHABLE, 0, REF);
    if (st == TB_JIRA_TOKEN) tb_jira_apply(&j, TB_JIRA_RES_TOKEN, 0, REF);
    if (st == TB_JIRA_NOFILTER) tb_jira_apply(&j, TB_JIRA_RES_NOFILTER, 0, REF);
    if (st == TB_JIRA_UNREACHABLE && count < 0) j.count = -1;
    tb_app_set_jira(a, &j, n);
    quiet_toast(a);
}
static void s_jira_ok(tb_app_t *a, tb_clock_t *n) { jira_scene(a, n, TB_JIRA_OK, 12, 20, 22 * 60); }
static void s_jira_one(tb_app_t *a, tb_clock_t *n) { jira_scene(a, n, TB_JIRA_OK, 1, -1, 22 * 60); }
static void s_jira_zero(tb_app_t *a, tb_clock_t *n) { jira_scene(a, n, TB_JIRA_OK, 0, 10, 22 * 60); }
static void s_jira_over(tb_app_t *a, tb_clock_t *n) { jira_scene(a, n, TB_JIRA_OK, 12, 10, 22 * 60); }
static void s_jira_big(tb_app_t *a, tb_clock_t *n) { jira_scene(a, n, TB_JIRA_OK, 12345, -1, 22 * 60); }
static void s_jira_loading(tb_app_t *a, tb_clock_t *n) { jira_scene(a, n, TB_JIRA_LOADING, 0, -1, 0); }
static void s_jira_kept(tb_app_t *a, tb_clock_t *n) { jira_scene(a, n, TB_JIRA_UNREACHABLE, 12, 20, 12 * 60); }
static void s_jira_kept_over(tb_app_t *a, tb_clock_t *n) { jira_scene(a, n, TB_JIRA_UNREACHABLE, 12, 10, 12 * 60); }
static void s_jira_dropped(tb_app_t *a, tb_clock_t *n) { jira_scene(a, n, TB_JIRA_UNREACHABLE, 12, 20, 3 * 3600); }
static void s_jira_token(tb_app_t *a, tb_clock_t *n) { jira_scene(a, n, TB_JIRA_TOKEN, 0, 10, 0); }
static void s_jira_nofilter(tb_app_t *a, tb_clock_t *n) { jira_scene(a, n, TB_JIRA_NOFILTER, 0, 10, 0); }
static void s_jira_dark_hold(tb_app_t *a, tb_clock_t *n)
{
    jira_scene(a, n, TB_JIRA_OK, 12, 20, 22 * 60);
    hold_menu(a, n);
}
static void s_menu_quick_offline(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_AVAILABLE);
    a->wifi_mode = TB_WIFI_OFFLINE;
    a->wifi_link_up = false;
    hold_menu(a, n);
}
static void s_menu_quick_nocal(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_AVAILABLE);
    hold_menu(a, n);
}
static void s_menu_quick_syncing(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_AVAILABLE);
    calendar(a, n, false, SAMPLE, 3);
    tb_app_set_calendar(a, true, true, REF - 120, n);
    hold_menu(a, n);
}
/* Firmware only: the office Wi-Fi dropped after setup (the calendar can't sync; the bar follows its saved copy). */
static void s_menu_quick_linkdown(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_AVAILABLE);
    calendar(a, n, false, SAMPLE, 3);
    tb_app_wifi_link(a, false, NULL, NULL, n);
    quiet_toast(a);
    hold_menu(a, n);
}
static void s_menu_quick_showagain(tb_app_t *a, tb_clock_t *n)
{
    s_aside_call(a, n);
    hold_menu(a, n);
}
static void s_menu_timer(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_POMODORO);
    pomo(a, TB_PH_FOCUS, 2, 18 * 60 + 42, true);
    hold_menu(a, n);
}
static void s_menu_timer_ready(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_POMODORO);
    hold_menu(a, n);
}
static void s_menu_timer_showagain(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_POMODORO);
    pomo(a, TB_PH_FOCUS, 2, 18 * 60 + 42, true);
    call(a, n, "Slack");
    tb_app_remote_aside(a, true, n);
    quiet_toast(a);
    hold_menu(a, n);
}
static void s_menu_timer_settings(tb_app_t *a, tb_clock_t *n)
{
    s_menu_timer(a, n);
    tap_action(a, n, TB_ACT_TIMER_SETTINGS);
}
/* The mock-up starts paired: a Mac over USB, this phone and a script (decisions.md, Pairing). */
static void paired3(tb_app_t *a)
{
    tb_app_set_paired(a, 3, "iPhone, Desk script, Mac");
}
static void s_menu_wifi(tb_app_t *a, tb_clock_t *n)
{
    s_menu_quick(a, n);
    paired3(a);
    tap_action(a, n, TB_ACT_WIFI);
}
static void s_menu_wifi_none(tb_app_t *a, tb_clock_t *n)
{
    s_menu_quick(a, n);
    tap_action(a, n, TB_ACT_WIFI);
}
/* Nothing paired, after a name clash: "minibar-2.local" (93 px) doesn't fit the tile, so the foot gives the IP. */
static void s_menu_wifi_none_renamed(tb_app_t *a, tb_clock_t *n)
{
    s_menu_quick(a, n);
    tb_app_wifi_link(a, true, "10.0.4.42", "minibar-2.local", n);
    tap_action(a, n, TB_ACT_WIFI);
}
/* ...and an IP address too long as well: "pair at its" over "IP address", pointing at the Network tile. */
static void s_menu_wifi_none_longip(tb_app_t *a, tb_clock_t *n)
{
    s_menu_quick(a, n);
    tb_app_wifi_link(a, true, "192.168.100.200", "minibar-2.local", n);
    tap_action(a, n, TB_ACT_WIFI);
}
/* Ten devices, the most a bar keeps: "Full", and Forget all names them on two lines at most. */
static const char TEN[] = "iPhone, Mac, Desk script, iPad, Android phone, Windows browser, Hallway sign, Shortcuts, "
                          "Chromebook, Kitchen iPad";
static void s_menu_wifi_full(tb_app_t *a, tb_clock_t *n)
{
    s_menu_quick(a, n);
    tb_app_set_paired(a, 10, TEN);
    tap_action(a, n, TB_ACT_WIFI);
}
static void s_menu_forget_full(tb_app_t *a, tb_clock_t *n)
{
    s_menu_wifi_full(a, n);
    tap_action(a, n, TB_ACT_DEVICES);
}
static void s_menu_wifi_offline(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_AVAILABLE);
    a->wifi_mode = TB_WIFI_OFFLINE;
    a->wifi_link_up = false;
    tb_app_pointer(a, true, 200, 80, TB_TILE_NONE, n);
    run(a, n, 700);
    tb_app_pointer(a, false, 200, 80, TB_TILE_NONE, n);
    run(a, n, 50);
    tap_action(a, n, TB_ACT_WIFI);
}
static void s_menu_forget(tb_app_t *a, tb_clock_t *n)
{
    s_menu_wifi(a, n);
    tap_action(a, n, TB_ACT_DEVICES);
}
static void s_menu_power(tb_app_t *a, tb_clock_t *n)
{
    s_menu_quick(a, n);
    tap_action(a, n, TB_ACT_POWER);
}
static void s_menu_setup(tb_app_t *a, tb_clock_t *n)
{
    s_setup_qr(a, n);
    hold_menu(a, n);
}
static void s_toast_status(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_AVAILABLE);
    calendar(a, n, false, SAMPLE, 3);
    tb_app_notify(a, "Meeting set aside \xC2\xB7 hold to show it again", n);
}
static void s_toast_clock(tb_app_t *a, tb_clock_t *n)
{
    s_clock(a, n);
    tb_app_notify(a, "Ready", n);
}
static void s_toast_setup(tb_app_t *a, tb_clock_t *n)
{
    s_setup_qr(a, n);
    tb_app_pointer(a, true, 300, 80, TB_TILE_NONE, n);
    run(a, n, 60);
    tb_app_pointer(a, false, 300, 80, TB_TILE_NONE, n);     /* "Scan the code with your phone" */
    run(a, n, 50);
}
static void s_toast_menu(tb_app_t *a, tb_clock_t *n)
{
    s_menu_timer_settings(a, n);
    tap_action(a, n, TB_ACT_AUTO);      /* "Auto-start on" over the settings */
}
static void s_toast_title(tb_app_t *a, tb_clock_t *n)
{
    static const mt_t M[] = {{-1, 45, "Quarterly planning with the platform team", "", false}};
    base(a, n, TB_ST_AVAILABLE);
    a->set.automatic.calendar = true;
    a->set.automatic.meeting_titles = true;
    tb_app_set_calendar(a, true, false, REF - 120, n);
    quiet_toast(a);
    tb_meeting_t m = {.id = 9, .start = REF + M[0].start_min * 60, .end = REF + 44 * 60};
    tb_strlcpy(m.title, M[0].title, sizeof m.title);
    tb_app_set_meetings(a, &m, 1, n);  /* "Calendar: Quarterly planning wi… started" */
}
static void s_hold_keep(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_BUSY);
    tb_app_button(a, TB_BTN_PWR_DOWN, n);
    run(a, n, 1700);
}
static void s_hold_off(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_BUSY);
    tb_app_button(a, TB_BTN_PWR_DOWN, n);
    run(a, n, 3100);
}
static void s_flash(tb_app_t *a, tb_clock_t *n)
{
    s_pomo_break_time(a, n);
    a->ringing = true;
    a->flash_at = n->mono - 120;
    a->rev++;
}
static void s_splash(tb_app_t *a, tb_clock_t *n)
{
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    *n = (tb_clock_t){.mono = 60 * MIN, .wall = REF, .valid = true};
    tb_app_init(a, &s, TB_WIFI_OK, n);
}
static void pairing_up(tb_app_t *a, tb_clock_t *n, const char *who, tb_pair_kind_t kind)
{
    base(a, n, TB_ST_BUSY);
    tb_app_pairing_show(a, "482913", who, kind, n);
    a->pairing.shown_at -= 18 * 1000;       /* the code went up 18 s ago: 1:42 left */
    a->pairing.expires -= 18 * 1000;
    a->rev++;
}
static void s_pairing(tb_app_t *a, tb_clock_t *n) { pairing_up(a, n, NULL, TB_PAIR_KIND_MAC); }
static void s_pairing_phone(tb_app_t *a, tb_clock_t *n) { pairing_up(a, n, NULL, TB_PAIR_KIND_PHONE); }
static void s_pairing_named(tb_app_t *a, tb_clock_t *n) { pairing_up(a, n, "Alex's MacBook Air", TB_PAIR_KIND_MAC); }
/* A script (kind automation, no name): "PAIRING · SCRIPT" and the sub line for any other device. */
static void s_pairing_script(tb_app_t *a, tb_clock_t *n) { pairing_up(a, n, NULL, TB_PAIR_KIND_SCRIPT); }
/* The code with 0:05 left: the progress bar nearly full. */
static void s_pairing_late(tb_app_t *a, tb_clock_t *n)
{
    pairing_up(a, n, NULL, TB_PAIR_KIND_PHONE);
    a->pairing.shown_at -= 97 * 1000;      /* 1:42 - 1:37 = 0:05 left */
    a->pairing.expires -= 97 * 1000;
    a->rev++;
}
/* How a pairing ends, as the bar says it: a tap cancels it ("Pairing canceled"); the right code ("Paired · iPhone");
 * a flip over a ready Pomodoro cancels it and starts focus ("Pairing canceled · Focus started"). */
static void s_toast_pair_canceled(tb_app_t *a, tb_clock_t *n)
{
    pairing_up(a, n, NULL, TB_PAIR_KIND_PHONE);
    tb_app_pointer(a, true, 300, 80, TB_TILE_NONE, n);
    run(a, n, 60);
    tb_app_pointer(a, false, 300, 80, TB_TILE_NONE, n);
    run(a, n, 50);
}
static void s_toast_paired(tb_app_t *a, tb_clock_t *n)
{
    pairing_up(a, n, "iPhone", TB_PAIR_KIND_PHONE);
    tb_app_pairing_end(a, TB_PAIR_END_PAIRED, NULL, n);
    run(a, n, 50);
}
static void s_toast_pair_flip(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_POMODORO);
    tb_app_pairing_show(a, "482913", NULL, TB_PAIR_KIND_MAC, n);
    run(a, n, 50);
    tb_app_flip(a, true, false, n);     /* a real flip: cancel, then turn, silence, start focus */
    tb_app_flip(a, false, true, n);     /* the layout only, back: the scene is drawn the right way up, as the mock-up's */
    run(a, n, 50);
}
/* Forget all, confirmed with a deliberate second tap: "Forgot 3 devices", the menu closed. */
static void s_toast_forgot(tb_app_t *a, tb_clock_t *n)
{
    s_menu_forget(a, n);
    run(a, n, TB_FORGET_GUARD_MS);
    tap_action(a, n, TB_ACT_FORGET_ALL);
    tb_app_set_paired(a, 0, "");        /* net revokes every token and says so (TB_FX_FORGET_DEVICES) */
}
static void s_dark(tb_app_t *a, tb_clock_t *n)
{
    base(a, n, TB_ST_BUSY);
    tb_app_button(a, TB_BTN_PWR_DOWN, n);
    run(a, n, 100);
    tb_app_button(a, TB_BTN_PWR_UP, n);
    run(a, n, 50);
}
static void s_flipped(tb_app_t *a, tb_clock_t *n)
{
    s_toast_status(a, n);
    tb_app_flip(a, true, true, n);
}

const ui_scene_t ui_scenes[] = {
    {"available", "Available, no calendar: how long you've been available", s_available, 0, false},
    {"available_free", "Available with a calendar: Free until the next meeting (titles off)", s_available_free, 0, false},
    {"available_free_titles", "Free until, with meeting titles on", s_available_free_titles, 0, false},
    {"available_rest", "Available, no meetings left today", s_available_rest, 0, false},
    {"busy", "Busy for 12 minutes", s_busy, 0, false},
    {"meeting_manual", "In a meeting picked by hand, no calendar", s_meeting_manual, 0, false},
    {"meeting_manual_next", "In a meeting by hand with the next meeting (titles on)", s_meeting_manual_next, 0, false},
    {"away", "Away without a time (proposed plain screen)", s_away, 0, false},
    {"away_back", "Away, back at 2:30, grabbing lunch", s_away_back, 0, false},
    {"message_short", "A short message: stands still at 62 px", s_message_short, 0, false},
    {"message_long", "A long message, scrolling (3 s into the pass)", s_message_long, 3000, false},
    {"clock", "Clock with Next up (titles off)", s_clock, 0, false},
    {"clock_titles", "Clock with Next up (titles on, place)", s_clock_titles, 0, false},
    {"clock_nothing", "Clock, nothing left today", s_clock_nothing, 0, false},
    {"clock_notsetup", "Clock, calendar not set up", s_clock_notsetup, 0, false},
    {"clock_caloff", "Clock, Calendar meetings turned off", s_clock_caloff, 0, false},
    {"clock_offline", "Clock, Wi-Fi skipped", s_clock_offline, 0, false},
    {"clock_unset", "Clock before the time is known (proposed)", s_clock_unset, 0, false},
    {"pomo_ready", "Pomodoro ready", s_pomo_ready, 0, false},
    {"pomo_focus", "Pomodoro focus 2 of 4 running, tomato ripening", s_pomo_focus, 0, false},
    {"pomo_paused", "Pomodoro paused", s_pomo_paused, 0, false},
    {"pomo_short", "Short break running", s_pomo_short, 0, false},
    {"pomo_long", "Long break running", s_pomo_long, 0, false},
    {"pomo_bigfoot", "Focus running after a long day: 12 done, 5h 10m focused", s_pomo_bigfoot, 0, false},
    {"pomo_paused_long", "A paused long break (the longest kicker)", s_pomo_paused_long, 0, false},
    {"pomo_paused_short", "A paused short break", s_pomo_paused_short, 0, false},
    {"pomo_break_time", "Focus done: Break time", s_pomo_break_time, 0, false},
    {"pomo_back_to_it", "Break over: Back to it", s_pomo_back_to_it, 0, false},
    {"pill_focus", "Busy with the focus timer in the pill", s_pill_focus, 0, false},
    {"pill_paused", "Available with a paused timer", s_pill_paused, 0, false},
    {"pill_done", "Busy with a phase waiting (Done)", s_pill_done, 0, false},
    {"pill_short", "Busy during a short break", s_pill_short, 0, false},
    {"call", "On a call (Slack)", s_call, 0, false},
    {"call_noapp", "On a call, no app name", s_call_noapp, 0, false},
    {"call_longname", "On a call, a long app name cut at 24 characters", s_call_longname, 0, false},
    {"call_paused", "On a call that paused the Pomodoro", s_call_paused, 0, false},
    {"call_meeting", "On a call during a calendar meeting", s_call_meeting, 0, false},
    {"call_meeting_titles", "On a call during a meeting, titles on", s_call_meeting_titles, 0, false},
    {"meeting_cal", "In a meeting from the calendar (titles off)", s_meeting_cal, 0, false},
    {"meeting_cal_titles", "In a meeting from the calendar (titles on, place, next)", s_meeting_cal_titles, 0, false},
    {"meeting_cal_private", "A private meeting: no title", s_meeting_cal_private, 0, false},
    {"meeting_cal_long", "A long meeting title, cut with an ellipsis", s_meeting_cal_long, 0, false},
    {"aside_call", "Busy with a call set aside (headset glyph)", s_aside_call, 0, false},
    {"aside_meeting_pill", "A meeting set aside next to the paused timer", s_aside_meeting_pill, 0, false},
    {"mac_icon", "Busy with a Mac connected", s_mac_icon, 0, false},
    {"setup_qr", "Wi-Fi setup: the QR code", s_setup_qr, 0, false},
    {"setup_connecting", "Wi-Fi setup: Connecting", s_setup_connecting, 0, false},
    {"setup_connected", "Wi-Fi setup: Connected", s_setup_connected, 0, false},
    {"setup_failed", "Wi-Fi setup: Couldn't connect", s_setup_failed, 0, false},
    {"menu_quick", "The quick menu", s_menu_quick, 0, false},
    {"menu_display", "The Display menu (firmware only: Light, Time, Back)", s_menu_display, 0, false},
    {"menu_display_24h", "The Display menu after Time is switched to 24-hr (toast over it)", s_menu_display_24h, 0, false},
    {"clock_24h", "Clock with Time format 24-hour", s_clock_24h, 0, false},
    {"away_back_24h", "Away, Back at 03:40 in 24-hour", s_away_back_24h, 0, false},
    {"jira_ok", "Jira: a count, updated 22 minutes ago (firmware only)", s_jira_ok, 0, false},
    {"jira_one", "Jira: one issue", s_jira_one, 0, false},
    {"jira_zero", "Jira: a calm zero", s_jira_zero, 0, false},
    {"jira_over", "Jira: over the limit (orange)", s_jira_over, 0, false},
    {"jira_big", "Jira: 10k+ at the 62 px step", s_jira_big, 0, false},
    {"jira_loading", "Jira: the first check", s_jira_loading, 0, false},
    {"jira_kept", "Jira: can't reach it, the last count kept (12 min)", s_jira_kept, 0, false},
    {"jira_kept_over", "Jira: can't reach it, kept and over the limit", s_jira_kept_over, 0, false},
    {"jira_dropped", "Jira: can't reach it for 3 hours, the count dropped", s_jira_dropped, 0, false},
    {"jira_token", "Jira: token rejected", s_jira_token, 0, false},
    {"jira_nofilter", "Jira: filter not found", s_jira_nofilter, 0, false},
    {"jira_menu", "Jira: the quick menu over it", s_jira_dark_hold, 0, false},
    {"menu_quick_offline", "The quick menu offline", s_menu_quick_offline, 0, false},
    {"menu_quick_nocal", "The quick menu without a calendar", s_menu_quick_nocal, 0, false},
    {"menu_quick_syncing", "The quick menu while syncing", s_menu_quick_syncing, 0, false},
    {"menu_quick_showagain", "The quick menu with Show again", s_menu_quick_showagain, 0, false},
    {"menu_quick_linkdown", "The quick menu with the Wi-Fi link down (firmware only)", s_menu_quick_linkdown, 0, false},
    {"menu_timer", "The timer menu mid-phase", s_menu_timer, 0, false},
    {"menu_timer_ready", "The timer menu when ready (no +5)", s_menu_timer_ready, 0, false},
    {"menu_timer_showagain", "The timer menu with Show again", s_menu_timer_showagain, 0, false},
    {"menu_timer_settings", "Timer settings", s_menu_timer_settings, 0, false},
    {"menu_wifi", "The Wi-Fi menu, 3 devices paired", s_menu_wifi, 0, false},
    {"menu_wifi_none", "The Wi-Fi menu, nothing paired", s_menu_wifi_none, 0, false},
    {"menu_wifi_none_renamed", "The Wi-Fi menu, nothing paired, after a name clash (pair at the IP address)", s_menu_wifi_none_renamed, 0, false},
    {"menu_wifi_none_longip", "The Wi-Fi menu, nothing paired, host and IP too long (pair at its IP address)", s_menu_wifi_none_longip, 0, false},
    {"menu_wifi_full", "The Wi-Fi menu with 10 devices: Full", s_menu_wifi_full, 0, false},
    {"menu_wifi_offline", "The Wi-Fi menu offline", s_menu_wifi_offline, 0, false},
    {"menu_forget", "Forget all's confirmation", s_menu_forget, 0, false},
    {"menu_forget_full", "Forget all's confirmation with 10 devices (names on two lines)", s_menu_forget_full, 0, false},
    {"menu_power", "The power menu", s_menu_power, 0, false},
    {"menu_setup", "The setup menu", s_menu_setup, 0, false},
    {"toast_status", "A toast over a status (the sub line hides)", s_toast_status, 0, false},
    {"toast_clock", "A toast over the clock", s_toast_clock, 0, false},
    {"toast_setup", "A toast over the setup screen", s_toast_setup, 0, false},
    {"toast_menu", "A toast over a menu", s_toast_menu, 0, false},
    {"toast_title", "A toast with a meeting title cut to fit", s_toast_title, 0, false},
    {"hold_keep", "Keep holding (PWR held 1.7 s)", s_hold_keep, 0, false},
    {"hold_off", "Powering off", s_hold_off, 0, false},
    {"flash", "The alarm's white flash", s_flash, 0, false},
    {"splash", "Starting up", s_splash, 0, false},
    {"pairing", "The pairing screen for a Mac (proposed)", s_pairing, 0, false},
    {"pairing_phone", "The pairing screen for a phone", s_pairing_phone, 0, false},
    {"pairing_named", "The pairing screen for a Mac with a name", s_pairing_named, 0, false},
    {"pairing_script", "The pairing screen for a script (any other device)", s_pairing_script, 0, false},
    {"pairing_late", "The pairing screen with 0:05 left", s_pairing_late, 0, false},
    {"toast_pair_canceled", "Pairing canceled by a tap", s_toast_pair_canceled, 0, false},
    {"toast_paired", "Paired with the right code", s_toast_paired, 0, false},
    {"toast_pair_flip", "A flip cancels pairing and starts focus", s_toast_pair_flip, 0, false},
    {"toast_forgot", "Forget all confirmed: Forgot 3 devices", s_toast_forgot, 0, false},
    {"dark", "The dark screen", s_dark, 0, false},
    {"flipped", "Flipped: a toast over Available, the display turned 180 degrees", s_flipped, 0, true},
};
const int ui_scene_count = (int)(sizeof ui_scenes / sizeof ui_scenes[0]);

const ui_scene_t *ui_scene_find(const char *name)
{
    for (int i = 0; i < ui_scene_count; i++)
        if (!strcmp(ui_scenes[i].name, name)) return &ui_scenes[i];
    return NULL;
}
