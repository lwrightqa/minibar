/*
 * tb_pomodoro.h: the Pomodoro timer, a port of the mock-up's `pomo` object and its helpers
 * (phaseLen, isReady, pomoActive, nextPhase, startWaiting, remSec, tomatoRow, resetPomo).
 *
 * Owner: core builder. Pure C.
 *
 * The alarm (ringing, its 4 s repeats and 1 min limit), what a tap or flip does, and how calls and meetings pause the
 * timer live one level up in tb_app.c, because they involve the screen and the automatic sources. This module is the
 * timer itself. Lengths come from tb_settings_t.pomodoro, read at the moment they're needed, as the mock-up reads
 * pomo.set.
 */
#pragma once

#include "tb_settings.h"
#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TB_RIPEN_FRAMES 24      /* mock-up RIPEN_FRAMES; ui ships the same 24 frames as images */
#define TB_POMO_MAX_ROUNDS 8    /* long_every is at most 8 */

typedef struct {
    tb_phase_t phase;
    uint8_t round;              /* 1..long_every: "focus 2 of 4" */
    tb_ms_t remaining_ms;
    bool running;
    bool waiting;               /* a phase ended and the next one waits for a start (alarm screens) */
    int8_t just_ended;          /* the phase that just ended while waiting (tb_phase_t), or -1 */
    uint16_t done_today;        /* finished focus sessions today: the red tomatoes */
    uint8_t skipped_mask;       /* bit (round - 1): focus rounds of this set that were skipped; their tomatoes stay pale */
    tb_ms_t focused_ms;         /* focused time today */
    tb_auto_t auto_paused;      /* the call or meeting that paused the timer, or NONE */
    tb_auto_t held_alarm;       /* the call or meeting that silenced a ringing alarm (it rings once when it's over) */
    int32_t day;                /* local date the tallies belong to, yyyymmdd; they reset at local midnight */
} tb_pomo_t;

/* A finished, upcoming or ripening tomato in the Today row (mock-up tomatoRow()). */
typedef enum { TB_TOMATO_RIPE = 0, TB_TOMATO_GHOST, TB_TOMATO_RIPENING } tb_tomato_kind_t;
typedef struct {
    tb_tomato_kind_t kind;
    uint8_t frame;              /* TB_TOMATO_RIPENING only: 0..TB_RIPEN_FRAMES-1, round(p * 23) */
} tb_tomato_t;

/* What tb_pomo_start_waiting() did, for the toast ("Focus started", "Short break resumed"). */
typedef enum { TB_POMO_NOTHING = 0, TB_POMO_STARTED, TB_POMO_RESUMED } tb_pomo_start_t;

/* A fresh, ready timer (focus 1, full length, not running), with today's tallies zeroed. */
void tb_pomo_init(tb_pomo_t *p, const tb_settings_t *s, int32_t today_yyyymmdd);

tb_ms_t tb_pomo_phase_len(const tb_settings_t *s, tb_phase_t ph);
/* isReady(): not running, not waiting, focus, and the full focus length left. */
bool tb_pomo_is_ready(const tb_pomo_t *p, const tb_settings_t *s);
/* pomoActive(): running, waiting, or anything other than ready (drives the corner pill off the Pomodoro screen). */
bool tb_pomo_active(const tb_pomo_t *p, const tb_settings_t *s);
/* remSec(): whole seconds left, rounded up, never negative. */
int32_t tb_pomo_remaining_s(const tb_pomo_t *p);

/* nextPhase(skipped): move to the next phase at its full length; counts a finished focus (or marks it skipped).
 * Returns the phase that ended. */
tb_phase_t tb_pomo_next_phase(tb_pomo_t *p, const tb_settings_t *s, bool skipped);

/* startWaiting(): start the phase that's waiting, a fresh session, or resume a paused timer. Clears auto_paused. */
tb_pomo_start_t tb_pomo_start_waiting(tb_pomo_t *p, const tb_settings_t *s);

/* Stop and power-off: back to focus 1, ready; today's tomatoes and focused time are kept (mock-up resetPomo()). */
void tb_pomo_reset_run(tb_pomo_t *p, const tb_settings_t *s);

/* Run the clock for dt ms if running; adds focused time. Returns true when the phase reached zero this call
 * (the caller then runs the end-of-phase logic, mock-up endPhase()). */
bool tb_pomo_advance(tb_pomo_t *p, const tb_settings_t *s, tb_ms_t dt);

/* Zero today's tallies when the local date changes (not in the mock-up, which never sees midnight). */
void tb_pomo_roll_day(tb_pomo_t *p, int32_t today_yyyymmdd);

/* Settings changed (Remote segments, api.md 10.2): a new length for the running phase restarts it at that length;
 * a long_every below the current round moves the round down and forgets skipped rounds above it. */
void tb_pomo_settings_changed(tb_pomo_t *p, const tb_settings_t *old_s, const tb_settings_t *new_s);

/* tomatoRow(): one entry per round of the set (long_every entries). Returns the count. */
int tb_pomo_tomatoes(const tb_pomo_t *p, const tb_settings_t *s, tb_tomato_t out[TB_POMO_MAX_ROUNDS]);

/* The ripening frame for the corner pill and the current tomato: round((1 - remaining/focus_len) * 23). */
uint8_t tb_pomo_ripen_frame(const tb_pomo_t *p, const tb_settings_t *s);

/* "Focus", "Short break", "Long break" (PHASE_NAME). */
const char *tb_phase_name(tb_phase_t ph);

#ifdef __cplusplus
}
#endif
