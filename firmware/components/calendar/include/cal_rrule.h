/*
 * cal_rrule.h: RFC 5545 recurrence rules, the subset Google Calendar writes. Pure C.
 *
 * Owner: calendar builder.
 *
 * Required: FREQ=DAILY, WEEKLY (with BYDAY and WKST), MONTHLY, YEARLY; INTERVAL; COUNT; UNTIL (a DATE, a local
 * DATE-TIME or a UTC DATE-TIME). MONTHLY must also handle BYMONTHDAY=n and BYDAY with an ordinal (2TU, -1FR), which
 * Google writes for "monthly on the second Tuesday" and "on the last Friday". Anything else (BYSETPOS, BYWEEKNO,
 * BYYEARDAY, BYHOUR...) makes cal_rrule_parse() return false, and the event's first instance is used alone.
 *
 * Expansion works in the event's own local civil time (so a 9:00 weekly meeting stays at 9:00 across a DST change)
 * and starts at DTSTART, so COUNT is honored; it stops at the window's end, UNTIL or COUNT.
 */
#pragma once

#include "cal_tz.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CAL_FREQ_NONE = 0, CAL_FREQ_DAILY, CAL_FREQ_WEEKLY, CAL_FREQ_MONTHLY, CAL_FREQ_YEARLY } cal_freq_t;

typedef struct {
    cal_freq_t freq;
    uint16_t interval;          /* default 1 */
    int32_t count;              /* 0 = no COUNT */
    bool has_until;
    bool until_is_utc;          /* UNTIL ended in Z */
    cal_civil_t until;          /* UTC civil if until_is_utc, else local civil (a DATE has 23:59:59) */
    uint8_t byday_mask;         /* WEEKLY: bit d (0 = Sunday) */
    int8_t byday_ord[7];        /* MONTHLY/YEARLY BYDAY ordinals per weekday: 0 none, 1..5, -1..-5 */
    uint8_t n_bymonthday;
    int8_t bymonthday[8];       /* 1..31 or -1..-31 */
    uint8_t wkst;               /* 0 = Sunday ... default 1 = Monday */
} cal_rrule_t;

/* Parse an RRULE value ("FREQ=WEEKLY;BYDAY=MO,WE;UNTIL=20261231T235959Z"). False if unsupported. */
bool cal_rrule_parse(const char *value, cal_rrule_t *out);

/* Called for each occurrence's local start. Return false to stop early. */
typedef bool (*cal_occ_fn)(void *ctx, const cal_civil_t *local_start);

/*
 * Walk the occurrences of r from dtstart (local civil time in tz), calling fn for each one whose UTC start is before
 * window_end and whose UTC end (start + duration_s) is after window_start. Occurrences before the window are still
 * counted toward COUNT. Stops after max_steps candidate dates as a safety net against hostile feeds (100000 is
 * plenty: a daily rule from 1990 is under 14000). Returns the number of occurrences passed to fn.
 */
int cal_rrule_expand(const cal_rrule_t *r, const cal_civil_t *dtstart, const cal_tz_t *tz, int32_t duration_s,
                     tb_epoch_t window_start, tb_epoch_t window_end, uint32_t max_steps, cal_occ_fn fn, void *ctx);

#ifdef __cplusplus
}
#endif
