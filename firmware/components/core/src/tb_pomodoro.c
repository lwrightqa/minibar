/*
 * tb_pomodoro.c: the Pomodoro timer. Owner: core builder.
 *
 * A port of the mock-up's `pomo` object and its helpers (phaseLen, isReady, pomoActive, remSec, nextPhase,
 * startWaiting, resetPomo, tomatoRow, the Remote's length segments, and the timer part of loop()). The alarm, the
 * screens and the automatic sources are one level up, in tb_app.c.
 */
#include <string.h>

#include "tb_pomodoro.h"

tb_ms_t tb_pomo_phase_len(const tb_settings_t *s, tb_phase_t ph)
{
    int min = ph == TB_PH_FOCUS ? s->pomodoro.focus_min : ph == TB_PH_SHORT ? s->pomodoro.short_min : s->pomodoro.long_min;
    return (tb_ms_t)min * 60000;
}

void tb_pomo_init(tb_pomo_t *p, const tb_settings_t *s, int32_t today_yyyymmdd)
{
    memset(p, 0, sizeof(*p));
    p->phase = TB_PH_FOCUS;
    p->round = 1;
    p->remaining_ms = tb_pomo_phase_len(s, TB_PH_FOCUS);
    p->just_ended = -1;
    p->day = today_yyyymmdd;
}

/* isReady(): not running, not waiting, focus, and the full focus length left. */
bool tb_pomo_is_ready(const tb_pomo_t *p, const tb_settings_t *s)
{
    return !p->running && !p->waiting && p->phase == TB_PH_FOCUS && p->remaining_ms == tb_pomo_phase_len(s, TB_PH_FOCUS);
}

/* pomoActive() */
bool tb_pomo_active(const tb_pomo_t *p, const tb_settings_t *s)
{
    return p->running || p->waiting || !tb_pomo_is_ready(p, s);
}

bool tb_pomo_mid_phase(const tb_pomo_t *p, const tb_settings_t *s)
{
    return !p->waiting && !tb_pomo_is_ready(p, s);
}

tb_pomo_state_t tb_pomo_state(const tb_pomo_t *p, const tb_settings_t *s)
{
    if (p->waiting) return TB_POMO_ST_WAITING;
    if (p->running) return TB_POMO_ST_RUNNING;
    if (tb_pomo_is_ready(p, s)) return TB_POMO_ST_READY;
    return TB_POMO_ST_PAUSED;
}

/* remSec(): Math.max(0, Math.ceil(remaining / 1000)) */
int32_t tb_pomo_remaining_s(const tb_pomo_t *p)
{
    if (p->remaining_ms <= 0) return 0;
    return (int32_t)((p->remaining_ms + 999) / 1000);
}

/*
 * nextPhase(skipped): a focus that ends counts a tomato (or, skipped, leaves its tomato pale); every long_every-th
 * focus is followed by the long break; a long break starts the set over at round 1 and forgets the skipped rounds.
 */
tb_phase_t tb_pomo_next_phase(tb_pomo_t *p, const tb_settings_t *s, bool skipped)
{
    tb_phase_t ended = p->phase;
    if (ended == TB_PH_FOCUS) {
        if (!skipped) p->done_today++;
        else if (p->round >= 1 && p->round <= TB_POMO_MAX_ROUNDS) p->skipped_mask |= (uint8_t)(1u << (p->round - 1));
        p->phase = p->round % s->pomodoro.long_every == 0 ? TB_PH_LONG : TB_PH_SHORT;
    } else {
        p->round = ended == TB_PH_LONG ? 1 : (uint8_t)(p->round + 1);
        if (ended == TB_PH_LONG) p->skipped_mask = 0;
        p->phase = TB_PH_FOCUS;
    }
    p->remaining_ms = tb_pomo_phase_len(s, p->phase);
    return ended;
}

/* startWaiting(): returns NOTHING when the timer was already running. */
tb_pomo_start_t tb_pomo_start_waiting(tb_pomo_t *p, const tb_settings_t *s)
{
    if (p->running) return TB_POMO_NOTHING;
    tb_pomo_start_t verb = p->waiting || tb_pomo_is_ready(p, s) ? TB_POMO_STARTED : TB_POMO_RESUMED;
    p->waiting = false;
    p->just_ended = -1;
    p->running = true;
    p->auto_paused = TB_AUTO_NONE;
    return verb;
}

/* resetPomo() (and the reset in stop()): the run ends; today's tomatoes and focused time stay. */
void tb_pomo_reset_run(tb_pomo_t *p, const tb_settings_t *s)
{
    p->phase = TB_PH_FOCUS;
    p->round = 1;
    p->skipped_mask = 0;
    p->remaining_ms = tb_pomo_phase_len(s, TB_PH_FOCUS);
    p->running = false;
    p->waiting = false;
    p->just_ended = -1;
    p->auto_paused = TB_AUTO_NONE;
    p->held_alarm = TB_AUTO_NONE;
}

/*
 * The timer part of loop():
 *   pomo.remaining -= d;
 *   if (pomo.phase === 'focus') pomo.focusedMs += Math.min(d, d + pomo.remaining);   // no credit for the overshoot
 *   if (pomo.remaining <= 0) endPhase();
 */
bool tb_pomo_advance(tb_pomo_t *p, const tb_settings_t *s, tb_ms_t dt)
{
    (void)s;
    if (!p->running || dt <= 0) return false;
    p->remaining_ms -= dt;
    if (p->phase == TB_PH_FOCUS) {
        tb_ms_t credit = dt + p->remaining_ms < dt ? dt + p->remaining_ms : dt;
        if (credit > 0) p->focused_ms += credit;
    }
    return p->remaining_ms <= 0;
}

bool tb_pomo_roll_day(tb_pomo_t *p, int32_t today_yyyymmdd)
{
    if (today_yyyymmdd == 0 || p->day == today_yyyymmdd) return false;
    if (p->day != 0) {
        p->done_today = 0;
        p->focused_ms = 0;
    }
    p->day = today_yyyymmdd;
    return true;
}

/*
 * The Remote's length segments (api.md 10.2):
 *   every: if (pomo.round > v) pomo.round = v; pomo.skipped = pomo.skipped.filter(r => r <= v);
 *   a length: if (pomo.phase === k) pomo.remaining = phaseLen(k);   // the current phase restarts at the new length
 * Only real changes count, so repeating the same PATCH doesn't restart the phase.
 */
void tb_pomo_settings_changed(tb_pomo_t *p, const tb_settings_t *old_s, const tb_settings_t *new_s)
{
    const uint8_t olds[3] = {old_s->pomodoro.focus_min, old_s->pomodoro.short_min, old_s->pomodoro.long_min};
    const uint8_t news[3] = {new_s->pomodoro.focus_min, new_s->pomodoro.short_min, new_s->pomodoro.long_min};
    for (int k = 0; k < 3; k++)
        if (olds[k] != news[k] && p->phase == (tb_phase_t)k) p->remaining_ms = tb_pomo_phase_len(new_s, (tb_phase_t)k);
    uint8_t every = new_s->pomodoro.long_every;
    if (every != old_s->pomodoro.long_every) {
        if (p->round > every) p->round = every;
        if (every < 8) p->skipped_mask &= (uint8_t)((1u << every) - 1);
    }
}

static uint8_t frame_of(double t)
{
    if (!(t > 0)) t = 0;        /* also catches NaN */
    if (t > 1) t = 1;
    return (uint8_t)(t * (TB_RIPEN_FRAMES - 1) + 0.5);   /* Math.round(t * (RIPEN_FRAMES - 1)); t >= 0, so the cast floors */
}

/* The share of the focus done: 1 - pomo.remaining / phaseLen('focus') (the mock-up uses the focus length even in a
 * break, which is harmless there because a break's tomato is drawn ripe). */
static double focus_progress(const tb_pomo_t *p, const tb_settings_t *s)
{
    tb_ms_t len = tb_pomo_phase_len(s, TB_PH_FOCUS);
    return len > 0 ? 1.0 - (double)p->remaining_ms / (double)len : 0.0;
}

/*
 * tomatoRow():
 *   for (let k = 1; k <= set.every; k++) {
 *     if (k < round || (k === round && phase !== 'focus')) out += tomato(pomo.skipped.includes(k) ? 'ghost' : 'ripe');
 *     else if (k === round) out += tomato('ripening', pomo.waiting ? 0 : p);
 *     else out += tomato('ghost');
 *   }
 */
int tb_pomo_tomatoes(const tb_pomo_t *p, const tb_settings_t *s, tb_tomato_t out[TB_POMO_MAX_ROUNDS])
{
    int n = s->pomodoro.long_every;
    if (n > TB_POMO_MAX_ROUNDS) n = TB_POMO_MAX_ROUNDS;
    double prog = focus_progress(p, s);
    for (int k = 1; k <= n; k++) {
        tb_tomato_t t = {TB_TOMATO_GHOST, 0};
        if (k < p->round || (k == p->round && p->phase != TB_PH_FOCUS))
            t.kind = p->skipped_mask & (1u << (k - 1)) ? TB_TOMATO_GHOST : TB_TOMATO_RIPE;
        else if (k == p->round) {
            t.kind = TB_TOMATO_RIPENING;
            t.frame = frame_of(p->waiting ? 0 : prog);
        }
        out[k - 1] = t;
    }
    return n;
}

/* The corner pill's tomato: tomato('ripening', 1 - pomo.remaining / phaseLen('focus')) in focus (waiting or not). */
uint8_t tb_pomo_ripen_frame(const tb_pomo_t *p, const tb_settings_t *s)
{
    return frame_of(focus_progress(p, s));
}

const char *tb_phase_name(tb_phase_t ph)
{
    return ph == TB_PH_FOCUS ? "Focus" : ph == TB_PH_SHORT ? "Short break" : "Long break";
}
