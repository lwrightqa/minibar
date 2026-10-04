/*
 * tb_pomodoro.c: the Pomodoro timer. Owner: core builder.
 * Skeleton: init, lengths and names are real; the rest are stubs to port from the mock-up (see tb_pomodoro.h).
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

bool tb_pomo_is_ready(const tb_pomo_t *p, const tb_settings_t *s)
{
    return !p->running && !p->waiting && p->phase == TB_PH_FOCUS && p->remaining_ms == tb_pomo_phase_len(s, TB_PH_FOCUS);
}

bool tb_pomo_active(const tb_pomo_t *p, const tb_settings_t *s)
{
    return p->running || p->waiting || !tb_pomo_is_ready(p, s);
}

int32_t tb_pomo_remaining_s(const tb_pomo_t *p)
{
    if (p->remaining_ms <= 0) return 0;
    return (int32_t)((p->remaining_ms + 999) / 1000);
}

tb_phase_t tb_pomo_next_phase(tb_pomo_t *p, const tb_settings_t *s, bool skipped)
{
    (void)s;
    (void)skipped;
    return p->phase;    /* TODO(core): nextPhase() */
}

tb_pomo_start_t tb_pomo_start_waiting(tb_pomo_t *p, const tb_settings_t *s)
{
    (void)p;
    (void)s;
    return TB_POMO_NOTHING; /* TODO(core): startWaiting() */
}

void tb_pomo_reset_run(tb_pomo_t *p, const tb_settings_t *s)
{
    (void)p;
    (void)s;    /* TODO(core): resetPomo(), keeping done_today and focused_ms */
}

bool tb_pomo_advance(tb_pomo_t *p, const tb_settings_t *s, tb_ms_t dt)
{
    (void)p;
    (void)s;
    (void)dt;
    return false;   /* TODO(core) */
}

void tb_pomo_roll_day(tb_pomo_t *p, int32_t today_yyyymmdd)
{
    (void)p;
    (void)today_yyyymmdd;   /* TODO(core) */
}

void tb_pomo_settings_changed(tb_pomo_t *p, const tb_settings_t *old_s, const tb_settings_t *new_s)
{
    (void)p;
    (void)old_s;
    (void)new_s;    /* TODO(core) */
}

int tb_pomo_tomatoes(const tb_pomo_t *p, const tb_settings_t *s, tb_tomato_t out[TB_POMO_MAX_ROUNDS])
{
    (void)p;
    int n = s->pomodoro.long_every;
    if (n > TB_POMO_MAX_ROUNDS) n = TB_POMO_MAX_ROUNDS;
    for (int i = 0; i < n; i++) out[i] = (tb_tomato_t){TB_TOMATO_GHOST, 0};   /* TODO(core): tomatoRow() */
    return n;
}

uint8_t tb_pomo_ripen_frame(const tb_pomo_t *p, const tb_settings_t *s)
{
    (void)p;
    (void)s;
    return 0;   /* TODO(core) */
}

const char *tb_phase_name(tb_phase_t ph)
{
    return ph == TB_PH_FOCUS ? "Focus" : ph == TB_PH_SHORT ? "Short break" : "Long break";
}
