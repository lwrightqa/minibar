/*
 * tb_app.c: the device state machine. Owner: core builder.
 *
 * A port of docs/mockup.html's script, function by function, in the mock-up's order of operations and with its copy.
 * Each function says which mock-up function it ports. Places where the firmware has to differ are marked "Firmware:"
 * (see the list at the top of tb_app.h). The mock-up's say() log lines have no device equivalent and are left out.
 *
 * Every public entry point ends with settle(), which turns state into effects the mock-up got from the DOM for
 * free: the ticking level, the backlight, saving settings and state, and the rev that redraws the screen and is the
 * API's ETag.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "tb_app.h"
#include "tb_fmt.h"
#include "tb_internal.h"
#include "tb_text.h"

static const char *const STATE_NAME[TB_ST_COUNT] = {"Available", "Busy", "In a meeting", "Pomodoro", "Away", "Message", "Clock"};
#define POMO TB_ST_POMODORO

const char *tb_status_name(tb_status_t st)
{
    return st < TB_ST_COUNT ? STATE_NAME[st] : "";
}

/* ======================================================================================================== */
/* Small helpers                                                                                            */
/* ======================================================================================================== */

void tb_fx(tb_app_t *a, tb_effect_kind_t kind, int32_t arg)
{
    if (a->n_fx >= TB_EFFECTS_MAX) {
        a->fx_dropped++;
        return;
    }
    a->fx[a->n_fx++] = (tb_effect_t){kind, arg};
}

static tb_epoch_t wall_or_0(const tb_clock_t *now)
{
    return now->valid ? now->wall : 0;
}

/* A monotonic stamp that's never 0 (0 means "none" in the fields that hold one). */
static tb_ms_t stamp(const tb_clock_t *now)
{
    return now->mono ? now->mono : 1;
}

static const char *kind_word(tb_auto_t k)
{
    return k == TB_AUTO_CALL ? "call" : "meeting";
}

static const char *kind_cap(tb_auto_t k)     /* cap(k) */
{
    return k == TB_AUTO_CALL ? "Call" : "Meeting";
}

/* FNV-1a, for the signatures in settle(). */
typedef struct { uint32_t h; } fnv_t;
static void h_bytes(fnv_t *s, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) {
        s->h ^= b[i];
        s->h *= 16777619u;
    }
}
static void h_i64(fnv_t *s, int64_t v) { h_bytes(s, &v, sizeof v); }
static void h_str(fnv_t *s, const char *str) { h_bytes(s, str, strlen(str) + 1); }

/* ======================================================================================================== */
/* Queries: callNow, meetNow, autoTop, asideKind, quiet, todays, currentEvent, nextEvent                    */
/* ======================================================================================================== */

bool tb_app_on_wifi_screen(const tb_app_t *a)
{
    return a->wifi_mode == TB_WIFI_SETUP || a->wifi_mode == TB_WIFI_CONNECTING || a->wifi_mode == TB_WIFI_CONNECTED ||
           a->wifi_mode == TB_WIFI_FAILED;
}

bool tb_app_cal_data(const tb_app_t *a)
{
    return a->cal_saved && a->set.automatic.calendar && a->wifi_mode != TB_WIFI_OFFLINE;
}

bool tb_app_call_now(const tb_app_t *a)
{
    return a->call.active && a->set.automatic.mac;
}

/* currentEvent(): the meeting in progress; of two that overlap, the one that started last. */
const tb_meeting_t *tb_app_current_meeting(const tb_app_t *a, const tb_clock_t *now)
{
    if (!now->valid) return NULL;   /* Firmware: without a clock no meeting can be in progress */
    const tb_meeting_t *best = NULL;
    for (int i = 0; i < a->n_meetings; i++) {
        const tb_meeting_t *e = &a->meetings[i];
        if (e->start <= now->wall && now->wall < e->end && (!best || e->start > best->start)) best = e;
    }
    return best;
}

const tb_meeting_t *tb_app_meet_now(const tb_app_t *a, const tb_clock_t *now)
{
    return tb_app_cal_data(a) ? tb_app_current_meeting(a, now) : NULL;
}

/* todays(): meetings not over yet that start today (local date). */
static bool is_today(const tb_meeting_t *e, const tb_clock_t *now, tb_epoch_t mid, tb_epoch_t next_mid)
{
    return e->end > now->wall && e->start >= mid && e->start < next_mid;
}

static void today_bounds(const tb_clock_t *now, tb_epoch_t *mid, tb_epoch_t *next_mid)
{
    *mid = tb_local_midnight(now->wall);
    *next_mid = tb_local_midnight(*mid + 26 * 3600);    /* a day is 23 to 25 hours: 26 h lands in the next one */
}

const tb_meeting_t *tb_app_next_meeting(const tb_app_t *a, const tb_clock_t *now)
{
    if (!now->valid || !a->n_meetings) return NULL;
    tb_epoch_t mid, next_mid;
    today_bounds(now, &mid, &next_mid);
    const tb_meeting_t *best = NULL;
    for (int i = 0; i < a->n_meetings; i++) {
        const tb_meeting_t *e = &a->meetings[i];
        if (is_today(e, now, mid, next_mid) && e->start > now->wall && (!best || e->start < best->start)) best = e;
    }
    return best;
}

int tb_app_meetings_left(const tb_app_t *a, const tb_clock_t *now)
{
    if (!now->valid || !a->n_meetings) return 0;
    tb_epoch_t mid, next_mid;
    today_bounds(now, &mid, &next_mid);
    int k = 0;
    for (int i = 0; i < a->n_meetings; i++)
        if (is_today(&a->meetings[i], now, mid, next_mid)) k++;
    return k;
}

char *tb_app_left_text(const tb_app_t *a, const tb_clock_t *now, char *buf, size_t cap)
{
    int k = tb_app_meetings_left(a, now);
    if (k) snprintf(buf, cap, "%d meeting%s left today", k, k == 1 ? "" : "s");
    else snprintf(buf, cap, "no more meetings today");
    return buf;
}

const char *tb_app_title_of(const tb_app_t *a, const tb_meeting_t *m)
{
    return m && a->set.automatic.meeting_titles && !m->priv ? m->title : "";
}

static bool starts_with_ci(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != *prefix) return false;
    }
    return true;
}

/* placeOf(): !/^https?:/i.test(e.loc) */
const char *tb_app_place_of(const tb_app_t *a, const tb_meeting_t *m)
{
    if (!m || !a->set.automatic.meeting_titles || m->priv || !m->location[0]) return "";
    if (starts_with_ci(m->location, "http:") || starts_with_ci(m->location, "https:")) return "";
    return m->location;
}

const char *tb_app_call_label(const tb_app_t *a, char *buf, size_t cap)
{
    if (!cap) return buf;
    buf[0] = '\0';
    if (!a->call.active || !a->call.app[0]) return buf;
    uint32_t bad[1];
    if (tb_text_unsupported(a->call.app, bad, 1)) return buf;
    tb_strlcpy(buf, a->call.app, cap);
    tb_text_ellipsize(buf, cap, TB_APP_SHOW_CHARS);
    return buf;
}

/* autoTop(): On a call, then In a meeting from the calendar, unless set aside (the next one always takes over). */
tb_auto_t tb_app_auto_top(const tb_app_t *a, const tb_clock_t *now, uint32_t *id)
{
    if (id) *id = 0;
    if (tb_app_call_now(a) && a->aside_call != a->call.id) {
        if (id) *id = a->call.id;
        return TB_AUTO_CALL;
    }
    const tb_meeting_t *m = tb_app_meet_now(a, now);
    if (m && a->aside_meeting != m->id) {
        if (id) *id = m->id;
        return TB_AUTO_MEETING;
    }
    return TB_AUTO_NONE;
}

tb_auto_t tb_app_aside_kind(const tb_app_t *a, const tb_clock_t *now)
{
    if (tb_app_call_now(a) && a->aside_call == a->call.id) return TB_AUTO_CALL;
    const tb_meeting_t *m = tb_app_meet_now(a, now);
    if (m && a->aside_meeting == m->id) return TB_AUTO_MEETING;
    return TB_AUTO_NONE;
}

tb_showing_t tb_app_showing(const tb_app_t *a, const tb_clock_t *now)
{
    if (tb_app_on_wifi_screen(a)) return TB_SHOWING_SETUP;
    tb_auto_t k = tb_app_auto_top(a, now, NULL);
    return k == TB_AUTO_CALL ? TB_SHOWING_CALL : k == TB_AUTO_MEETING ? TB_SHOWING_MEETING : TB_SHOWING_OWN;
}

/* quiet(): no sound while a call is on or a calendar meeting is in progress, even one you set aside. */
bool tb_app_quiet(const tb_app_t *a, const tb_clock_t *now)
{
    return tb_app_call_now(a) || tb_app_meet_now(a, now) != NULL;
}

/* ticking(): pomo.tick && s.powered && !s.booting && !s.off && pomo.running && pomo.phase === 'focus' && !s.ringing
 * && !quiet(). Firmware: also silent while "Powering off" shows. */
bool tb_app_ticking(const tb_app_t *a, const tb_clock_t *now)
{
    return a->set.pomodoro.ticking && !a->powered_off && !a->powering_off && !a->booting && !a->off &&
           a->pomo.running && a->pomo.phase == TB_PH_FOCUS && !a->ringing && !tb_app_quiet(a, now);
}

tb_auto_t tb_app_paused_by(const tb_app_t *a, const tb_clock_t *now)
{
    if (tb_pomo_state(&a->pomo, &a->set) != TB_POMO_ST_PAUSED) return TB_AUTO_NONE;
    return a->pomo.auto_paused != TB_AUTO_NONE && tb_app_quiet(a, now) ? a->pomo.auto_paused : TB_AUTO_NONE;
}

bool tb_app_pairing_visible(const tb_app_t *a)
{
    return a->pairing.active && !a->powered_off && !a->booting && !a->powering_off && a->hold == TB_HOLD_NONE &&
           !a->off && !tb_app_on_wifi_screen(a);
}

bool tb_app_pairing_shown(const tb_app_t *a)
{
    return a->pairing.active && a->pairing.shown_at != 0;
}

/* screenFree(): s.powered && !s.booting && !s.off && menu.hidden && !onWifiScreen() && holdOv.hidden, plus the
 * pairing screen. */
bool tb_app_screen_free(const tb_app_t *a)
{
    return !a->powered_off && !a->booting && !a->off && a->menu.kind == TB_MENU_NONE && !tb_app_on_wifi_screen(a) &&
           a->hold == TB_HOLD_NONE && !a->powering_off && !a->pairing.active;
}

tb_color_key_t tb_app_color_key(const tb_app_t *a, const tb_clock_t *now)
{
    if (a->booting) return TB_KEY_CLOCK;
    if (tb_app_on_wifi_screen(a)) return TB_KEY_SETUP;
    tb_auto_t k = tb_app_auto_top(a, now, NULL);
    if (k == TB_AUTO_CALL) return TB_KEY_CALL;
    if (k == TB_AUTO_MEETING) return TB_KEY_MEETING;
    switch (a->idx) {
    case TB_ST_AVAILABLE: return TB_KEY_AVAILABLE;
    case TB_ST_BUSY: return TB_KEY_BUSY;
    case TB_ST_MEETING: return TB_KEY_MEETING;
    case TB_ST_POMODORO:
        return a->pomo.phase == TB_PH_FOCUS ? TB_KEY_FOCUS : a->pomo.phase == TB_PH_SHORT ? TB_KEY_SHORT : TB_KEY_LONG;
    case TB_ST_AWAY: return TB_KEY_AWAY;
    case TB_ST_MESSAGE: return TB_KEY_MESSAGE;
    default: return TB_KEY_CLOCK;
    }
}

/* ======================================================================================================== */
/* Toasts: toast(), notify(), withTitle()                                                                   */
/* ======================================================================================================== */

static void set_pending(tb_app_t *a, const char *text, size_t toff, size_t tlen)
{
    tb_strlcpy(a->pending_toast, text, sizeof(a->pending_toast));
    a->pending_title_off = (uint8_t)toff;
    a->pending_title_len = (uint8_t)tlen;
}

/* toast(text, hold): a dark screen stays dark, so the confirmation waits until the screen is woken, unless something
 * is already waiting. Powered off, there's nothing to show it on. While a pairing code shows, a change made underneath
 * is shown once pairing ends, after the pairing toast; that one (hold) stays its full 1.6 s and anything that comes in
 * meanwhile follows it. */
static void toast_span(tb_app_t *a, const char *text, size_t toff, size_t tlen, bool hold, const tb_clock_t *now)
{
    if (a->powered_off) return;
    if (a->off) {
        if (!a->pending_toast[0]) set_pending(a, text, toff, tlen);
        return;
    }
    if (tb_app_pairing_shown(a) || (!hold && now->mono < a->toast_hold_until)) {
        set_pending(a, text, toff, tlen);
        return;
    }
    tb_strlcpy(a->toast, text, sizeof(a->toast));
    a->toast_title_off = (uint8_t)toff;
    a->toast_title_len = (uint8_t)tlen;
    a->toast_until = now->mono + TB_TOAST_MS;
    a->toast_hold_until = hold ? a->toast_until : 0;
    tb_bump(a);
}

static void toast(tb_app_t *a, const char *text, const tb_clock_t *now)
{
    toast_span(a, text, 0, 0, false, now);
}

static void toast_hold(tb_app_t *a, const char *text, const tb_clock_t *now)
{
    toast_span(a, text, 0, 0, true, now);
}

static void toastf(tb_app_t *a, const tb_clock_t *now, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void toastf(tb_app_t *a, const tb_clock_t *now, const char *fmt, ...)
{
    char buf[TB_TOAST_BYTES];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    toast(a, buf, now);
}

/* notify(): toasts for automatic changes wait until the screen is free (and replace one already waiting). */
static void notify_span(tb_app_t *a, const char *text, size_t toff, size_t tlen, const tb_clock_t *now)
{
    if (tb_app_screen_free(a)) {
        toast_span(a, text, toff, tlen, false, now);
        return;
    }
    if (a->powered_off) return;
    set_pending(a, text, toff, tlen);
}

static void notifyf(tb_app_t *a, const tb_clock_t *now, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void notifyf(tb_app_t *a, const tb_clock_t *now, const char *fmt, ...)
{
    char buf[TB_TOAST_BYTES];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    notify_span(a, buf, 0, 0, now);
}

/*
 * withTitle(make, t): a meeting title in a toast is cut to 24 characters: t.slice(0, n - 1).trimEnd() + '…'.
 * The ui may cut it further by measured width (toast_title_off/len say where it is). pre + title + post.
 * real_title: t is a meeting's title, not the "meeting" fallback (a span is only recorded for a real one).
 */
static void with_title(tb_app_t *a, bool via_notify, const char *pre, const char *t, bool real_title, const char *post,
                       const tb_clock_t *now)
{
    char cut[TB_TITLE_TOAST_CHARS * 4 + 4];
    tb_strlcpy(cut, t, sizeof cut);
    if (tb_utf8_len(t) > TB_TITLE_TOAST_CHARS) {
        /* the first 23 characters, trailing spaces trimmed, then the ellipsis */
        size_t i = 0, c = 0;
        while (t[i] && c < TB_TITLE_TOAST_CHARS - 1) {
            i++;
            while (((unsigned char)t[i] & 0xC0) == 0x80) i++;
            c++;
        }
        while (i > 0 && t[i - 1] == ' ') i--;
        if (i > sizeof cut - 4) i = sizeof cut - 4;
        memcpy(cut, t, i);
        memcpy(cut + i, "\xE2\x80\xA6", 4);
    }
    char buf[TB_TOAST_BYTES];
    int n = snprintf(buf, sizeof buf, "%s%s%s", pre, cut, post);
    size_t off = strlen(pre), len = strlen(cut);
    if (n < 0 || (size_t)n >= sizeof buf || !real_title) off = len = 0;
    if (via_notify) notify_span(a, buf, off, len, now);
    else toast_span(a, buf, off, len, false, now);
}

/* ======================================================================================================== */
/* Screen and power basics: togglePower, wake, hideMenu, silence, flash, chime                              */
/* ======================================================================================================== */

static void hide_menu(tb_app_t *a)
{
    if (a->menu.kind == TB_MENU_NONE) return;
    memset(&a->menu, 0, sizeof(a->menu));
    tb_bump(a);
}

/* togglePower(): a PWR press turns the screen off or on. Going dark hides the toast and the menu. */
static void toggle_power(tb_app_t *a)
{
    a->off = !a->off;
    if (a->off) a->toast[0] = '\0';
    hide_menu(a);
    tb_bump(a);
}

/* wake(): a dark screen wakes; returns whether it was dark. */
static bool wake(tb_app_t *a)
{
    if (!a->off) return false;
    toggle_power(a);
    return true;
}

/* silence(): returns whether the alarm was ringing. */
static bool silence(tb_app_t *a)
{
    bool was = a->ringing;
    a->ringing = false;
    return was;
}

static void flash(tb_app_t *a, const tb_clock_t *now)
{
    a->flash_at = stamp(now);
    tb_bump(a);
}

/* chime(toBreak): if (!pomo.sound || !ac || quiet()) return; */
static void chime(tb_app_t *a, bool to_break, const tb_clock_t *now)
{
    if (!a->set.pomodoro.chime || tb_app_quiet(a, now)) return;
    tb_fx(a, TB_FX_CHIME, to_break ? 1 : 0);
}

/* ======================================================================================================== */
/* Automatic status: setAside, takeover, releaseHeldAlarm, syncAuto, backToOwn, showAgain                   */
/* ======================================================================================================== */

/* setAside(): set aside whatever call or meeting is on screen (both, if both are on). Returns its kind, or NONE. */
static tb_auto_t set_aside(tb_app_t *a, const tb_clock_t *now)
{
    tb_auto_t top = tb_app_auto_top(a, now, NULL);
    if (top == TB_AUTO_NONE) return TB_AUTO_NONE;
    const tb_meeting_t *m = tb_app_meet_now(a, now);
    if (tb_app_call_now(a)) a->aside_call = a->call.id;
    if (m) a->aside_meeting = m->id;
    a->shown_kind = TB_AUTO_NONE;
    a->shown_id = 0;
    tb_bump(a);
    return top;
}

/* takeover(): a call or meeting taking over pauses a running Pomodoro and holds a ringing alarm (it rings once when
 * the call or meeting is over). */
static void takeover(tb_app_t *a, tb_auto_t top)
{
    if (top == TB_AUTO_NONE) return;
    if (a->pomo.running) {
        a->pomo.running = false;
        a->pomo.auto_paused = top;
    }
    if (a->ringing) {
        a->ringing = false;
        a->pomo.held_alarm = top;
    }
}

/* releaseHeldAlarm(): once the call or meeting that held the alarm is over, the waiting screen chimes and flashes
 * once (no repeats). A dark screen stays dark. Proposed (decisions.md, Pairing): an alarm held by a pairing code rings
 * once when pairing ends; if a call or meeting took over meanwhile it waits for that instead, and one that's set aside
 * only mutes the chime (chime() checks), so it just flashes. */
static void release_held_alarm(tb_app_t *a, const tb_clock_t *now)
{
    if (a->powered_off || a->booting) return;
    if (a->alarm_held_by_pairing) {
        if (tb_app_pairing_shown(a)) return;
        a->alarm_held_by_pairing = false;
        tb_auto_t top = tb_app_auto_top(a, now, NULL);
        if (top != TB_AUTO_NONE && a->pomo.waiting) {
            a->pomo.held_alarm = top;
            return;
        }
    } else {
        if (a->pomo.held_alarm == TB_AUTO_NONE || tb_app_quiet(a, now)) return;
        a->pomo.held_alarm = TB_AUTO_NONE;
    }
    if (!a->pomo.waiting) return;
    a->idx = POMO;
    tb_bump(a);
    chime(a, a->pomo.phase != TB_PH_FOCUS, now);
    flash(a, now);
}

/*
 * syncAuto(lead): compare what should show with what is showing, and announce the change once. lead names what
 * happened ("Call ended", "Calendar removed"...) when the caller knows. Returns true when the screen changed.
 */
static bool sync_auto(tb_app_t *a, const char *lead, const tb_clock_t *now)
{
    if (a->powered_off || a->booting) return false;
    uint32_t id;
    tb_auto_t top = tb_app_auto_top(a, now, &id);
    tb_auto_t prev = a->shown_kind;
    if (top == prev && (top == TB_AUTO_NONE || id == a->shown_id)) return false;
    a->shown_kind = top;
    a->shown_id = id;
    a->pending_toast[0] = '\0';
    takeover(a, top);
    if (top == TB_AUTO_CALL) {
        if (lead && prev == TB_AUTO_NONE) notifyf(a, now, "%s \xC2\xB7 On a call", lead);
        else notifyf(a, now, "Mac: call started");
    } else if (top == TB_AUTO_MEETING) {
        const char *t = tb_app_title_of(a, tb_app_meet_now(a, now));
        if (prev == TB_AUTO_CALL && !tb_app_call_now(a)) {
            char pre[64];
            snprintf(pre, sizeof pre, "%s \xC2\xB7 still in ", lead ? lead : "Call ended");
            with_title(a, true, pre, t[0] ? t : "a meeting", t[0] != 0, "", now);
        } else if (lead) {
            notifyf(a, now, "%s \xC2\xB7 In a meeting", lead);
        } else {
            with_title(a, true, "Calendar: ", t[0] ? t : "meeting", t[0] != 0, " started", now);
        }
    } else {
        bool held = a->pomo.held_alarm != TB_AUTO_NONE && a->pomo.waiting && !tb_app_quiet(a, now);
        const char *what = lead ? lead : prev == TB_AUTO_CALL ? "Call ended" : "Meeting ended";
        if (held) notifyf(a, now, "%s", what);
        else notifyf(a, now, "%s \xC2\xB7 back to %s", what, STATE_NAME[a->idx]);
    }
    tb_bump(a);
    release_held_alarm(a, now);
    return true;
}

/* backToOwn(): a tap or BOOT on an automatic screen goes back to your own status. The device has no hint line, so
 * the toast says how to undo it. */
static void back_to_own(tb_app_t *a, const tb_clock_t *now)
{
    silence(a);
    tb_auto_t k = set_aside(a, now);
    hide_menu(a);
    if (k != TB_AUTO_NONE) toastf(a, now, "%s set aside \xC2\xB7 hold to show it again", kind_cap(k));
}

/* showAgain(): the quick menu's Show again tile and the Remote's Show again button. */
static bool show_again(tb_app_t *a, const tb_clock_t *now)
{
    tb_auto_t k = tb_app_aside_kind(a, now);
    hide_menu(a);
    if (k == TB_AUTO_NONE) {
        toast(a, "Nothing set aside", now);
        return false;
    }
    a->aside_call = a->aside_meeting = 0;
    uint32_t id;
    a->shown_kind = tb_app_auto_top(a, now, &id);
    a->shown_id = id;
    takeover(a, a->shown_kind);
    tb_bump(a);
    toastf(a, now, "Showing the %s again", kind_word(k));
    return true;
}

/* ======================================================================================================== */
/* The Pomodoro: endPhase, startPause, skip, stop, setTicking                                               */
/* ======================================================================================================== */

/* endPhase(): when a phase ends without auto-start, the alarm repeats until it is answered, for at most a minute.
 * During a call or calendar meeting it flashes without the chime. */
static void end_phase(tb_app_t *a, const tb_clock_t *now)
{
    tb_phase_t ended = tb_pomo_next_phase(&a->pomo, &a->set, false);
    tb_auto_t top = tb_app_auto_top(a, now, NULL);
    if (top != TB_AUTO_NONE) {
        /* A call or meeting pauses the timer when it takes over, so this shouldn't happen; if it does, the alarm
         * waits until the call or meeting ends. */
        a->pomo.running = false;
        a->pomo.waiting = true;
        a->pomo.just_ended = (int8_t)ended;
        a->pomo.held_alarm = top;
        tb_bump(a);
        return;
    }
    /* Proposed (decisions.md, Pairing): while a pairing code shows, the alarm waits too, with no chime or flash, and
     * rings once when pairing ends (as after a call). With auto-start on, the next phase starts on time under the
     * code, silently. */
    bool pairing = tb_app_pairing_shown(a);
    if (pairing && !a->set.pomodoro.auto_start) {
        a->pomo.running = false;
        a->pomo.waiting = true;
        a->pomo.just_ended = (int8_t)ended;
        a->alarm_held_by_pairing = true;
        tb_bump(a);
        return;
    }
    if (!pairing) chime(a, ended == TB_PH_FOCUS, now);
    if (a->set.pomodoro.auto_start) {
        a->pomo.running = true;
        a->pomo.waiting = false;
        a->pomo.just_ended = -1;
    } else {
        a->pomo.running = false;
        a->pomo.waiting = true;
        a->pomo.just_ended = (int8_t)ended;
        a->ringing = true;
        a->ring_until = now->mono + TB_ALARM_LIMIT_MS;
        a->next_ring = now->mono + TB_ALARM_REPEAT_MS;
    }
    wake(a);
    a->idx = POMO;
    hide_menu(a);
    if (!pairing) flash(a, now);
    tb_bump(a);
}

static void toast_started_span(tb_app_t *a, tb_pomo_start_t started, const char *before, bool hold, const tb_clock_t *now)
{
    char buf[TB_TOAST_BYTES];
    snprintf(buf, sizeof buf, "%s%s %s", before, tb_phase_name(a->pomo.phase), started == TB_POMO_STARTED ? "started" : "resumed");
    toast_span(a, buf, 0, 0, hold, now);
}

static void toast_started(tb_app_t *a, tb_pomo_start_t started, const char *before, const tb_clock_t *now)
{
    toast_started_span(a, started, before, false, now);
}

/* startPause(): start what's waiting, a fresh session or a paused timer; or pause a running one. */
static void start_pause(tb_app_t *a, bool aside, const tb_clock_t *now)
{
    silence(a);
    if (aside) set_aside(a, now);
    tb_pomo_start_t started = tb_pomo_start_waiting(&a->pomo, &a->set);
    if (started != TB_POMO_NOTHING) toast_started(a, started, "", now);
    else {
        a->pomo.running = false;
        toast(a, "Paused", now);
    }
    tb_bump(a);
}

/* skip(): the current phase ends without counting, and the next one starts running. */
static void skip(tb_app_t *a, bool aside, const tb_clock_t *now)
{
    silence(a);
    if (aside) set_aside(a, now);
    tb_pomo_next_phase(&a->pomo, &a->set, true);
    a->pomo.running = true;
    a->pomo.waiting = false;
    a->pomo.just_ended = -1;
    a->pomo.auto_paused = TB_AUTO_NONE;
    tb_bump(a);
}

/* stop(): ends the run (today's tomatoes stay) and goes back to the status you had before. */
static void stop(tb_app_t *a, bool aside, const tb_clock_t *now)
{
    silence(a);
    if (aside) set_aside(a, now);
    tb_pomo_reset_run(&a->pomo, &a->set);
    a->idx = a->last_status;
    toast(a, "Pomodoro stopped", now);
    tb_bump(a);
}

static const char *vol_name(tb_tick_vol_t v)
{
    return v == TB_TICK_MEDIUM ? "Medium" : "Soft";
}

/* setTicking(on, vol): has_vol false = vol left out (null). The volume choice is kept while ticking is off. */
static void set_ticking(tb_app_t *a, bool on, bool has_vol, tb_tick_vol_t vol, const tb_clock_t *now)
{
    bool vol_only = on == a->set.pomodoro.ticking && has_vol && vol != a->set.pomodoro.tick_volume;
    a->set.pomodoro.ticking = on;
    if (has_vol) a->set.pomodoro.tick_volume = vol;
    if (vol_only && !on) {
        toastf(a, now, "Ticking volume \xC2\xB7 %s (ticking is off)", vol_name(a->set.pomodoro.tick_volume));
        return;
    }
    if (on) toastf(a, now, "Ticking on \xC2\xB7 %s", vol_name(a->set.pomodoro.tick_volume));
    else toast(a, "Ticking off", now);
}

/* ======================================================================================================== */
/* Own status: go()                                                                                         */
/* ======================================================================================================== */

/* go(i): changing status by hand. During a call or meeting this also sets it aside (your choice wins until it ends);
 * the Remote can ask it not to (api.md 8, set_aside false). */
static void go(tb_app_t *a, int i, bool aside_it, const tb_clock_t *now)
{
    if (tb_app_on_wifi_screen(a)) {
        toast(a, "Finish setup, or hold to skip", now);
        return;
    }
    silence(a);
    tb_auto_t aside = aside_it ? set_aside(a, now) : TB_AUTO_NONE;
    a->idx = (tb_status_t)((i + TB_ST_COUNT) % TB_ST_COUNT);
    if (a->idx != POMO && a->idx != TB_ST_CLOCK) a->last_status = a->idx;
    /* Firmware: Message before any message was set shows the mock-up's "Hello"; it's stored, so the API and the
     * Remote report what the bar shows (and POST /status "message" works). */
    if (a->idx == TB_ST_MESSAGE && !a->message[0]) {
        tb_strlcpy(a->message, TB_MESSAGE_FALLBACK, sizeof(a->message));
        a->message_at = wall_or_0(now);
    }
    a->since_ms = now->mono;
    a->since = wall_or_0(now);
    hide_menu(a);
    if (aside != TB_AUTO_NONE) toastf(a, now, "%s set aside \xC2\xB7 %s", kind_cap(aside), STATE_NAME[a->idx]);
    tb_bump(a);
}

/* ======================================================================================================== */
/* Wi-Fi setup: startSetup, skipWifi, wifiTap, the Connected screen                                         */
/* ======================================================================================================== */

static void wifi_done(tb_app_t *a)
{
    a->wifi_mode = TB_WIFI_OK;
    tb_fx(a, TB_FX_WIFI_DONE, 0);
    tb_bump(a);
}

static void end_pairing_here(tb_app_t *a, int how, const tb_clock_t *now);
#define PE_CANCEL 0     /* tap, swipe, hold, BOOT: "Pairing canceled" */
#define PE_PWR    1     /* PWR press: no toast (the dark screen is the confirmation); a held alarm's chime is dropped */
#define PE_FLIP   2     /* the flip says it itself, and answers a held alarm by starting the next phase */
#define PE_SETUP  3     /* Wi-Fi setup started: "Pairing canceled" */
#define PE_OFF    4     /* powering off or restarting: no toast, a held alarm is forgotten */

static void start_setup(tb_app_t *a, const tb_clock_t *now)
{
    /* Proposed (decisions.md, Pairing): starting Wi-Fi setup ends a pairing, which counts as canceled. */
    end_pairing_here(a, PE_SETUP, now);
    a->wifi_mode = TB_WIFI_SETUP;
    a->wifi_error[0] = '\0';
    hide_menu(a);
    tb_fx(a, TB_FX_WIFI_SETUP, 0);
    tb_bump(a);
}

static void skip_wifi(tb_app_t *a, const tb_clock_t *now)
{
    a->wifi_mode = TB_WIFI_OFFLINE;
    a->wifi_link_up = false;
    hide_menu(a);
    tb_fx(a, TB_FX_WIFI_SKIP, 0);
    tb_bump(a);
    /* Without Wi-Fi the calendar is off, so a meeting on screen ends here. */
    if (!sync_auto(a, "Offline, calendar off", now)) toast(a, "Offline \xC2\xB7 statuses still work", now);
}

/* wifiTap(): on the setup screens a tap moves things along, and never leaves you stuck. */
static void wifi_tap(tb_app_t *a, const tb_clock_t *now)
{
    silence(a);
    switch (a->wifi_mode) {
    case TB_WIFI_FAILED:
        a->wifi_mode = TB_WIFI_SETUP;   /* back to the QR code (the setup network is still up) */
        tb_bump(a);
        break;
    case TB_WIFI_CONNECTED: wifi_done(a); break;
    case TB_WIFI_CONNECTING: toast(a, "Still connecting", now); break;
    default: toast(a, "Scan the code with your phone", now); break;
    }
}

/* ======================================================================================================== */
/* Menus: showMenu, menuAction                                                                              */
/* ======================================================================================================== */

/* showMenu(): the quick menu; on the Pomodoro screen the timer menu instead (but on a call or meeting screen it's
 * always the quick menu); on the setup screens the setup options. Opening a menu hides the toast. */
static void show_menu(tb_app_t *a, const tb_clock_t *now)
{
    /* Connected is the end of setup: holding there finishes it and opens the usual menu, so Skip can't undo it. */
    if (a->wifi_mode == TB_WIFI_CONNECTED) wifi_done(a);
    tb_menu_kind_t kind;
    if (tb_app_on_wifi_screen(a)) kind = TB_MENU_SETUP;
    else if (tb_app_auto_top(a, now, NULL) == TB_AUTO_NONE && a->idx == POMO) kind = TB_MENU_TIMER;
    else kind = TB_MENU_QUICK;
    a->menu.kind = TB_MENU_NONE;        /* a fresh menu: no Devices confirmation carried over */
    tb_menu_build(a, kind, now);
    a->toast[0] = '\0';
    tb_bump(a);
}

static void open_submenu(tb_app_t *a, tb_menu_kind_t kind, const tb_clock_t *now)
{
    tb_menu_build(a, kind, now);
    tb_bump(a);
}

static void power_off(tb_app_t *a, const tb_clock_t *now);
static void restart(tb_app_t *a, const tb_clock_t *now);

/* The Light tile: 40, 70, 100, then 40 again; from any other value, the next level above it. */
static uint8_t next_brightness(uint8_t cur)
{
    static const uint8_t levels[] = TB_BRIGHT_LEVELS;
    for (size_t i = 0; i < sizeof levels; i++)
        if (levels[i] > cur) return levels[i];
    return levels[0];
}

static void menu_action(tb_app_t *a, tb_action_t act, const tb_clock_t *now)
{
    switch (act) {
    case TB_ACT_BRIGHT:
        a->set.display.brightness = next_brightness(a->set.display.brightness);
        show_menu(a, now);
        return;
    case TB_ACT_POWER: open_submenu(a, TB_MENU_POWER, now); return;
    case TB_ACT_WIFI: open_submenu(a, TB_MENU_WIFI, now); return;
    case TB_ACT_WIFI_SETUP: start_setup(a, now); return;
    case TB_ACT_WIFI_SKIP: skip_wifi(a, now); return;
    case TB_ACT_TIMER_SETTINGS: open_submenu(a, TB_MENU_TIMER_SETTINGS, now); return;
    case TB_ACT_TIMER_MENU: show_menu(a, now); return;
    case TB_ACT_AUTO:
        a->set.pomodoro.auto_start = !a->set.pomodoro.auto_start;
        open_submenu(a, TB_MENU_TIMER_SETTINGS, now);
        toastf(a, now, "Auto-start %s", a->set.pomodoro.auto_start ? "on" : "off");
        return;
    case TB_ACT_TICK: {
        /* Off, Soft, Medium, Off... */
        int cur = !a->set.pomodoro.ticking ? 0 : a->set.pomodoro.tick_volume == TB_TICK_MEDIUM ? 2 : 1;
        int nxt = (cur + 1) % 3;
        if (nxt == 0) set_ticking(a, false, false, TB_TICK_SOFT, now);
        else set_ticking(a, true, true, nxt == 2 ? TB_TICK_MEDIUM : TB_TICK_SOFT, now);
        open_submenu(a, TB_MENU_TIMER_SETTINGS, now);
        return;
    }
    case TB_ACT_SHOW_AGAIN: show_again(a, now); return;
    case TB_ACT_DEVICES:
        /* Proposed (api.md 4.8, decisions.md "the Devices tile"): Devices opens Forget all's confirmation. */
        if (a->paired_count) open_submenu(a, TB_MENU_FORGET, now);
        else hide_menu(a);
        return;
    case TB_ACT_KEEP_DEVICES: open_submenu(a, TB_MENU_WIFI, now); return;
    case TB_ACT_FORGET_ALL: {
        /* Forget all sits right under Devices, so taps in its first 600 ms are ignored: a quick double tap on Devices
         * can't forget everything. It still takes a second, deliberate tap. */
        if (now->mono - a->menu.opened_at < TB_FORGET_GUARD_MS) return;
        unsigned n = a->paired_count;
        hide_menu(a);
        tb_fx(a, TB_FX_FORGET_DEVICES, 0);
        toastf(a, now, "Forgot %u device%s", n, n == 1 ? "" : "s");
        return;
    }
    default: break;
    }
    hide_menu(a);
    switch (act) {
    case TB_ACT_RESTART: restart(a, now); break;
    case TB_ACT_POWER_OFF: power_off(a, now); break;
    case TB_ACT_SYNC:
        /* Firmware: "No Wi-Fi" also covers a link that dropped after setup (the mock-up only knows skipped). */
        if (a->wifi_mode == TB_WIFI_OFFLINE || !a->wifi_link_up) toast(a, "No Wi-Fi, can't sync", now);
        else if (!a->cal_saved) toast(a, "Add your calendar on the Remote", now);
        else if (a->cal_checking) toast(a, "Still syncing", now);
        else {
            /* Firmware: the fetch takes a few seconds; "Calendar synced" comes with TB_CALEV_SYNCED. */
            a->cal_checking = true;
            tb_fx(a, TB_FX_CAL_SYNC, 0);
            tb_bump(a);
        }
        break;
    case TB_ACT_SKIP: skip(a, true, now); break;
    case TB_ACT_STOP: stop(a, true, now); break;
    case TB_ACT_ADD:
        if (tb_pomo_mid_phase(&a->pomo, &a->set)) {
            a->pomo.remaining_ms += (tb_ms_t)TB_ADD_MIN * 60000;
            toast(a, "+5 min", now);
            tb_bump(a);
        }
        break;
    default: break;    /* Close, Done, Back, and a read-only tile: the menu just closes */
    }
}

/* ======================================================================================================== */
/* Power: powerOff, powerOn, restart, PWR                                                                   */
/* ======================================================================================================== */

/* powerOff(): "Powering off" for 1.2 s, then the power goes (tick() finishes it). */
static void power_off(tb_app_t *a, const tb_clock_t *now)
{
    silence(a);
    hide_menu(a);
    /* Powering off ends a pairing; paired devices stay paired (the back-off lives in RAM and goes with it). */
    end_pairing_here(a, PE_OFF, now);
    a->pwr_down_at = 0;
    a->hold = TB_HOLD_POWERING_OFF;
    a->powering_off = true;
    a->hold_since = now->mono;
    tb_bump(a);
}

/* powerOn(): the splash for 1.5 s; tick() runs its ending (boot_done). */
static void power_on(tb_app_t *a, const tb_clock_t *now)
{
    a->booting = true;
    a->boot_until = now->mono + TB_BOOT_SPLASH_MS;
    tb_bump(a);
}

/* restart(): a running Pomodoro run ends (today's tomatoes stay), then the bar starts up again. On the device
 * TB_FX_RESTART reboots it; core also plays the splash, as the mock-up does, for the host tests. */
static void restart(tb_app_t *a, const tb_clock_t *now)
{
    silence(a);
    hide_menu(a);
    end_pairing_here(a, PE_OFF, now);
    tb_pomo_reset_run(&a->pomo, &a->set);
    tb_fx(a, TB_FX_RESTART, 0);
    power_on(a, now);
}

/* The end of powerOn(): a Pomodoro status falls back to the last status (the run reset), half-done setup screens go
 * back to the QR code, set-aside is forgotten, and a call or meeting that's still on shows at once. */
static void boot_done(tb_app_t *a, const tb_clock_t *now)
{
    a->booting = false;
    if (a->idx == POMO) a->idx = a->last_status;
    if (a->wifi_mode == TB_WIFI_CONNECTING || a->wifi_mode == TB_WIFI_FAILED) a->wifi_mode = TB_WIFI_SETUP;
    if (a->wifi_mode == TB_WIFI_CONNECTED) a->wifi_mode = TB_WIFI_OK;
    a->aside_call = a->aside_meeting = 0;
    uint32_t id;
    a->shown_kind = tb_app_auto_top(a, now, &id);
    a->shown_id = id;
    tb_bump(a);
    if (a->shown_kind == TB_AUTO_CALL) toast(a, "Ready \xC2\xB7 On a call", now);
    else if (a->shown_kind == TB_AUTO_MEETING) toast(a, "Ready \xC2\xB7 In a meeting", now);
    else toast(a, "Ready", now);
}

/* pairEnd() for the endings core decides (net's own endings come through tb_app_pairing_end). Every one tells net
 * (TB_FX_PAIRING_CANCELED), which counts it as a failed pairing for the back-off. The screen stays on afterwards. */
static void end_pairing_here(tb_app_t *a, int how, const tb_clock_t *now)
{
    if (!a->pairing.active) return;
    bool was_shown = a->pairing.shown_at != 0;
    a->pairing.active = false;
    a->pairing.shown_at = 0;
    tb_fx(a, TB_FX_PAIRING_CANCELED, 0);
    /* A PWR press drops a held alarm's chime: the waiting screen shows, silently, when the screen is woken. */
    if (how == PE_PWR && a->alarm_held_by_pairing) {
        a->alarm_held_by_pairing = false;
        if (a->pomo.waiting) a->idx = POMO;
    }
    if (how == PE_OFF) a->alarm_held_by_pairing = false;
    tb_bump(a);
    if (was_shown && (how == PE_CANCEL || how == PE_SETUP)) toast_hold(a, "Pairing canceled", now);
    if (how != PE_FLIP) release_held_alarm(a, now);
}

static void cancel_pairing(tb_app_t *a, const tb_clock_t *now)
{
    end_pairing_here(a, PE_CANCEL, now);
}

/* pwrShort(): a press answers an alarm, or turns the screen off or on. On a pairing code it cancels it and turns the
 * screen off as usual; the dark screen is the confirmation. */
static void pwr_short(tb_app_t *a, const tb_clock_t *now)
{
    if (tb_app_pairing_visible(a)) {
        end_pairing_here(a, PE_PWR, now);
        toggle_power(a);
        return;
    }
    if (silence(a)) {
        toast(a, "Alarm off", now);
        tb_bump(a);
        return;
    }
    toggle_power(a);
}

/* ======================================================================================================== */
/* Settle: effects and rev derived from the state                                                           */
/* ======================================================================================================== */

static uint32_t state_sig(const tb_app_t *a)
{
    fnv_t s = {2166136261u};
    h_i64(&s, a->idx);
    h_i64(&s, a->last_status);
    h_str(&s, a->message);
    h_i64(&s, a->message_at);
    h_i64(&s, a->pomo.done_today);
    h_i64(&s, a->pomo.day);
    h_i64(&s, a->pomo.running);
    return s.h;
}

static void h_tile(fnv_t *s, const tb_tile_t *t)
{
    h_i64(s, t->action);
    h_i64(s, t->style);
    h_i64(s, t->value_two_lines);
    h_str(s, t->label);
    h_str(s, t->value);
    h_str(s, t->foot);
}

/* Everything the screen or GET /api/v1/status shows, except the countdowns (the timer, focused time) and the clock. */
static uint32_t visible_sig(const tb_app_t *a)
{
    fnv_t s = {2166136261u};
    const tb_settings_t *st = &a->set;
    h_i64(&s, a->idx);
    h_i64(&s, a->last_status);
    h_i64(&s, a->since_ms);
    h_i64(&s, a->since);
    h_str(&s, a->message);
    h_i64(&s, a->message_at);
    h_str(&s, a->away_back_at);
    h_str(&s, a->away_note);
    h_i64(&s, a->flipped | a->off << 1 | a->booting << 2 | a->powering_off << 3 | a->powered_off << 4 |
                  a->ringing << 5 | a->wifi_link_up << 6 | a->cal_saved << 7 | a->cal_checking << 8 |
                  a->pairing.active << 9 | a->alarm_held_by_pairing << 10 | (a->pairing.shown_at != 0) << 11 |
                  a->pairing.kind << 12);
    h_i64(&s, a->hold);
    h_i64(&s, a->flash_at);
    h_i64(&s, a->wifi_mode);
    h_str(&s, a->wifi_ssid);
    h_str(&s, a->wifi_ip);
    h_str(&s, a->wifi_host);
    h_str(&s, a->wifi_error);
    h_i64(&s, a->call.active);
    h_i64(&s, a->call.id);
    h_str(&s, a->call.app);
    h_i64(&s, a->call.via);
    h_i64(&s, a->call.since);
    h_i64(&s, a->call.since_ms);
    h_i64(&s, a->mac_link);
    h_i64(&s, a->n_meetings);
    h_i64(&s, a->cal_last_sync);
    h_i64(&s, a->aside_call);
    h_i64(&s, a->aside_meeting);
    h_i64(&s, a->shown_kind);
    h_i64(&s, a->shown_id);
    const tb_pomo_t *p = &a->pomo;
    h_i64(&s, p->phase | p->round << 4 | p->running << 12 | p->waiting << 13 | (int)((unsigned)(p->just_ended + 1) << 14) |
                  p->skipped_mask << 16 | p->auto_paused << 24 | p->held_alarm << 28);
    h_i64(&s, p->done_today);
    h_i64(&s, p->day);
    h_i64(&s, st->pomodoro.focus_min | st->pomodoro.short_min << 8 | st->pomodoro.long_min << 16 |
                  (int64_t)st->pomodoro.long_every << 24 | (int64_t)st->pomodoro.auto_start << 32 |
                  (int64_t)st->pomodoro.chime << 33 | (int64_t)st->pomodoro.ticking << 34 |
                  (int64_t)st->pomodoro.tick_volume << 35 | (int64_t)st->display.brightness << 40 |
                  (int64_t)st->automatic.calendar << 48 | (int64_t)st->automatic.mac << 49 |
                  (int64_t)st->automatic.meeting_titles << 50);
    h_str(&s, st->device.name);
    h_str(&s, st->device.time_zone);
    h_i64(&s, a->menu.kind);
    h_i64(&s, a->menu.n);
    for (int i = 0; i < a->menu.n; i++) h_tile(&s, &a->menu.tiles[i]);
    h_str(&s, a->toast);
    h_str(&s, a->pairing.code);
    h_str(&s, a->pairing.who);
    h_i64(&s, a->pairing.expires);
    h_i64(&s, a->paired_count);
    h_str(&s, a->paired_names);
    return s.h;
}

static void resync_sigs(tb_app_t *a)
{
    a->sig_state = state_sig(a);
    a->sig_rev = visible_sig(a);
    a->set_saved = a->set;
}

/* The end of every entry point: the effects and the rev that follow from the state. */
static void settle(tb_app_t *a, const tb_clock_t *now)
{
    if (!a->powered_off) {
        /* pairTryShow(): a waiting code goes on the screen once the power screens are gone (never during Wi-Fi setup).
         * It wakes a dark screen, closes an open menu without acting on it, hides the toast, and holds a ringing alarm
         * as a call does. The 2 minutes count from here. */
        if (a->pairing.active && !a->pairing.shown_at && !a->booting && !a->powering_off && a->hold == TB_HOLD_NONE &&
            !tb_app_on_wifi_screen(a)) {
            a->pairing.shown_at = stamp(now);
            a->pairing.expires = a->pairing.shown_at + TB_PAIR_MS;
            wake(a);
            hide_menu(a);
            a->toast[0] = '\0';
            a->toast_title_off = a->toast_title_len = 0;
            a->toast_hold_until = 0;
            if (a->ringing) {
                a->ringing = false;
                a->alarm_held_by_pairing = true;
            }
            tb_bump(a);
        }
        /* Firmware: the setup menu goes once setup is over underneath it (Connected moved on), so its Skip can't
         * undo a join that worked (decisions.md: "Connected is the end of setup"). */
        if (a->menu.kind == TB_MENU_SETUP && !tb_app_on_wifi_screen(a)) hide_menu(a);
        if (a->menu.kind != TB_MENU_NONE) tb_menu_fill(a, now);   /* Firmware: tiles stay current while open */

        int8_t level = tb_app_ticking(a, now) ? (a->set.pomodoro.tick_volume == TB_TICK_MEDIUM ? 2 : 1) : 0;
        if (level != a->ticking_level) {
            a->ticking_level = level;
            tb_fx(a, TB_FX_TICKING, level);
        }
        int16_t bl = a->off ? 0 : a->set.display.brightness;
        if (bl != a->bl_level) {
            a->bl_level = bl;
            tb_fx(a, TB_FX_BACKLIGHT, bl);
        }
    }
    if (!tb_settings_equal(&a->set, &a->set_saved)) {
        a->set_saved = a->set;
        tb_fx(a, TB_FX_SAVE_SETTINGS, 0);
    }
    uint32_t ss = state_sig(a);
    if (ss != a->sig_state) {
        a->sig_state = ss;
        tb_fx(a, TB_FX_SAVE_STATE, 0);
    }
    uint32_t vs = visible_sig(a);
    if (vs != a->sig_rev) {
        a->sig_rev = vs;
        a->rev++;
    }
}

/* ======================================================================================================== */
/* Life cycle                                                                                               */
/* ======================================================================================================== */

void tb_app_init(tb_app_t *a, const tb_settings_t *s, tb_wifi_mode_t wifi_start, const tb_clock_t *now)
{
    memset(a, 0, sizeof(*a));
    a->set = *s;
    /* Proposed first-boot status (not in the mock-up, which opens on a sample Pomodoro): Clock, with Available as
     * the status Stop returns to. tb_app_restore() replaces both on later boots. */
    a->idx = TB_ST_CLOCK;
    a->last_status = TB_ST_AVAILABLE;
    a->since_ms = now->mono;
    a->since = wall_or_0(now);
    a->booting = true;
    a->boot_until = now->mono + TB_BOOT_SPLASH_MS;
    /* Firmware: a skip is remembered, so an offline bar starts offline (the mock-up's powerOn() keeps the mode). */
    a->wifi_mode = wifi_start == TB_WIFI_OK || wifi_start == TB_WIFI_OFFLINE ? wifi_start : TB_WIFI_SETUP;
    strcpy(a->wifi_host, "tinybar.local");
    tb_pomo_init(&a->pomo, &a->set, now->valid ? tb_local_yyyymmdd(now->wall) : 0);
    tb_gesture_reset(&a->gesture);
    a->last_mono = now->mono;
    a->last_wall_min = -1;
    a->bl_level = (int16_t)a->set.display.brightness;
    tb_fx(a, TB_FX_BACKLIGHT, a->set.display.brightness);
    a->rev = 1;
    resync_sigs(a);
}

void tb_app_restore(tb_app_t *a, tb_status_t idx, tb_status_t last_status, const char *message, tb_epoch_t message_at,
                    uint16_t done_today, tb_ms_t focused_ms, int32_t tallies_day)
{
    if (idx < TB_ST_COUNT) a->idx = idx;
    if (last_status < TB_ST_COUNT && last_status != TB_ST_POMODORO && last_status != TB_ST_CLOCK) a->last_status = last_status;
    if (message) tb_strlcpy(a->message, message, sizeof(a->message));
    a->message_at = message_at;
    if (a->idx == TB_ST_MESSAGE && !a->message[0]) tb_strlcpy(a->message, TB_MESSAGE_FALLBACK, sizeof(a->message));
    a->pomo.done_today = done_today;
    a->pomo.focused_ms = focused_ms < 0 ? 0 : focused_ms;
    /* Tallies from another day reset at the first tick that knows the date (tb_pomo_roll_day). */
    a->pomo.day = tallies_day;
    a->rev++;
    resync_sigs(a);     /* what was just restored is what's saved */
}

int tb_app_take_effects(tb_app_t *a, tb_effect_t *out, int max)
{
    int n = a->n_fx < max ? a->n_fx : max;
    if (n < 0) n = 0;
    memcpy(out, a->fx, (size_t)n * sizeof(tb_effect_t));
    memmove(a->fx, a->fx + n, (size_t)(a->n_fx - n) * sizeof(tb_effect_t));
    a->n_fx = (uint8_t)(a->n_fx - n);
    return n;
}

/* loop() and the mock-up's timers (setTimeout): toasts, menus, Connected, PWR hold, Powering off, the splash, then
 * syncAuto, releaseHeldAlarm, the alarm repeats, the Pomodoro clock and endPhase, pending toasts. */
void tb_app_tick(tb_app_t *a, const tb_clock_t *now)
{
    if (a->powered_off) return;
    tb_ms_t dt = now->mono - a->last_mono;
    if (dt < 0) dt = 0;
    a->last_mono = now->mono;

    /* the timers */
    if (a->toast[0] && now->mono >= a->toast_until) {
        a->toast[0] = '\0';
        a->toast_title_off = a->toast_title_len = 0;
        tb_bump(a);
    }
    if (a->menu.kind != TB_MENU_NONE && now->mono >= a->menu.closes_at) hide_menu(a);   /* "menu: closed after 8 s" */
    if (a->wifi_mode == TB_WIFI_CONNECTED && now->mono >= a->connected_until) wifi_done(a);
    if (a->pwr_down_at) {
        tb_ms_t held = now->mono - a->pwr_down_at;
        if (held >= TB_PWR_OFF_MS) power_off(a, now);
        else if (held >= TB_PWR_SHOW_HOLD_MS && a->hold == TB_HOLD_NONE) {
            a->hold = TB_HOLD_KEEP_HOLDING;
            a->hold_since = now->mono;
            tb_bump(a);
        }
    }
    if (a->powering_off && now->mono - a->hold_since >= TB_POWERING_OFF_MS) {
        /* The power goes. A running Pomodoro stops when the power is cut; today's tomatoes are kept. */
        a->hold = TB_HOLD_NONE;
        a->powering_off = false;
        a->powered_off = true;
        a->off = false;
        a->toast[0] = '\0';
        a->pending_toast[0] = '\0';
        tb_pomo_reset_run(&a->pomo, &a->set);
        a->shown_kind = TB_AUTO_NONE;
        a->shown_id = 0;
        if (a->ticking_level) {
            a->ticking_level = 0;
            tb_fx(a, TB_FX_TICKING, 0);
        }
        tb_fx(a, TB_FX_POWER_OFF, 0);
        settle(a, now);
        return;
    }
    if (a->booting) {
        if (now->mono >= a->boot_until) boot_done(a, now);
        else {
            settle(a, now);
            return;     /* loop() does nothing while starting up */
        }
    }

    /* Firmware: "since" was picked before the clock was known; fill it in now. */
    if (now->valid && !a->since) a->since = now->wall - (now->mono - a->since_ms) / 1000;

    sync_auto(a, NULL, now);
    release_held_alarm(a, now);
    bool ended = false;
    if (a->ringing) {
        if (now->mono > a->ring_until) {
            a->ringing = false;             /* "alarm: stopped after a minute" */
            tb_bump(a);
        } else if (now->mono >= a->next_ring) {
            chime(a, a->pomo.phase != TB_PH_FOCUS, now);
            flash(a, now);
            a->next_ring = now->mono + TB_ALARM_REPEAT_MS;
        }
    }
    if (a->pomo.running && tb_pomo_advance(&a->pomo, &a->set, dt)) {
        end_phase(a, now);
        ended = true;
    }
    if (!ended && a->pending_toast[0] && tb_app_screen_free(a) && !a->toast[0]) {
        char text[TB_TOAST_BYTES];
        tb_strlcpy(text, a->pending_toast, sizeof text);
        uint8_t off = a->pending_title_off, len = a->pending_title_len;
        a->pending_toast[0] = '\0';
        a->pending_title_off = a->pending_title_len = 0;
        toast_span(a, text, off, len, false, now);
    }
    /* Firmware: today's tallies reset at local midnight (checked once a minute). */
    if (now->valid && now->wall / 60 != a->last_wall_min) {
        a->last_wall_min = now->wall / 60;
        if (tb_pomo_roll_day(&a->pomo, tb_local_yyyymmdd(now->wall))) tb_bump(a);
    }
    settle(a, now);
}

/* ======================================================================================================== */
/* Inputs from the bar: touch, BOOT, PWR, flip                                                              */
/* ======================================================================================================== */

/* The hold timer fired (550 ms without moving): silence any alarm and open the menu. On the pairing screen it
 * cancels the pairing instead. */
static void on_hold(tb_app_t *a, const tb_clock_t *now)
{
    if (tb_app_pairing_visible(a)) {
        /* A press that began before the code appeared is ignored, so a touch meant for the screen underneath can't
         * cancel it. */
        if (a->gesture.t0 >= a->pairing.shown_at) cancel_pairing(a, now);
        return;
    }
    silence(a);
    show_menu(a, now);
}

/* pointerup on a screen that's on, for a press that counts. g: TAP, MOVED_TAP, SWIPE_NEXT or SWIPE_PREV. */
static void on_release(tb_app_t *a, tb_gesture_t g, int8_t tile, const tb_clock_t *now)
{
    bool moved = g != TB_GEST_TAP;
    if (tb_app_pairing_visible(a)) {
        if (a->gesture.t0 < a->pairing.shown_at) return;   /* it began before the code appeared: ignored */
        silence(a);
        cancel_pairing(a, now);
        return;
    }
    if (a->menu.kind != TB_MENU_NONE) {
        /* Only a tap acts on a menu. A swipe is ignored, so it can't run whichever tile the finger lifts on. */
        if (moved) return;
        if (tile >= 0 && tile < a->menu.n && a->menu.tiles[tile].action != TB_ACT_NONE) menu_action(a, a->menu.tiles[tile].action, now);
        else hide_menu(a);              /* "tap: menu closed" (also a tap on the read-only Network tile) */
        return;
    }
    if (tb_app_on_wifi_screen(a)) {
        wifi_tap(a, now);
        return;
    }
    if (g == TB_GEST_SWIPE_NEXT) go(a, (int)a->idx + 1, true, now);
    else if (g == TB_GEST_SWIPE_PREV) go(a, (int)a->idx - 1, true, now);
    else if (tb_app_auto_top(a, now, NULL) != TB_AUTO_NONE) back_to_own(a, now);
    else if (a->idx == POMO) start_pause(a, true, now);
    else if (silence(a)) {
        toast(a, "Alarm off", now);
        tb_bump(a);
    } else go(a, (int)a->idx + 1, true, now);
}

void tb_app_pointer(tb_app_t *a, bool pressed, int16_t x, int16_t y, int8_t tile, const tb_clock_t *now)
{
    if (a->powered_off) return;
    if (!pressed && tile == TB_TILE_LOST) {
        /* pointercancel */
        tb_gesture_cancel(&a->gesture);
        a->ptr_live = false;
        settle(a, now);
        return;
    }
    tb_gesture_t g = tb_gesture_feed(&a->gesture, pressed, x, y, now->mono);
    if (pressed) {
        if (g == TB_GEST_DOWN) a->ptr_live = !(a->off || a->booting || a->powering_off);   /* pointerdown */
        else if (g == TB_GEST_HOLD && a->ptr_live) on_hold(a, now);
        settle(a, now);
        return;
    }
    /* pointerup */
    if (g == TB_GEST_HOLD && a->ptr_live) on_hold(a, now);  /* a hold that was due before this late sample */
    bool live = a->ptr_live;
    a->ptr_live = false;
    if (a->booting || a->powering_off) {
        settle(a, now);
        return;
    }
    if (a->off) {
        wake(a);        /* a dark screen wakes on the first tap, swipe or hold */
        settle(a, now);
        return;
    }
    if (live && (g == TB_GEST_TAP || g == TB_GEST_MOVED_TAP || g == TB_GEST_SWIPE_NEXT || g == TB_GEST_SWIPE_PREV))
        on_release(a, g, tile, now);
    settle(a, now);
}

void tb_app_pointer_poll(tb_app_t *a, const tb_clock_t *now)
{
    if (a->powered_off) return;
    if (tb_gesture_poll(&a->gesture, now->mono) == TB_GEST_HOLD && a->ptr_live) {
        on_hold(a, now);
        settle(a, now);
    }
}

/* BOOT: any button first wakes a dark screen or answers a ringing alarm. On an automatic screen it goes back to
 * your status, like a tap; otherwise the next status. */
static void boot_click(tb_app_t *a, const tb_clock_t *now)
{
    if (a->booting || a->powering_off) return;
    if (wake(a)) return;
    if (tb_app_pairing_visible(a)) {
        silence(a);
        cancel_pairing(a, now);
        return;
    }
    if (tb_app_on_wifi_screen(a)) {
        wifi_tap(a, now);
        return;
    }
    if (silence(a)) {
        toast(a, "Alarm off", now);
        tb_bump(a);
        return;
    }
    if (tb_app_auto_top(a, now, NULL) != TB_AUTO_NONE) {
        back_to_own(a, now);
        return;
    }
    go(a, (int)a->idx + 1, true, now);
}

void tb_app_button(tb_app_t *a, tb_button_t b, const tb_clock_t *now)
{
    if (a->powered_off) return;
    switch (b) {
    case TB_BTN_BOOT: boot_click(a, now); break;
    case TB_BTN_PWR_DOWN:
        /* While starting up or powering off a press does nothing (the mock-up arms no timers then). */
        a->pwr_down_at = a->booting || a->powering_off ? 0 : stamp(now);
        break;
    case TB_BTN_PWR_UP: {
        if (!a->pwr_down_at) break;
        tb_ms_t held = now->mono - a->pwr_down_at;
        if (held >= TB_PWR_OFF_MS) {    /* the 3 s timer was due before this release */
            power_off(a, now);
            break;
        }
        a->pwr_down_at = 0;
        if (a->hold == TB_HOLD_KEEP_HOLDING) {
            a->hold = TB_HOLD_NONE;     /* "PWR: released early, still on" */
            tb_bump(a);
        }
        if (a->booting) break;
        if (held < TB_PWR_SHOW_HOLD_MS) pwr_short(a, now);
        break;
    }
    }
    settle(a, now);
}

/* flipBtn: turning the bar over always does three things: turns the layout the right way up, silences any alarm,
 * and starts whatever the Pomodoro is waiting for. A running timer is left alone. On a call or meeting screen it
 * also shows the Pomodoro, which sets the call or meeting aside. */
void tb_app_flip(tb_app_t *a, bool flipped, bool initial, const tb_clock_t *now)
{
    if (a->powered_off) return;
    if (flipped == a->flipped) return;
    a->flipped = flipped;
    tb_fx(a, TB_FX_ROTATE, flipped);
    tb_bump(a);
    /* Firmware: the IMU's orientation is absolute, so the layout always follows it; the semantics wait for the bar
     * to be up (the mock-up ignores a flip during the splash; at boot, initial just sets the layout). */
    if (initial || a->booting || a->powering_off) {
        settle(a, now);
        return;
    }
    /* Proposed (decisions.md, Pairing): on a pairing code a flip cancels it first, then does all three things as
     * usual. A held alarm is answered by the flip starting the next phase. */
    bool pairing = tb_app_pairing_shown(a);
    if (pairing) end_pairing_here(a, PE_FLIP, now);
    wake(a);
    hide_menu(a);
    bool quieted = silence(a);
    tb_auto_t aside = set_aside(a, now);
    tb_pomo_start_t started = tb_pomo_start_waiting(&a->pomo, &a->set);
    if (started != TB_POMO_NOTHING || aside != TB_AUTO_NONE) a->idx = POMO;
    if (pairing) {
        if (!a->pomo.waiting) a->alarm_held_by_pairing = false;
        if (started != TB_POMO_NOTHING) toast_started_span(a, started, "Pairing canceled \xC2\xB7 ", true, now);
        else toast_hold(a, "Pairing canceled", now);
        release_held_alarm(a, now);
        tb_bump(a);
        settle(a, now);
        return;
    }
    const char *before = quieted ? "Alarm off \xC2\xB7 " : "";
    if (started != TB_POMO_NOTHING) toast_started(a, started, before, now);
    else toastf(a, now, "%s%s already running", before, tb_phase_name(a->pomo.phase));
    tb_bump(a);
    settle(a, now);
}

/* ======================================================================================================== */
/* Inputs from the Remote and the API                                                                       */
/* ======================================================================================================== */

static tb_err_t remote_guard(const tb_app_t *a, bool setup_blocks)
{
    if (a->powered_off || a->powering_off || a->booting) return TB_E_POWERED_OFF;
    if (setup_blocks && tb_app_on_wifi_screen(a)) return TB_E_IN_SETUP;
    return TB_OK;
}

static bool valid_hhmm(const char *s)
{
    if (strlen(s) != 5 || s[2] != ':') return false;
    for (int i = 0; i < 5; i++)
        if (i != 2 && (s[i] < '0' || s[i] > '9')) return false;
    int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
    return h < 24 && m < 60;
}

/* The Remote's status buttons: if (s.off) togglePower(); go(i, 'remote'). */
tb_err_t tb_app_remote_status(tb_app_t *a, tb_status_t st, const char *back_at, const char *note, bool set_aside_it,
                              const tb_clock_t *now)
{
    tb_err_t err = remote_guard(a, true);
    if (err) return err;
    if ((int)st < 0 || st >= TB_ST_COUNT) return TB_E_BAD_VALUE;
    bool has_back = back_at && back_at[0], has_note = note && note[0];
    if ((has_back || has_note) && st != TB_ST_AWAY) return TB_E_BAD_VALUE;
    if (has_back && !valid_hhmm(back_at)) return TB_E_BAD_VALUE;
    char clean[TB_AWAY_NOTE_BYTES] = "";
    if (has_note) {
        char tmp[TB_AWAY_NOTE_CHARS * 4 + 8];
        size_t n = tb_text_clean(tmp, sizeof tmp, note);
        if (n < 1 || n > TB_AWAY_NOTE_CHARS) return TB_E_BAD_VALUE;
        tb_text_replace_unsupported(tmp, sizeof tmp);
        tb_strlcpy(clean, tmp, sizeof clean);
    }
    if (st == TB_ST_MESSAGE && !a->message[0]) return TB_E_NO_MESSAGE;
    wake(a);
    if (st == TB_ST_AWAY) {
        /* Proposed (api.md 8.1): without them the bar shows its plain Away screen. */
        tb_strlcpy(a->away_back_at, has_back ? back_at : "", sizeof(a->away_back_at));
        tb_strlcpy(a->away_note, clean, sizeof(a->away_note));
    }
    go(a, st, set_aside_it, now);
    settle(a, now);
    return TB_OK;
}

/* msgForm: s.message = text; s.messageAt = now(); go(message). Firmware: it wakes a dark screen like the status
 * buttons (the mock-up's message form doesn't, which looks like an oversight). */
tb_err_t tb_app_remote_message(tb_app_t *a, const char *text, bool set_aside_it, const tb_clock_t *now)
{
    tb_err_t err = remote_guard(a, true);
    if (err) return err;
    char tmp[TB_MESSAGE_MAX_CHARS * 4 + 8];
    size_t n = tb_text_clean(tmp, sizeof tmp, text);
    if (n < 1 || n > TB_MESSAGE_MAX_CHARS) return TB_E_BAD_VALUE;
    tb_text_replace_unsupported(tmp, sizeof tmp);
    tb_strlcpy(a->message, tmp, sizeof(a->message));
    a->message_at = wall_or_0(now);
    wake(a);
    go(a, TB_ST_MESSAGE, set_aside_it, now);
    settle(a, now);
    return TB_OK;
}

/* abBtn and autoShowAgain: wake(); set aside, or Show again. */
tb_err_t tb_app_remote_aside(tb_app_t *a, bool aside, const tb_clock_t *now)
{
    tb_err_t err = remote_guard(a, true);
    if (err) return err;
    if (aside) {
        if (tb_app_auto_top(a, now, NULL) == TB_AUTO_NONE) return TB_E_NOTHING_TO_SET_ASIDE;
        wake(a);
        back_to_own(a, now);
    } else {
        if (tb_app_aside_kind(a, now) == TB_AUTO_NONE) return TB_E_NOTHING_SET_ASIDE;
        wake(a);
        show_again(a, now);
    }
    settle(a, now);
    return TB_OK;
}

/* The Remote's pStart, pSkip and pStop (toPomo: wake(); s.idx = POMO; fn('remote')) and api.md 9.1. */
tb_err_t tb_app_remote_pomodoro(tb_app_t *a, tb_pomo_action_t act, int minutes, bool set_aside_it, const tb_clock_t *now)
{
    tb_err_t err = remote_guard(a, true);
    if (err) return err;
    switch (act) {
    case TB_POMO_START:
        /* Starts what's waiting, a ready session or a paused timer; already running, nothing changes. */
        wake(a);
        a->idx = POMO;
        silence(a);
        if (set_aside_it) set_aside(a, now);
        {
            tb_pomo_start_t started = tb_pomo_start_waiting(&a->pomo, &a->set);
            if (started != TB_POMO_NOTHING) toast_started(a, started, "", now);
        }
        tb_bump(a);
        break;
    case TB_POMO_PAUSE:
    case TB_POMO_TOGGLE:
        /* startPause() pauses a running timer; "pause" on one that isn't running is an error. */
        if (act == TB_POMO_PAUSE && !a->pomo.running) {
            /* api.md 9.1: every action silences a ringing alarm first, as any control does, even this refused one. */
            if (silence(a)) {
                wake(a);
                toast(a, "Alarm off", now);
                tb_bump(a);
                settle(a, now);
            }
            return TB_E_NOT_RUNNING;
        }
        wake(a);
        a->idx = POMO;
        start_pause(a, set_aside_it, now);
        break;
    case TB_POMO_SKIP:
        wake(a);
        a->idx = POMO;
        skip(a, set_aside_it, now);
        break;
    case TB_POMO_STOP:
        wake(a);
        stop(a, set_aside_it, now);
        break;
    case TB_POMO_EXTEND:
        if (minutes < 1 || minutes > 60) return TB_E_BAD_VALUE;
        if (!tb_pomo_mid_phase(&a->pomo, &a->set)) return TB_E_NOTHING_TO_EXTEND;
        wake(a);
        a->idx = POMO;
        silence(a);
        if (set_aside_it) set_aside(a, now);
        a->pomo.remaining_ms += (tb_ms_t)minutes * 60000;
        toastf(a, now, "+%d min", minutes);
        tb_bump(a);
        break;
    default: return TB_E_BAD_VALUE;
    }
    settle(a, now);
    return TB_OK;
}

/*
 * PATCH /api/v1/settings (api.md 10.2), as the mock-up's Remote controls do it: the length segments, pAuto, pSound,
 * pTick and tickSeg, calOn, calTitles, macOn. All or nothing (checked first). Every change shows its toast; when a
 * patch changes several things, the last of these wins: lengths, auto-start, chime, ticking, brightness, meeting
 * titles, Calls from your Mac, Calendar meetings.
 */
tb_err_t tb_app_remote_settings(tb_app_t *a, const tb_settings_patch_t *p, const char **field, const tb_clock_t *now)
{
    tb_err_t err = remote_guard(a, false);
    if (err) {
        if (field) *field = NULL;
        return err;
    }
    err = tb_settings_check(p, a->cal_saved, field);
    if (err != TB_OK) return err;
    tb_settings_t old = a->set;
    tb_settings_t want = a->set;
    tb_settings_apply(&want, p);

    /* The length segments: restart the phase that's running at its new length, move the round down, then show the
     * Pomodoro (during a call or meeting a length change doesn't pull the bar off the automatic screen). */
    a->set.pomodoro.focus_min = want.pomodoro.focus_min;
    a->set.pomodoro.short_min = want.pomodoro.short_min;
    a->set.pomodoro.long_min = want.pomodoro.long_min;
    a->set.pomodoro.long_every = want.pomodoro.long_every;
    tb_pomo_settings_changed(&a->pomo, &old, &a->set);
    if (p->has_focus_min || p->has_short_min || p->has_long_min || p->has_long_every) {
        wake(a);
        if (tb_app_auto_top(a, now, NULL) == TB_AUTO_NONE) a->idx = POMO;
        /* Firmware: the mock-up's segments change the screen without a toast; the API says each change shows one.
         * The words are the mock-up's log lines. */
        if (p->has_focus_min) toastf(a, now, "Focus set to %d min", a->set.pomodoro.focus_min);
        if (p->has_short_min) toastf(a, now, "Short break set to %d min", a->set.pomodoro.short_min);
        if (p->has_long_min) toastf(a, now, "Long break set to %d min", a->set.pomodoro.long_min);
        if (p->has_long_every) toastf(a, now, "Long break after %d sessions", a->set.pomodoro.long_every);
    }
    if (p->has_auto_start) {
        a->set.pomodoro.auto_start = want.pomodoro.auto_start;
        toastf(a, now, "Auto-start %s", a->set.pomodoro.auto_start ? "on" : "off");
    }
    if (p->has_chime) {
        a->set.pomodoro.chime = want.pomodoro.chime;
        toastf(a, now, "Chime %s", a->set.pomodoro.chime ? "on" : "off");     /* new copy (mock-up: log only) */
    }
    if (p->has_ticking || p->has_tick_volume)
        set_ticking(a, p->has_ticking ? want.pomodoro.ticking : a->set.pomodoro.ticking, p->has_tick_volume,
                    want.pomodoro.tick_volume, now);
    if (p->has_brightness) {
        a->set.display.brightness = want.display.brightness;
        toastf(a, now, "Light %d%%", a->set.display.brightness);    /* new copy, named like the quick menu's Light tile */
    }
    if (p->has_name) tb_strlcpy(a->set.device.name, want.device.name, sizeof(a->set.device.name));
    if (p->has_time_zone) tb_strlcpy(a->set.device.time_zone, want.device.time_zone, sizeof(a->set.device.time_zone));
    if (p->has_meeting_titles) {
        a->set.automatic.meeting_titles = want.automatic.meeting_titles;
        tb_bump(a);
        toastf(a, now, "Meeting titles %s", a->set.automatic.meeting_titles ? "on" : "off");
    }
    /* Turning a source off ends its status at once and forgets a call or meeting set aside from it; turning it back
     * on during a call or meeting shows it at once. */
    if (p->has_mac) {
        a->set.automatic.mac = want.automatic.mac;
        if (!a->set.automatic.mac) a->aside_call = 0;
        tb_bump(a);
        char lead[48];
        snprintf(lead, sizeof lead, "Calls from your Mac %s", a->set.automatic.mac ? "on" : "off");
        if (!sync_auto(a, lead, now)) toast(a, lead, now);
    }
    if (p->has_calendar) {
        a->set.automatic.calendar = want.automatic.calendar;
        if (!a->set.automatic.calendar) a->aside_meeting = 0;
        tb_bump(a);
        char lead[48];
        snprintf(lead, sizeof lead, "Calendar meetings %s", a->set.automatic.calendar ? "on" : "off");
        if (!sync_auto(a, lead, now)) toast(a, lead, now);
    }
    tb_bump(a);
    settle(a, now);
    return TB_OK;
}

void tb_app_set_time_zone(tb_app_t *a, const char *iana, const tb_clock_t *now)
{
    if (!iana || !iana[0] || strlen(iana) >= sizeof(a->set.device.time_zone)) return;
    if (a->set.device.time_zone[0]) return;     /* api.md 6.6: only if the bar has no time zone yet */
    tb_strlcpy(a->set.device.time_zone, iana, sizeof(a->set.device.time_zone));
    tb_bump(a);
    settle(a, now);
}

/* ======================================================================================================== */
/* Automatic sources                                                                                        */
/* ======================================================================================================== */

/* The Mac's call (simCallStart / simCallEnd / macTimers in the mock-up; net's Mac table on the device). */
void tb_app_set_call(tb_app_t *a, const tb_call_t *call, const char *lead, const tb_clock_t *now)
{
    if (a->powered_off) return;
    bool had = tb_app_call_now(a);
    if (call && call->active) {
        a->call = *call;
        a->call.app[sizeof(a->call.app) - 1] = '\0';
        if (!a->call.id) a->call.id = 1;
    } else {
        memset(&a->call, 0, sizeof(a->call));
        a->aside_call = 0;
    }
    tb_bump(a);
    /* macTimers(): if (!syncAuto('Lost contact with your Mac')) { if (hadCall) notify('Lost contact ... · call ended') } */
    if (!sync_auto(a, lead, now) && lead && had && !tb_app_call_now(a)) notifyf(a, now, "%s \xC2\xB7 call ended", lead);
    settle(a, now);
}

void tb_app_set_mac_link(tb_app_t *a, tb_link_t link, const tb_clock_t *now)
{
    if (a->powered_off) return;
    if (a->mac_link != link) {
        a->mac_link = link;
        tb_bump(a);
    }
    settle(a, now);
}

void tb_app_set_meetings(tb_app_t *a, const tb_meeting_t *m, int n, const tb_clock_t *now)
{
    if (a->powered_off) return;
    if (n > TB_MEETINGS_MAX) n = TB_MEETINGS_MAX;
    if (n < 0 || !m) n = 0;
    if (n > 0) memcpy(a->meetings, m, (size_t)n * sizeof(*m));
    for (int i = 0; i < n; i++) {
        tb_meeting_t *e = &a->meetings[i];
        if (!e->id) e->id = 1;
        e->title[sizeof(e->title) - 1] = '\0';
        e->location[sizeof(e->location) - 1] = '\0';
    }
    a->n_meetings = (uint8_t)n;
    a->rev++;   /* the list isn't in the visible signature (too big to hash every loop) */
    sync_auto(a, NULL, now);
    settle(a, now);
}

void tb_app_set_calendar(tb_app_t *a, bool saved, bool checking, tb_epoch_t last_sync, const tb_clock_t *now)
{
    if (a->powered_off) return;
    a->cal_saved = saved;
    a->cal_checking = checking;
    a->cal_last_sync = last_sync;
    tb_bump(a);
    sync_auto(a, NULL, now);
    settle(a, now);
}

/* saveCalendar(), removeCalendar() and the Sync buttons, as events from the calendar service. */
void tb_app_calendar_event(tb_app_t *a, tb_cal_event_t ev, const tb_clock_t *now)
{
    if (a->powered_off) return;
    char left[48];
    switch (ev) {
    case TB_CALEV_SAVED: {
        bool fresh = !a->cal_saved;
        a->cal_saved = true;
        if (fresh) a->set.automatic.calendar = true;    /* a first address turns Calendar meetings on */
        tb_bump(a);
        if (!sync_auto(a, NULL, now)) notifyf(a, now, "Calendar synced \xC2\xB7 %s", tb_app_left_text(a, now, left, sizeof left));
        break;
    }
    case TB_CALEV_SETUP_FAILED:
        notify_span(a, "Calendar address didn't work \xC2\xB7 add it on the Remote", 0, 0, now);
        break;
    case TB_CALEV_REMOVED:
        /* The address and the saved copy go; Calendar meetings and Show meeting titles turn off (api.md 11.3). */
        a->cal_saved = false;
        a->cal_checking = false;
        a->cal_last_sync = 0;
        a->n_meetings = 0;
        a->aside_meeting = 0;
        a->set.automatic.calendar = false;
        a->set.automatic.meeting_titles = false;
        a->rev++;
        if (!sync_auto(a, "Calendar removed", now)) toast(a, "Calendar removed", now);
        break;
    case TB_CALEV_SYNCED:
        a->cal_checking = false;
        toast(a, "Calendar synced", now);
        break;
    case TB_CALEV_SYNC_FAILED:
        a->cal_checking = false;
        toast(a, "Couldn't sync the calendar", now);
        break;
    }
    settle(a, now);
}

/* ======================================================================================================== */
/* Wi-Fi                                                                                                    */
/* ======================================================================================================== */

void tb_app_wifi_connecting(tb_app_t *a, const char *ssid, const tb_clock_t *now)
{
    if (a->powered_off) return;
    a->wifi_mode = TB_WIFI_CONNECTING;
    tb_strlcpy(a->wifi_ssid, ssid ? ssid : "", sizeof(a->wifi_ssid));
    tb_bump(a);
    settle(a, now);
}

void tb_app_wifi_connected(tb_app_t *a, const char *ssid, const char *ip, const char *host, const tb_clock_t *now)
{
    if (a->powered_off) return;
    a->wifi_mode = TB_WIFI_CONNECTED;
    a->wifi_link_up = true;
    a->wifi_error[0] = '\0';
    tb_strlcpy(a->wifi_ssid, ssid ? ssid : "", sizeof(a->wifi_ssid));
    tb_strlcpy(a->wifi_ip, ip ? ip : "", sizeof(a->wifi_ip));
    if (host && host[0]) tb_strlcpy(a->wifi_host, host, sizeof(a->wifi_host));
    a->connected_until = now->mono + TB_CONNECTED_MS;
    tb_bump(a);
    settle(a, now);
}

void tb_app_wifi_failed(tb_app_t *a, const char *ssid, const char *error_text, const tb_clock_t *now)
{
    if (a->powered_off) return;
    a->wifi_mode = TB_WIFI_FAILED;
    tb_strlcpy(a->wifi_ssid, ssid ? ssid : "", sizeof(a->wifi_ssid));
    tb_strlcpy(a->wifi_error, error_text ? error_text : "", sizeof(a->wifi_error));
    tb_bump(a);
    settle(a, now);
}

void tb_app_wifi_link(tb_app_t *a, bool up, const char *ip, const char *host, const tb_clock_t *now)
{
    if (a->powered_off) return;
    a->wifi_link_up = up;
    if (up && ip) tb_strlcpy(a->wifi_ip, ip, sizeof(a->wifi_ip));
    if (!up) a->wifi_ip[0] = '\0';
    if (host && host[0]) tb_strlcpy(a->wifi_host, host, sizeof(a->wifi_host));
    tb_bump(a);
    settle(a, now);
}

/* ======================================================================================================== */
/* Pairing (api.md 4.8, proposed)                                                                           */
/* ======================================================================================================== */

/* KIND_LABEL: the label when the device sent no name the bar can draw (api.md 4.6; "Phone" proposed for remote). */
static const char *kind_label(tb_pair_kind_t k)
{
    return k == TB_PAIR_KIND_MAC ? "Mac" : k == TB_PAIR_KIND_PHONE ? "Phone" : k == TB_PAIR_KIND_SCRIPT ? "Script" : "Device";
}

void tb_app_pairing_show(tb_app_t *a, const char *code, const char *who, tb_pair_kind_t kind, const tb_clock_t *now)
{
    if (a->powered_off) return;
    a->pairing.active = true;
    a->pairing.kind = kind;
    tb_strlcpy(a->pairing.code, code ? code : "", sizeof(a->pairing.code));
    tb_strlcpy(a->pairing.who, who && who[0] ? who : kind_label(kind), sizeof(a->pairing.who));
    a->pairing.shown_at = 0;
    a->pairing.expires = 0;
    tb_bump(a);
    settle(a, now);     /* shows it now, or once the power screens are gone (pairTryShow) */
}

/* pairEnd() for the endings net reports. USB pairing has no code: it only confirms, and a code another device asked
 * for stays on the screen (the confirmation follows once that pairing ends). */
void tb_app_pairing_end(tb_app_t *a, tb_pair_end_t why, const char *who, const tb_clock_t *now)
{
    if (a->powered_off) return;
    const char *name = who && who[0] ? who : a->pairing.who[0] ? a->pairing.who : "Mac";
    char label[TB_CLIENT_NAME_BYTES];
    tb_strlcpy(label, name, sizeof label);
    if (why == TB_PAIR_END_PAIRED_USB) {
        notifyf(a, now, "Paired \xC2\xB7 %s \xC2\xB7 over USB", label);
        settle(a, now);
        return;
    }
    bool shown = a->pairing.active && a->pairing.shown_at != 0;
    a->pairing.active = false;
    a->pairing.shown_at = 0;
    tb_bump(a);
    switch (why) {
    case TB_PAIR_END_PAIRED: {
        char t[TB_TOAST_BYTES];
        snprintf(t, sizeof t, "Paired \xC2\xB7 %s", label);
        toast_hold(a, t, now);
        break;
    }
    case TB_PAIR_END_TIMEOUT: if (shown) toast_hold(a, "Pairing timed out", now); break;
    case TB_PAIR_END_WRONG_CODE: if (shown) toast_hold(a, "Pairing canceled \xC2\xB7 wrong code", now); break;
    case TB_PAIR_END_CANCELED: if (shown) toast_hold(a, "Pairing canceled", now); break;
    default: break;
    }
    release_held_alarm(a, now);
    settle(a, now);
}

void tb_app_set_paired(tb_app_t *a, uint8_t n, const char *names)
{
    char nm[TB_PAIRED_NAMES_BYTES];
    tb_strlcpy(nm, n && names ? names : "", sizeof nm);
    if (n == a->paired_count && !strcmp(nm, a->paired_names)) return;    /* unchanged: the ETag stays */
    a->paired_count = n;
    memcpy(a->paired_names, nm, sizeof nm);
    a->rev++;
}

void tb_app_set_paired_count(tb_app_t *a, uint8_t n)
{
    a->paired_count = n;
    if (!n) a->paired_names[0] = '\0';
    a->rev++;
}

void tb_app_notify(tb_app_t *a, const char *text, const tb_clock_t *now)
{
    if (a->powered_off || !text) return;
    notify_span(a, text, 0, 0, now);
    settle(a, now);
}
