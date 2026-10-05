/*
 * ui_view.c: every screen's words as data (see ui_view.h). Owner: ui builder.
 *
 * A port of docs/mockup.html's view(), pomoView(), autoView(), wifiView(), sysRow(), side() and tomatoRow(), with the
 * mock-up's copy word for word. Each function names the mock-up function it ports. Where the firmware has to say
 * something the mock-up never shows (the clock isn't set yet, Away without a time, a message from another day, the
 * pairing screen), the copy is marked "Proposed:".
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tb_fmt.h"
#include "tb_text.h"
#include "ui_view.h"

#define MID_DOT "\xC2\xB7"      /* · */

/* snprintf into a fixed array, never cutting a UTF-8 character in half. */
#define PUT(dst, ...) put((dst), sizeof(dst), __VA_ARGS__)
static void put(char *dst, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void put(char *dst, size_t cap, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    tb_strlcpy(dst, tmp, cap);
}

/* ---------- time helpers (the mock-up's fmt, fmtShort, ampm, hm, minsUp) ---------- */

typedef struct {
    char s[16];
} tstr;

static tstr fmt(tb_epoch_t t)
{
    tstr r;
    tb_fmt_time(r.s, sizeof r.s, t);
    return r;
}

static tstr fmt_short(tb_epoch_t t)
{
    tstr r;
    tb_fmt_time_short(r.s, sizeof r.s, t);
    return r;
}

static tstr hm(int32_t mins)
{
    tstr r;
    tb_fmt_hm(r.s, sizeof r.s, mins);
    return r;
}

static tstr mmss(int32_t s)
{
    tstr r;
    tb_fmt_mmss(r.s, sizeof r.s, s);
    return r;
}

/* Math.round(ms / 60000), for the screens that round to the nearest minute (Busy, Away). */
static int32_t mins_round(tb_ms_t ms)
{
    if (ms < 0) ms = 0;
    return (int32_t)((ms + 30000) / 60000);
}

static tb_ms_t since_ms(const tb_clock_t *now, tb_ms_t then)
{
    return now->mono > then ? now->mono - then : 0;
}

/* "since 1:52 PM", or "" while the time it started isn't known. */
static void since_text(char *dst, size_t cap, const char *pre, tb_epoch_t t, const tb_clock_t *now)
{
    if (t && now->valid) {
        tstr f = fmt(t);
        snprintf(dst, cap, "%s%s", pre, f.s);
    } else {
        dst[0] = '\0';
    }
}

/* ---------- side(), sysRow() ---------- */

static void side(ui_view_t *v, const char *label, const char *value, const char *ampm, const char *foot, bool small)
{
    v->side = true;
    PUT(v->label, "%s", label);
    PUT(v->value, "%s", value);
    PUT(v->value_ampm, "%s", ampm ? ampm : "");
    PUT(v->foot, "%s", foot ? foot : "");
    v->value_small = small;
}

/* sysRow(): the time, the Pomodoro pill off the Pomodoro screen, and the icons. The bar runs on USB (Bold Signal's
 * power 'usb'): no battery; a Mac icon while a Mac is connected. Next to the pill there's room for two icons: Wi-Fi
 * and the set-aside glyph, or else the Mac. */
static void sys_row(ui_view_t *v, const tb_app_t *a, const tb_clock_t *now, bool on_pomodoro)
{
    const tb_pomo_t *p = &a->pomo;
    v->pill = !on_pomodoro && tb_pomo_active(p, &a->set);
    if (v->pill) {
        if (p->waiting) PUT(v->pill_text, "Done");
        else if (!p->running) PUT(v->pill_text, "Paused");
        else PUT(v->pill_text, "%s", mmss(tb_pomo_remaining_s(p)).s);
        v->pill_phase = p->phase;
    }
    /* Proposed: no time in the corner while the clock is unknown. */
    if (now->valid) PUT(v->sys_time, "%s", v->pill ? fmt_short(now->wall).s : fmt(now->wall).s);
    else v->sys_time[0] = '\0';

    tb_auto_t aside = tb_app_aside_kind(a, now);
    v->n_icons = 0;
    if (aside == TB_AUTO_CALL) v->icons[v->n_icons++] = UI_ICON_HEADSET;
    else if (aside == TB_AUTO_MEETING) v->icons[v->n_icons++] = UI_ICON_CALENDAR;
    if (a->mac_link != TB_LINK_NONE && !(v->pill && aside != TB_AUTO_NONE)) v->icons[v->n_icons++] = UI_ICON_MAC;
    /* Wi-Fi crossed out when it was skipped; proposed (ARCHITECTURE.md 13.6): also while the link is down. */
    bool off = a->wifi_mode == TB_WIFI_OFFLINE || (a->wifi_mode == TB_WIFI_OK && !a->wifi_link_up);
    v->icons[v->n_icons++] = off ? UI_ICON_WIFI_OFF : UI_ICON_WIFI;
}

static bool same_day(tb_epoch_t a, tb_epoch_t b)
{
    return tb_local_yyyymmdd(a) == tb_local_yyyymmdd(b);
}

/* nextLine(fallback) in view(): "Next: Design review at 2:30 PM", "Next meeting at 2:30 PM". */
static void next_line(char *dst, size_t cap, const tb_app_t *a, const tb_meeting_t *nx, const char *fallback)
{
    if (!nx) {
        snprintf(dst, cap, "%s", fallback);
        return;
    }
    const char *t = tb_app_title_of(a, nx);
    tstr at = fmt(nx->start);
    if (t[0]) snprintf(dst, cap, "Next: %s at %s", t, at.s);
    else snprintf(dst, cap, "Next meeting at %s", at.s);
}

static void status_main(ui_view_t *v, const char *kicker, const char *head, ui_fit_t fit, const char *sub)
{
    v->layout = UI_LAYOUT_STATUS;
    PUT(v->kicker, "%s", kicker);
    PUT(v->head, "%s", head);
    v->fit = fit;
    v->head_caps = true;
    PUT(v->sub, "%s", sub);
}

/* ---------- pomoView() ---------- */

static void pomo_view(ui_view_t *v, const tb_app_t *a, const tb_clock_t *now)
{
    const tb_pomo_t *p = &a->pomo;
    const tb_settings_t *s = &a->set;
    int every = s->pomodoro.long_every;
    sys_row(v, a, now, true);
    v->side = true;
    PUT(v->label, "Today");
    v->tomatoes = true;
    v->n_tomatoes = (uint8_t)tb_pomo_tomatoes(p, s, v->tomato);
    PUT(v->foot, "%u done " MID_DOT " %s focused", (unsigned)p->done_today, hm((int32_t)((p->focused_ms + 30000) / 60000)).s);

    if (p->waiting) {
        v->layout = UI_LAYOUT_ALARM;
        v->fit = UI_FIT_62;
        v->head_caps = true;
        if (p->just_ended == TB_PH_FOCUS) {
            int len = p->phase == TB_PH_LONG ? s->pomodoro.long_min : s->pomodoro.short_min;
            PUT(v->kicker, "Focus %d of %d done", p->round, every);
            PUT(v->head, "Break time");
            PUT(v->sub, "Flip or tap to start a %d min %s break", len, p->phase == TB_PH_LONG ? "long" : "short");
        } else {
            PUT(v->kicker, "Break over");
            PUT(v->head, "Back to it");
            PUT(v->sub, "Flip or tap to start focus %d of %d", p->round, every);
        }
        return;
    }

    bool ready = tb_pomo_is_ready(p, s);
    bool paused = !p->running;
    int next_round = p->phase == TB_PH_LONG ? 1 : p->round + 1;
    char kicker[96];
    if (p->phase == TB_PH_FOCUS) snprintf(kicker, sizeof kicker, "Pomodoro " MID_DOT " focus %d of %d", p->round, every);
    else snprintf(kicker, sizeof kicker, "%s " MID_DOT " then focus %d of %d", tb_phase_name(p->phase), next_round, every);
    if (ready) PUT(v->kicker, "Pomodoro " MID_DOT " ready");
    else if (paused) PUT(v->kicker, "Paused " MID_DOT " %s", kicker);
    else PUT(v->kicker, "%s", kicker);

    tstr end = fmt(now->wall + p->remaining_ms / 1000);    /* simEnd() */
    if (ready) PUT(v->sub, "Flip or tap to start %d min of focus", s->pomodoro.focus_min);
    else if (paused) PUT(v->sub, "Flip or tap to resume");
    /* Proposed: while the clock is unknown the end time is left out. */
    else if (p->phase == TB_PH_FOCUS && now->valid) PUT(v->sub, "Please don't interrupt " MID_DOT " break at %s", end.s);
    else if (p->phase == TB_PH_FOCUS) PUT(v->sub, "Please don't interrupt");
    else if (now->valid) PUT(v->sub, "Free to chat " MID_DOT " back to focus at %s", end.s);
    else PUT(v->sub, "Free to chat");

    v->layout = UI_LAYOUT_STATUS;
    PUT(v->head, "%s", mmss(tb_pomo_remaining_s(p)).s);
    v->fit = UI_FIT_TIME;
    v->head_caps = true;
    tb_ms_t total = tb_pomo_phase_len(s, p->phase);
    tb_ms_t done = total - p->remaining_ms;
    if (done < 0) done = 0;
    v->bar_permille = total > 0 ? (int16_t)(done * 1000 / total) : 0;
    if (v->bar_permille > 1000) v->bar_permille = 1000;
}

/* ---------- view() ---------- */

static void own_view(ui_view_t *v, const tb_app_t *a, const tb_clock_t *now)
{
    bool cd = tb_app_cal_data(a);
    const tb_meeting_t *nx = cd ? tb_app_next_meeting(a, now) : NULL;
    char foot[160], line[200];
    tb_ms_t gone = since_ms(now, a->since_ms);

    switch (a->idx) {
    case TB_ST_AVAILABLE:
        sys_row(v, a, now, false);
        status_main(v, "Status", "Available", UI_FIT_WORD, "Happy to chat");
        /* Free until comes from the calendar; without one, the side says how long you've been available. */
        if (!cd) {
            since_text(foot, sizeof foot, "since ", a->since, now);
            side(v, "Available for", hm(tb_mins_up(gone)).s, NULL, foot, false);
        } else if (nx) {
            const char *t = tb_app_title_of(a, nx);
            snprintf(foot, sizeof foot, "then %s", t[0] ? t : "a meeting");
            side(v, "Free until", fmt_short(nx->start).s, tb_fmt_ampm(nx->start), foot, false);
        } else if (!now->valid) {
            /* Proposed: meetings can't be placed until the clock is set. */
            side(v, "Free until", "Not known", NULL, "the clock isn't set yet", true);
        } else {
            side(v, "Free", "Rest of day", NULL, "no meetings left", true);
        }
        return;
    case TB_ST_BUSY:
        sys_row(v, a, now, false);
        status_main(v, "Status", "Busy", UI_FIT_WORD, "Heads down, message me instead");
        since_text(foot, sizeof foot, "since ", a->since, now);
        side(v, "Busy for", hm(mins_round(gone)).s, NULL, foot, false);
        return;
    case TB_ST_MEETING:
        /* In a meeting set by hand: no title or end time is known, so it counts up from when you picked it. */
        sys_row(v, a, now, false);
        next_line(line, sizeof line, a, nx, "Message me if it's urgent");
        status_main(v, "Status", "In a meeting", UI_FIT_WORD, line);
        since_text(foot, sizeof foot, "since ", a->since, now);
        side(v, "Meeting for", hm(tb_mins_up(gone)).s, NULL, foot, false);
        return;
    case TB_ST_POMODORO:
        pomo_view(v, a, now);
        return;
    case TB_ST_AWAY: {
        sys_row(v, a, now, false);
        /* The mock-up shows "Back at 12:30" and "Grabbing lunch" as sample data; api.md 8.1 (proposed) sets them.
         * Proposed: without a time the headline is the plain status, under the "Status" kicker like the others. */
        const char *note = a->away_note[0] ? a->away_note : "Not at my desk";
        int hh, mm;
        if (a->away_back_at[0] && sscanf(a->away_back_at, "%d:%d", &hh, &mm) == 2) {
            char head[32];
            snprintf(head, sizeof head, "Back at %d:%02d", hh % 12 == 0 ? 12 : hh % 12, mm);
            status_main(v, "Away", head, UI_FIT_WORD, note);
        } else {
            status_main(v, "Status", "Away", UI_FIT_WORD, note);
        }
        since_text(foot, sizeof foot, "left at ", a->since, now);
        side(v, "Gone for", hm(mins_round(gone)).s, NULL, foot, false);
        return;
    }
    case TB_ST_MESSAGE: {
        sys_row(v, a, now, false);
        const char *text = a->message[0] ? a->message : "Hello";
        size_t n = tb_utf8_len(text);
        v->layout = UI_LAYOUT_MESSAGE;
        PUT(v->kicker, "Message");
        PUT(v->head, "%s", text);
        v->fit = UI_FIT_MARQUEE;
        v->head_caps = false;
        v->marquee_long = n > 22;
        v->marquee_ms = n * 320 > 6000 ? (uint32_t)(n * 320) : 6000;
        PUT(v->sub, "Set from the Remote");
        if (a->message_at) {
            /* "today" in the mock-up. Proposed: a message kept from another day says which. */
            if (!now->valid) foot[0] = '\0';
            else if (same_day(a->message_at, now->wall)) snprintf(foot, sizeof foot, "today");
            else if (same_day(a->message_at, now->wall - 86400)) snprintf(foot, sizeof foot, "yesterday");
            else {
                time_t tt = (time_t)a->message_at;
                struct tm tm;
                localtime_r(&tt, &tm);
                strftime(foot, sizeof foot, "%b ", &tm);
                snprintf(foot + strlen(foot), sizeof foot - strlen(foot), "%d", tm.tm_mday);
            }
            side(v, "Posted", fmt_short(a->message_at).s, tb_fmt_ampm(a->message_at), foot, false);
        } else {
            side(v, "Posted", "Earlier", NULL, "", true);   /* Proposed: posted while the clock was unknown */
        }
        return;
    }
    case TB_ST_CLOCK:
    default: {
        sys_row(v, a, now, false);
        char sub[96];
        snprintf(sub, sizeof sub, "Idle " MID_DOT " %u Pomodoro%s done today", (unsigned)a->pomo.done_today,
                 a->pomo.done_today == 1 ? "" : "s");
        if (now->valid) {
            char date[48];
            tb_fmt_date_long(date, sizeof date, now->wall);
            status_main(v, date, fmt_short(now->wall).s, UI_FIT_TIME, sub);
            PUT(v->head_ampm, "%s", tb_fmt_ampm(now->wall));
        } else {
            /* Proposed (ARCHITECTURE.md 13.5): the clock's look before the time is known. */
            status_main(v, "Clock not set", "--:--", UI_FIT_TIME, sub);
        }
        if (a->wifi_mode == TB_WIFI_OFFLINE) side(v, "Calendar", "Off", NULL, "needs Wi-Fi", true);
        else if (!a->cal_saved) side(v, "Calendar", "Not set up", NULL, "add it on the Remote", true);
        else if (!a->set.automatic.calendar) side(v, "Calendar", "Off", NULL, "turned off on the Remote", true);
        /* Next up reads like Free until and Posted: the start time is the side value with AM or PM after the digits,
         * then the title and place (titles on) or how soon it starts. */
        else if (nx) {
            const char *t = tb_app_title_of(a, nx), *place = tb_app_place_of(a, nx);
            if (t[0]) snprintf(foot, sizeof foot, "%s%s%s", t, place[0] ? " " MID_DOT " " : "", place);
            else snprintf(foot, sizeof foot, "meeting in %s", hm(tb_mins_up((nx->start - now->wall) * 1000)).s);
            side(v, "Next up", fmt_short(nx->start).s, tb_fmt_ampm(nx->start), foot, false);
        } else if (!now->valid) {
            side(v, "Next up", "Not known", NULL, "the clock isn't set yet", true);   /* Proposed */
        } else {
            side(v, "Next up", "Nothing", NULL, "rest of today is free", true);
        }
        return;
    }
    }
}

/* ---------- autoView() ---------- */

static int16_t progress(const tb_meeting_t *m, const tb_clock_t *now)
{
    if (!m || m->end <= m->start) return -1;
    int64_t p = (now->wall - m->start) * 1000 / (m->end - m->start);
    return (int16_t)(p < 0 ? 0 : p > 1000 ? 1000 : p);
}

static void auto_view(ui_view_t *v, const tb_app_t *a, const tb_clock_t *now, tb_auto_t kind)
{
    const tb_meeting_t *m = tb_app_meet_now(a, now);
    char foot[160];
    sys_row(v, a, now, false);
    v->layout = UI_LAYOUT_STATUS;
    v->head_caps = true;
    v->fit = UI_FIT_WORD;
    if (kind == TB_AUTO_CALL) {
        char app[TB_APP_NAME_BYTES + 4];
        tb_app_call_label(a, app, sizeof app);
        int32_t mins = tb_mins_up(since_ms(now, a->call.since_ms));
        const char *t = tb_app_title_of(a, m);
        v->chip = UI_CHIP_MAC;
        PUT(v->kicker, "%s", app[0] ? app : "Mic or camera on");
        PUT(v->head, "On a call");
        PUT(v->sub, "%s", t[0] ? t : "Please keep voices low nearby");
        if (m) {
            snprintf(foot, sizeof foot, "at %s " MID_DOT " call %s", fmt(m->end).s, hm(mins).s);
            side(v, "Meeting ends in", hm(tb_mins_up((m->end - now->wall) * 1000)).s, NULL, foot, false);
            v->bar_permille = progress(m, now);
        } else {
            since_text(foot, sizeof foot, "since ", a->call.since, now);
            side(v, "On a call for", hm(mins).s, NULL, foot, false);
        }
        return;
    }
    /* In a meeting from the calendar (autoTop() only says so when the meeting and the clock are known). */
    if (!m) return;
    const char *t = tb_app_title_of(a, m), *place = tb_app_place_of(a, m);
    const tb_meeting_t *nx = tb_app_next_meeting(a, now);
    char span[48], next[200];
    tb_fmt_span(span, sizeof span, m->start, m->end);
    v->chip = UI_CHIP_CALENDAR;
    PUT(v->kicker, "%s%s", t[0] ? "In a meeting " MID_DOT " " : "", span);
    if (t[0]) {
        PUT(v->head, "%s", t);
        v->fit = UI_FIT_62;
        v->head_caps = false;
    } else {
        PUT(v->head, "In a meeting");
    }
    next_line(next, sizeof next, a, nx, "No more meetings today");
    PUT(v->sub, "%s%s%s", place, place[0] ? " " MID_DOT " " : "", next);
    snprintf(foot, sizeof foot, "at %s", fmt(m->end).s);
    side(v, "Ends in", hm(tb_mins_up((m->end - now->wall) * 1000)).s, NULL, foot, false);
    v->bar_permille = progress(m, now);
}

/* ---------- wifiView() ---------- */

static void wifi_view(ui_view_t *v, const tb_app_t *a)
{
    v->head_caps = false;
    v->fit = UI_FIT_62;
    switch (a->wifi_mode) {
    case TB_WIFI_SETUP:
        v->layout = UI_LAYOUT_SETUP_QR;
        PUT(v->kicker, "Wi-Fi setup");
        PUT(v->head, "Scan to set up");
        PUT(v->qr_payload, "WIFI:T:nopass;S:MiniBar-Setup;;");
        PUT(v->step1_pre, "Join ");
        PUT(v->step1_bold, "MiniBar-Setup");
        PUT(v->step1_post, " with your phone");
        PUT(v->step2, "Pick your office Wi-Fi on the page that opens");
        PUT(v->sub, "Hold to skip and use without Wi-Fi");      /* the setup screen's foot (.wfoot) */
        return;
    case TB_WIFI_CONNECTING:
        v->layout = UI_LAYOUT_SETUP_TEXT;
        PUT(v->kicker, "Wi-Fi setup");
        PUT(v->head, "Connecting");
        v->head_dots = true;
        PUT(v->sub, "to %s", a->wifi_ssid);
        return;
    case TB_WIFI_CONNECTED:
        v->layout = UI_LAYOUT_SETUP_TEXT;
        /* The bar's own name first, so bars in one office can be told apart (pairing round, proposed). */
        PUT(v->kicker, "%s " MID_DOT " Connected to %s", a->set.device.name, a->wifi_ssid);
        PUT(v->head, "%s", a->wifi_host[0] ? a->wifi_host : "minibar.local");
        PUT(v->sub, "or %s " MID_DOT " open it on your phone for the Remote", a->wifi_ip);
        return;
    default:
        v->layout = UI_LAYOUT_SETUP_TEXT;
        PUT(v->kicker, "Couldn't connect to %s", a->wifi_ssid);
        PUT(v->head, "%s", a->wifi_error[0] ? a->wifi_error : "Couldn't connect");
        PUT(v->sub, "Tap to try again " MID_DOT " Hold to skip");
        return;
    }
}

/* ---------- pairView() (api.md 4.8 and decisions.md "Pairing", proposed) ---------- */

/* The Clock screen's dark surfaces, since it isn't a status. The kicker names who asked, the code is the 112 px
 * headline in tabular digits, and the info column counts the 2 minutes down above this bar's own name, so in an office
 * with several bars you can check it's the one you meant. The progress bar fills as the time runs out. */
static void pairing_view(ui_view_t *v, const tb_app_t *a, const tb_clock_t *now)
{
    sys_row(v, a, now, false);
    v->layout = UI_LAYOUT_STATUS;
    PUT(v->kicker, "Pairing " MID_DOT " %s", a->pairing.who[0] ? a->pairing.who : "Mac");
    const char *c = a->pairing.code;
    if (strlen(c) == 6) PUT(v->head, "%.3s %.3s", c, c + 3);    /* "482 913" */
    else PUT(v->head, "%s", c);
    v->fit = UI_FIT_TIME;
    v->head_caps = true;
    if (a->pairing.kind == TB_PAIR_KIND_MAC) PUT(v->sub, "Type it on your Mac " MID_DOT " tap to cancel");
    else if (a->pairing.kind == TB_PAIR_KIND_PHONE) PUT(v->sub, "Type it on your phone " MID_DOT " tap to cancel");
    else PUT(v->sub, "Type this code on that device " MID_DOT " tap to cancel");
    tb_ms_t left = a->pairing.expires - now->mono;
    if (left < 0) left = 0;
    if (left > TB_PAIR_MS) left = TB_PAIR_MS;
    /* Math.max(1, Math.ceil(left / 1000)): it reads 0:01 to the end */
    int32_t secs = (int32_t)((left + 999) / 1000);
    side(v, "Code expires in", mmss(secs < 1 ? 1 : secs).s, NULL, a->set.device.name, false);
    v->bar_permille = (int16_t)((TB_PAIR_MS - left) * 1000 / TB_PAIR_MS);
}

/* ---------- render() ---------- */

void ui_view_build(const tb_app_t *a, const tb_clock_t *now, ui_view_t *v)
{
    memset(v, 0, sizeof(*v));
    v->bar_permille = -1;
    v->key = tb_app_color_key(a, now);
    if (a->booting) {
        v->layout = UI_LAYOUT_SPLASH;
        v->key = TB_KEY_CLOCK;
        PUT(v->head, "MiniBar");
        v->head_caps = true;
        return;
    }
    if (tb_app_on_wifi_screen(a)) {
        v->key = TB_KEY_SETUP;
        wifi_view(v, a);
        return;
    }
    if (tb_app_pairing_visible(a)) {
        v->key = TB_KEY_CLOCK;      /* a dark surface, like the menus: it isn't a status */
        pairing_view(v, a, now);
        return;
    }
    /* A call or meeting shows over your own status; your status is still there underneath, unchanged. */
    tb_auto_t top = tb_app_auto_top(a, now, NULL);
    if (top != TB_AUTO_NONE) auto_view(v, a, now, top);
    else own_view(v, a, now);
}

/* The CSS ease-out timing function, cubic-bezier(0, 0, .58, 1): the curve's y at x (P1 = (0, 0), P2 = (.58, 1)). */
static double ease_out(double x)
{
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    double lo = 0, hi = 1, t = x;
    for (int i = 0; i < 40; i++) {     /* x(t) = 3(1-t)t^2 * .58 + t^3 is increasing: bisect */
        t = (lo + hi) / 2;
        double xt = 3 * (1 - t) * t * t * .58 + t * t * t;
        if (xt < x) lo = t;
        else hi = t;
    }
    return 3 * (1 - t) * t * t + t * t * t;   /* y: control points 0, 0, 1, 1 */
}

uint8_t ui_flash_opa(tb_ms_t ms)
{
    const tb_ms_t pulse = 550;
    if (ms < 0 || ms >= 3 * pulse) return 0;
    double x = (double)(ms % pulse) / pulse;
    double opa = .7 * (1 - ease_out(x));
    return (uint8_t)(opa * 255 + .5);
}

void ui_overlay_build(const tb_app_t *a, const tb_clock_t *now, ui_overlay_t *o)
{
    memset(o, 0, sizeof(*o));
    o->dark = a->off && !a->powering_off;
    o->menu = a->menu.kind != TB_MENU_NONE && !a->off;
    if (a->toast[0] && !a->off) {
        o->toast = true;
        tb_strlcpy(o->toast_text, a->toast, sizeof o->toast_text);
        o->toast_title_off = a->toast_title_off;
        o->toast_title_len = a->toast_title_len;
        /* Centered on the status field while the info column shows and no menu is open, else on the screen. */
        o->toast_on_field = !a->booting && !tb_app_on_wifi_screen(a) && !o->menu;
    }
    if (a->hold != TB_HOLD_NONE) {
        bool off = a->hold == TB_HOLD_POWERING_OFF;
        o->hold = true;
        PUT(o->hold_title, "%s", off ? "Powering off" : "Keep holding");
        PUT(o->hold_sub, "%s", off ? "Press PWR to turn it back on" : "to power off");
        /* The track fills in 2.6 s from when the screen appears (400 ms in), so it's full at 3 s. */
        tb_ms_t t = now->mono - a->hold_since;
        o->hold_permille = off ? 1000 : t <= 0 ? 0 : t >= 2600 ? 1000 : (uint16_t)(t * 1000 / 2600);
    }
    if (a->flash_at && !a->off) {
        tb_ms_t t = now->mono - a->flash_at;
        if (t >= 0 && t < TB_FLASH_MS) {
            o->flash = true;
            o->flash_opa = ui_flash_opa(t);
        }
    }
}

uint64_t ui_view_key(const tb_app_t *a, const tb_clock_t *now)
{
    uint64_t h = 1469598103934665603ull;
    int64_t parts[] = {
        a->rev,
        now->valid,
        now->valid ? now->wall / 60 : 0,          /* the shown minute */
        now->mono / 60000,                        /* durations count from the monotonic clock */
        tb_pomo_remaining_s(&a->pomo),            /* the timer's second (and the pill's) */
        a->pairing.active ? (a->pairing.expires - now->mono + 999) / 1000 : 0,
    };
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) {
        h ^= (uint64_t)parts[i];
        h *= 1099511628211ull;
    }
    return h;
}

/* ---------- capitals ---------- */

void ui_text_upper(char *dst, size_t cap, const char *src)
{
    size_t o = 0;
    if (!cap) return;
    for (const unsigned char *s = (const unsigned char *)src; *s;) {
        char buf[4];
        size_t n;
        if (*s >= 'a' && *s <= 'z') {
            buf[0] = (char)(*s - 32);
            n = 1;
            s++;
        } else if (s[0] == 0xC3 && s[1] >= 0x80 && s[1] <= 0xBF) {
            /* U+00C0..U+00FF */
            unsigned cp = 0xC0 + (s[1] - 0x80);
            if (cp == 0xDF) {               /* ß -> SS */
                buf[0] = buf[1] = 'S';
                n = 2;
            } else {
                if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) cp -= 0x20;
                buf[0] = (char)0xC3;
                buf[1] = (char)(0x80 + (cp - 0xC0));
                n = 2;
            }
            s += 2;
        } else {
            /* Anything else is copied as a whole UTF-8 sequence. */
            n = *s < 0x80 ? 1 : (*s & 0xE0) == 0xC0 ? 2 : (*s & 0xF0) == 0xE0 ? 3 : 4;
            size_t k = 0;
            for (; k < n && s[k]; k++) buf[k] = (char)s[k];
            n = k;
            s += n;
        }
        if (o + n >= cap) break;
        memcpy(dst + o, buf, n);
        o += n;
    }
    dst[o] = '\0';
}
