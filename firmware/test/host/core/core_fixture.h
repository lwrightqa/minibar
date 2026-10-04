/*
 * core_fixture.h: a bar on the bench for core's host tests. Owner: core builder.
 *
 * One tb_app_t driven the way main/app_task.c drives it: inputs, then tb_app_pointer_poll() and tb_app_tick() every
 * 50 ms of fake time, with every effect collected into a log the tests can search. The clock starts on
 * Sunday 2026-10-04 at 10:00 PDT (the runner sets TZ to US Pacific).
 */
#pragma once

#include "tb_app.h"
#include "tb_fmt.h"
#include "tb_test.h"

#define T0_WALL 1791133200      /* 2026-10-04 10:00:00 PDT */
#define T0_MONO 10000
#define FX_LOG_MAX 4096

typedef struct {
    tb_app_t a;
    tb_clock_t now;
    tb_effect_t log[FX_LOG_MAX];
    int nlog;
    uint32_t call_seq;
} bench_t;

/* A fresh bar: defaults, Wi-Fi configured (unless setup is asked for), past the splash with the "Ready" toast gone
 * and the effect log cleared. Status Busy unless the test changes it. */
bench_t *bench_new(void);
bench_t *bench_new_opts(bool wifi_configured, bool skip_splash);
/* Run the app task's loop for ms of fake time, in 50 ms steps. */
void bench_run(bench_t *b, tb_ms_t ms);
/* Move the clock without running the loop (for testing late samples). */
void bench_jump(bench_t *b, tb_ms_t ms);
/* Collect pending effects into the log (the inputs below do it themselves). */
void bench_drain(bench_t *b);
void bench_clear_log(bench_t *b);
/* How many effects of this kind were logged; the arg of the last one (or -999). */
int fx_count(const bench_t *b, tb_effect_kind_t k);
int32_t fx_last(const bench_t *b, tb_effect_kind_t k);

/* Touch and buttons, as the ui and board report them. */
void tap(bench_t *b);
void tap_tile(bench_t *b, int tile);
void tap_tile_named(bench_t *b, tb_action_t act);   /* the tile with this action (fails the test if none) */
void swipe(bench_t *b, int dx);                      /* dx < 0: towards the left (next status) */
void hold(bench_t *b);
void boot_btn(bench_t *b);
void pwr_press(bench_t *b);
void pwr_hold(bench_t *b, tb_ms_t ms);
void flip(bench_t *b);

/* Remote shortcuts. */
void go_status(bench_t *b, tb_status_t st);         /* the Remote's status button, set_aside true */

/* The Mac: a new call (fresh id), the same call repeating, or none. */
void call_start(bench_t *b, const char *app);
void call_end(bench_t *b, const char *lead);

/* The calendar: an address saved, with meetings given as minutes from now and lengths. */
typedef struct {
    int start_min, len_min;
    const char *title;
    const char *loc;
    bool priv;
} mt_t;
void cal_save(bench_t *b, const mt_t *m, int n);
void cal_meetings(bench_t *b, const mt_t *m, int n);   /* replace the list (keeps the address) */

/* The Pomodoro: start a focus session from a fresh bar (tap on the Pomodoro screen). */
void pomo_start(bench_t *b);

const tb_tile_t *tile_with(const bench_t *b, tb_action_t act);
int tile_index(const bench_t *b, tb_action_t act);
void tb_strlcpy_test(char *dst, const char *src, size_t cap);

#define POMO_FOCUS_MS (25 * 60 * 1000)
#define POMO_SHORT_MS (5 * 60 * 1000)
#define POMO_LONG_MS (15 * 60 * 1000)
