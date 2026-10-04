/*
 * tb_app.c: the device state machine. Owner: core builder.
 *
 * Skeleton: init, effects and the simplest queries are real so the firmware boots into a drawable state; every
 * handler is a stub marked TODO(core). Port each one from docs/mockup.html (function names in tb_app.h), keeping the
 * mock-up's order of operations, toasts and log-worthy outcomes, and cover it with host tests in test/host/core/.
 */
#include <string.h>

#include "tb_app.h"
#include "tb_fmt.h"
#include "tb_internal.h"

void tb_fx(tb_app_t *a, tb_effect_kind_t kind, int32_t arg)
{
    if (a->n_fx >= TB_EFFECTS_MAX) return;      /* TODO(core): count drops; the app task drains every loop */
    a->fx[a->n_fx++] = (tb_effect_t){kind, arg};
}

void tb_app_init(tb_app_t *a, const tb_settings_t *s, bool wifi_configured, const tb_clock_t *now)
{
    memset(a, 0, sizeof(*a));
    a->set = *s;
    /* Proposed first-boot status (not in the mock-up, which opens on a sample Pomodoro): Clock, with Available as
     * the status Stop returns to. tb_app_restore() replaces both on later boots. */
    a->idx = TB_ST_CLOCK;
    a->last_status = TB_ST_AVAILABLE;
    a->since_ms = now->mono;
    a->since = now->valid ? now->wall : 0;
    a->booting = true;
    a->boot_until = now->mono + TB_BOOT_SPLASH_MS;
    a->wifi_mode = wifi_configured ? TB_WIFI_OK : TB_WIFI_SETUP;
    strcpy(a->wifi_host, "tinybar.local");
    tb_pomo_init(&a->pomo, &a->set, now->valid ? tb_local_yyyymmdd(now->wall) : 0);
    tb_gesture_reset(&a->gesture);
    tb_fx(a, TB_FX_BACKLIGHT, a->set.display.brightness);
    a->rev = 1;
}

void tb_app_restore(tb_app_t *a, tb_status_t idx, tb_status_t last_status, const char *message, tb_epoch_t message_at,
                    uint16_t done_today, tb_ms_t focused_ms, int32_t tallies_day)
{
    if (idx < TB_ST_COUNT) a->idx = idx;
    if (last_status < TB_ST_COUNT && last_status != TB_ST_POMODORO && last_status != TB_ST_CLOCK) a->last_status = last_status;
    if (message) tb_strlcpy(a->message, message, sizeof(a->message));
    a->message_at = message_at;
    a->pomo.done_today = done_today;
    a->pomo.focused_ms = focused_ms;
    a->pomo.day = tallies_day;
    a->rev++;
}

void tb_app_tick(tb_app_t *a, const tb_clock_t *now)
{
    /* TODO(core): loop() — syncAuto, releaseHeldAlarm, alarm repeats, Pomodoro clock and endPhase, pending toasts,
     * toast expiry, the menu's 8 s, PWR hold timing, Powering off, Connected's 3 s, day roll-over, ticking level. */
    if (a->booting && now->mono >= a->boot_until) {
        a->booting = false;     /* TODO(core): powerOn()'s end: Pomodoro -> last status, aside cleared, "Ready" toast */
        a->rev++;
    }
    if (a->toast[0] && now->mono >= a->toast_until) {
        a->toast[0] = '\0';
        a->rev++;
    }
}

int tb_app_take_effects(tb_app_t *a, tb_effect_t *out, int max)
{
    int n = a->n_fx < max ? a->n_fx : max;
    memcpy(out, a->fx, (size_t)n * sizeof(tb_effect_t));
    memmove(a->fx, a->fx + n, (size_t)(a->n_fx - n) * sizeof(tb_effect_t));
    a->n_fx = (uint8_t)(a->n_fx - n);
    return n;
}

/* ---------- Inputs: all TODO(core) ---------- */

void tb_app_pointer(tb_app_t *a, bool pressed, int16_t x, int16_t y, int8_t tile, const tb_clock_t *now)
{
    (void)tile;
    (void)tb_gesture_feed(&a->gesture, pressed, x, y, now->mono);   /* TODO(core): act on the gesture */
}

void tb_app_pointer_poll(tb_app_t *a, const tb_clock_t *now)
{
    (void)tb_gesture_poll(&a->gesture, now->mono);   /* TODO(core) */
}

void tb_app_button(tb_app_t *a, tb_button_t b, const tb_clock_t *now)
{
    (void)a;
    (void)b;
    (void)now;  /* TODO(core): BOOT click, PWR down/up */
}

void tb_app_flip(tb_app_t *a, bool flipped, bool initial, const tb_clock_t *now)
{
    (void)now;
    if (flipped != a->flipped) {
        a->flipped = flipped;
        tb_fx(a, TB_FX_ROTATE, flipped);
        a->rev++;
    }
    (void)initial;  /* TODO(core): the flip semantics when !initial */
}

tb_err_t tb_app_remote_status(tb_app_t *a, tb_status_t st, const char *back_at, const char *note, bool set_aside,
                              const tb_clock_t *now)
{
    (void)a; (void)st; (void)back_at; (void)note; (void)set_aside; (void)now;
    return TB_OK;   /* TODO(core) */
}

tb_err_t tb_app_remote_message(tb_app_t *a, const char *text, bool set_aside, const tb_clock_t *now)
{
    (void)a; (void)text; (void)set_aside; (void)now;
    return TB_OK;   /* TODO(core) */
}

tb_err_t tb_app_remote_aside(tb_app_t *a, bool aside, const tb_clock_t *now)
{
    (void)a; (void)aside; (void)now;
    return TB_OK;   /* TODO(core) */
}

tb_err_t tb_app_remote_pomodoro(tb_app_t *a, tb_pomo_action_t act, int minutes, bool set_aside, const tb_clock_t *now)
{
    (void)a; (void)act; (void)minutes; (void)set_aside; (void)now;
    return TB_OK;   /* TODO(core) */
}

tb_err_t tb_app_remote_settings(tb_app_t *a, const tb_settings_patch_t *p, const char **field, const tb_clock_t *now)
{
    (void)now;
    tb_err_t err = tb_settings_check(p, a->cal_saved, field);
    if (err != TB_OK) return err;
    tb_settings_t old = a->set;
    tb_settings_apply(&a->set, p);
    tb_pomo_settings_changed(&a->pomo, &old, &a->set);
    tb_fx(a, TB_FX_SAVE_SETTINGS, 0);
    a->rev++;
    return TB_OK;   /* TODO(core): the toasts and source on/off effects of api.md 10.2 */
}

void tb_app_set_time_zone(tb_app_t *a, const char *iana, const tb_clock_t *now)
{
    (void)now;
    if (!iana || !strcmp(a->set.device.time_zone, iana)) return;
    tb_strlcpy(a->set.device.time_zone, iana, sizeof(a->set.device.time_zone));
    tb_fx(a, TB_FX_SAVE_SETTINGS, 0);
    a->rev++;
}

void tb_app_set_call(tb_app_t *a, const tb_call_t *call, const char *lead, const tb_clock_t *now)
{
    (void)lead; (void)now;
    if (call && call->active) a->call = *call;
    else memset(&a->call, 0, sizeof(a->call));
    a->rev++;   /* TODO(core): syncAuto(lead) */
}

void tb_app_set_mac_link(tb_app_t *a, tb_link_t link, const tb_clock_t *now)
{
    (void)now;
    if (a->mac_link != link) {
        a->mac_link = link;
        a->rev++;
    }
}

void tb_app_set_meetings(tb_app_t *a, const tb_meeting_t *m, int n, const tb_clock_t *now)
{
    (void)now;
    if (n > TB_MEETINGS_MAX) n = TB_MEETINGS_MAX;
    if (n > 0) memcpy(a->meetings, m, (size_t)n * sizeof(*m));
    a->n_meetings = (uint8_t)(n < 0 ? 0 : n);
    a->rev++;   /* TODO(core): syncAuto() */
}

void tb_app_set_calendar(tb_app_t *a, bool saved, bool checking, tb_epoch_t last_sync, const tb_clock_t *now)
{
    (void)now;
    a->cal_saved = saved;
    a->cal_checking = checking;
    a->cal_last_sync = last_sync;
    a->rev++;
}

void tb_app_calendar_event(tb_app_t *a, tb_cal_event_t ev, const tb_clock_t *now)
{
    (void)a; (void)ev; (void)now;   /* TODO(core) */
}

void tb_app_wifi_connecting(tb_app_t *a, const char *ssid, const tb_clock_t *now)
{
    (void)now;
    a->wifi_mode = TB_WIFI_CONNECTING;
    tb_strlcpy(a->wifi_ssid, ssid, sizeof(a->wifi_ssid));
    a->rev++;
}

void tb_app_wifi_connected(tb_app_t *a, const char *ssid, const char *ip, const char *host, const tb_clock_t *now)
{
    a->wifi_mode = TB_WIFI_CONNECTED;
    a->wifi_link_up = true;
    tb_strlcpy(a->wifi_ssid, ssid, sizeof(a->wifi_ssid));
    tb_strlcpy(a->wifi_ip, ip, sizeof(a->wifi_ip));
    if (host) tb_strlcpy(a->wifi_host, host, sizeof(a->wifi_host));
    a->connected_until = now->mono + TB_CONNECTED_MS;
    a->rev++;
}

void tb_app_wifi_failed(tb_app_t *a, const char *ssid, const char *error_text, const tb_clock_t *now)
{
    (void)now;
    a->wifi_mode = TB_WIFI_FAILED;
    tb_strlcpy(a->wifi_ssid, ssid, sizeof(a->wifi_ssid));
    tb_strlcpy(a->wifi_error, error_text, sizeof(a->wifi_error));
    a->rev++;
}

void tb_app_wifi_link(tb_app_t *a, bool up, const char *ip, const char *host, const tb_clock_t *now)
{
    (void)now;
    a->wifi_link_up = up;
    if (ip) tb_strlcpy(a->wifi_ip, ip, sizeof(a->wifi_ip));
    if (host) tb_strlcpy(a->wifi_host, host, sizeof(a->wifi_host));
    a->rev++;
}

void tb_app_pairing_show(tb_app_t *a, const char *code, const char *who, tb_ms_t expires, const tb_clock_t *now)
{
    (void)now;
    a->pairing.active = true;
    tb_strlcpy(a->pairing.code, code, sizeof(a->pairing.code));
    tb_strlcpy(a->pairing.who, who, sizeof(a->pairing.who));
    a->pairing.expires = expires;
    a->rev++;   /* TODO(core): wake a dark screen, replace an open menu, wait for the power screens */
}

void tb_app_pairing_end(tb_app_t *a, tb_pair_end_t why, const char *who, const tb_clock_t *now)
{
    (void)why; (void)who; (void)now;
    a->pairing.active = false;
    a->rev++;   /* TODO(core): the toast for each ending */
}

void tb_app_set_paired_count(tb_app_t *a, uint8_t n)
{
    a->paired_count = n;
}

void tb_app_notify(tb_app_t *a, const char *text, const tb_clock_t *now)
{
    /* TODO(core): notify(): show now if screenFree(), else keep it in pending_toast. */
    tb_strlcpy(a->toast, text, sizeof(a->toast));
    a->toast_until = now->mono + TB_TOAST_MS;
    a->rev++;
}

/* ---------- Queries ---------- */

tb_auto_t tb_app_auto_top(const tb_app_t *a, const tb_clock_t *now, uint32_t *id)
{
    (void)a; (void)now;
    if (id) *id = 0;
    return TB_AUTO_NONE;    /* TODO(core): autoTop() */
}

tb_auto_t tb_app_aside_kind(const tb_app_t *a, const tb_clock_t *now)
{
    (void)a; (void)now;
    return TB_AUTO_NONE;    /* TODO(core): asideKind() */
}

bool tb_app_on_wifi_screen(const tb_app_t *a)
{
    return a->wifi_mode == TB_WIFI_SETUP || a->wifi_mode == TB_WIFI_CONNECTING || a->wifi_mode == TB_WIFI_CONNECTED ||
           a->wifi_mode == TB_WIFI_FAILED;
}

tb_showing_t tb_app_showing(const tb_app_t *a, const tb_clock_t *now)
{
    if (tb_app_on_wifi_screen(a)) return TB_SHOWING_SETUP;
    tb_auto_t k = tb_app_auto_top(a, now, NULL);
    return k == TB_AUTO_CALL ? TB_SHOWING_CALL : k == TB_AUTO_MEETING ? TB_SHOWING_MEETING : TB_SHOWING_OWN;
}

bool tb_app_cal_data(const tb_app_t *a)
{
    return a->cal_saved && a->set.automatic.calendar && a->wifi_mode != TB_WIFI_OFFLINE;
}

const tb_meeting_t *tb_app_current_meeting(const tb_app_t *a, const tb_clock_t *now)
{
    (void)a; (void)now;
    return NULL;    /* TODO(core): currentEvent() */
}

const tb_meeting_t *tb_app_next_meeting(const tb_app_t *a, const tb_clock_t *now)
{
    (void)a; (void)now;
    return NULL;    /* TODO(core): nextEvent() */
}

int tb_app_meetings_left(const tb_app_t *a, const tb_clock_t *now)
{
    (void)a; (void)now;
    return 0;       /* TODO(core): todays().length */
}

const char *tb_app_title_of(const tb_app_t *a, const tb_meeting_t *m)
{
    return m && a->set.automatic.meeting_titles && !m->priv ? m->title : "";
}

const char *tb_app_place_of(const tb_app_t *a, const tb_meeting_t *m)
{
    if (!m || !a->set.automatic.meeting_titles || m->priv || !m->location[0]) return "";
    if (strncmp(m->location, "http://", 7) == 0 || strncmp(m->location, "https://", 8) == 0) return "";  /* TODO(core): case-insensitive */
    return m->location;
}

bool tb_app_quiet(const tb_app_t *a, const tb_clock_t *now)
{
    (void)a; (void)now;
    return false;   /* TODO(core): quiet() */
}

bool tb_app_screen_free(const tb_app_t *a)
{
    return !a->booting && !a->off && a->menu.kind == TB_MENU_NONE && !tb_app_on_wifi_screen(a) &&
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
