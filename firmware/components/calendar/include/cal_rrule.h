/*
 * cal_rrule.h: RFC 5545 recurrence rules, the subset calendar apps write. Pure C.
 *
 * Owner: calendar builder.
 *
 * Supported: FREQ=DAILY, WEEKLY, MONTHLY, YEARLY; INTERVAL; COUNT; UNTIL (a DATE, a local DATE-TIME or a UTC
 * DATE-TIME); WKST; BYDAY (plain days, and with an ordinal such as 2TU or -1FR for MONTHLY and YEARLY, as Google
 * writes for "monthly on the second Tuesday" and "on the last Friday"); BYMONTHDAY (1..31, -1..-31); BYMONTH; and
 * BYSETPOS (Outlook's "last weekday of the month" is BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1). Anything else (BYWEEKNO,
 * BYYEARDAY, BYHOUR, BYMINUTE, BYSECOND, FREQ=HOURLY and finer, RSCALE) makes cal_rrule_parse() return false, and
 * the event's first instance is used alone.
 *
 * Expansion works in the event's own local civil time (so a 9:00 weekly meeting stays at 9:00 across a DST change)
 * and starts at DTSTART, so COUNT is honored. DTSTART is always the first instance, as RFC 5545 says, even if it
 * doesn't match the rule. Expansion stops at the window's end, UNTIL or COUNT.
 */
#pragma once

#include "cal_tz.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAL_RRULE_BYDAY_MAX      16
#define CAL_RRULE_BYMONTHDAY_MAX 31
#define CAL_RRULE_BYSETPOS_MAX   8

typedef enum { CAL_FREQ_NONE = 0, CAL_FREQ_DAILY, CAL_FREQ_WEEKLY, CAL_FREQ_MONTHLY, CAL_FREQ_YEARLY } cal_freq_t;

typedef struct {
    cal_freq_t freq;
    uint16_t interval;          /* default 1 */
    int32_t count;              /* 0 = no COUNT */
    bool has_until;
    bool until_is_utc;          /* UNTIL ended in Z: compare instants */
    tb_epoch_t until_utc;       /* when until_is_utc */
    cal_civil_t until;          /* the local civil limit otherwise (a DATE becomes 23:59:59 that day) */
    uint8_t n_byday;
    struct { int8_t ord; uint8_t wd; } byday[CAL_RRULE_BYDAY_MAX];   /* ord 0 = every such weekday; wd 0 = Sunday */
    uint8_t n_bymonthday;
    int8_t bymonthday[CAL_RRULE_BYMONTHDAY_MAX];  /* 1..31 or -1..-31 */
    uint16_t bymonth_mask;      /* bit m for month m (1..12), 0 = none */
    uint8_t n_bysetpos;
    int16_t bysetpos[CAL_RRULE_BYSETPOS_MAX];     /* 1..366 or -1..-366 */
    uint8_t wkst;               /* 0 = Sunday ... default 1 = Monday */
} cal_rrule_t;

/* Parse an RRULE value ("FREQ=WEEKLY;BYDAY=MO,WE;UNTIL=20261231T235959Z"). Case-insensitive. False if malformed or
 * unsupported (out is then cleared). */
bool cal_rrule_parse(const char *value, cal_rrule_t *out);

/* Called for each occurrence's local start (in the event's zone). Return false to stop early. */
typedef bool (*cal_occ_fn)(void *ctx, const cal_civil_t *local_start);

/* Called now and then while the calendar code works (about every CAL_TICK_DAYS days a walk examines, and every
 * CAL_TICK_BYTES bytes the ICS reader takes), so the device can give the CPU away on elapsed time: cal_sync blocks
 * for a tick after 50 ms of work, which keeps core 0's idle task (and the task watchdog) fed through a hostile feed.
 * The pure code never blocks or reads a clock itself. */
typedef void (*cal_tick_fn)(void *ctx);
#define CAL_TICK_DAYS  512
#define CAL_TICK_BYTES 1024

/*
 * Walk the occurrences of r from dtstart (local civil time in tz), calling fn for each one whose UTC start is before
 * window_end and whose UTC end (start + duration_s) is after window_start. Occurrences before the window are still
 * counted toward COUNT. Without COUNT, whole periods before the window are skipped arithmetically, so a daily rule
 * from 1990 costs nothing. *steps (optional, in and out) is a budget of days examined, a safety net against hostile
 * feeds that bounds the CPU time: each period costs the days it looks at (a DAILY one 1, a WEEKLY one 7, a MONTHLY
 * one with BYDAY or BYMONTHDAY the days of its month, a YEARLY one with BYDAY a whole year; at least 1), and the walk
 * stops when it reaches 0. Returns the number of occurrences passed to fn, or -1 if the budget ran out first
 * (occurrences found before that were passed).
 */
int cal_rrule_expand(const cal_rrule_t *r, const cal_civil_t *dtstart, const cal_tz_t *tz, int32_t duration_s,
                     tb_epoch_t window_start, tb_epoch_t window_end, uint32_t *steps, cal_occ_fn fn, void *ctx);
/* The same, calling tick (if not NULL) every CAL_TICK_DAYS days examined. */
int cal_rrule_expand_ex(const cal_rrule_t *r, const cal_civil_t *dtstart, const cal_tz_t *tz, int32_t duration_s,
                        tb_epoch_t window_start, tb_epoch_t window_end, uint32_t *steps, cal_tick_fn tick,
                        void *tick_ctx, cal_occ_fn fn, void *ctx);

#ifdef __cplusplus
}
#endif
