/*
 * cal_tz.h: civil dates and time zones without the C library's global TZ, so the calendar can convert an event's
 * TZID while the screen keeps the device's own zone. Pure C.
 *
 * Owner: calendar builder.
 *
 * Zones are POSIX TZ rules ("PST8PDT,M3.2.0,M11.1.0"). IANA names (what Google's TZID and the API's time_zone use)
 * map to them through a built-in table generated from IANA tzdata by tools/gen_tz_table.py (the POSIX footer of each
 * zone's TZif file, the same source posix_tz_db uses; about 12 KB of flash for 597 names).
 * main also uses cal_tz_posix_for() to set the device zone: setenv("TZ", posix, 1); tzset().
 *
 * A POSIX rule describes a zone from its last change of rules on, so dates before that (say, a US date before 2007)
 * convert with today's rules. The bar only looks at today and tomorrow, so that never matters to it.
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t year;
    int8_t month;       /* 1..12 */
    int8_t day;         /* 1..31 */
    int8_t hour, min, sec;
} cal_civil_t;

/* One of a POSIX rule's two yearly changes: Mm.w.d (month, week 1..5 where 5 = last, weekday 0 = Sunday), Jn (day
 * 1..365, February 29 never counted) or n (day 0..365, counting February 29), at secs after local midnight in the
 * time in effect before the change (POSIX allows -167 to 167 hours: "M3.4.4/26" is 2 AM on the Friday). */
typedef enum { CAL_TZ_RULE_M = 0, CAL_TZ_RULE_J, CAL_TZ_RULE_N } cal_tz_rule_kind_t;

typedef struct {
    uint8_t kind;           /* cal_tz_rule_kind_t */
    uint8_t m, w, d;        /* kind M */
    uint16_t yday;          /* kind J (1..365) or N (0..365) */
    int32_t secs;
} cal_tz_rule_t;

/* A parsed POSIX TZ rule. */
typedef struct {
    int32_t std_offset;     /* seconds EAST of UTC (note: POSIX strings write west, "PST8" is -28800) */
    int32_t dst_offset;     /* the other period's offset; usually std + 1 h, but Europe/Dublin's is std - 1 h */
    bool has_dst;
    cal_tz_rule_t start, end;   /* start: std -> dst; end: dst -> std */
} cal_tz_t;

/* Days since 1970-01-01 for a proleptic Gregorian date, and back. */
int32_t cal_days_from_civil(int y, int m, int d);
void cal_civil_from_days(int32_t days, int *y, int *m, int *d);
/* 0 = Sunday. */
int cal_weekday(int y, int m, int d);
int cal_days_in_month(int y, int m);
bool cal_is_leap(int y);

/* Civil time <-> seconds, ignoring zones (the civil time read as if it were UTC). */
tb_epoch_t cal_civil_to_secs(const cal_civil_t *c);
void cal_secs_to_civil(tb_epoch_t t, cal_civil_t *out);
/* -1, 0, 1. */
int cal_civil_cmp(const cal_civil_t *a, const cal_civil_t *b);
/* A civil date and time is real (month 1..12, day within the month, 00:00:00..23:59:59). */
bool cal_civil_valid(const cal_civil_t *c);

/* The zone at UTC, for fallbacks. */
void cal_tz_utc(cal_tz_t *out);
/* Parse a POSIX TZ string. Returns false if it isn't one we understand (out is then UTC). A zone name without rules
 * ("EST5EDT") gets the US rules, as glibc does. */
bool cal_tz_parse(const char *posix, cal_tz_t *out);
/* The POSIX rule for an IANA name ("America/Los_Angeles", "UTC", "Etc/UTC", links like "US/Pacific"), or NULL if not
 * in the table. Exact names are found by binary search; other capitalizations ("america/new_york") are accepted too. */
const char *cal_tz_posix_for(const char *iana);
/* The table, for tests and tools: number of names, and the i-th name and rule (NULL past the end). */
int cal_tz_table_size(void);
const char *cal_tz_table_name(int i, const char **posix);
/* Both of the year's change-overs as UTC instants (has_dst only). */
void cal_tz_transitions(const cal_tz_t *tz, int year, tb_epoch_t *dst_start, tb_epoch_t *dst_end);
/* The offset in effect at a UTC instant, in seconds east of UTC. */
int32_t cal_tz_offset_at(const cal_tz_t *tz, tb_epoch_t utc);
/* Local civil time in tz to UTC. In a gap (spring forward) the time is read with the offset in effect before the
 * gap, so it moves forward by the gap's length (2:30 becomes 3:30); in an overlap (fall back) the earlier of the two
 * instants is used. Both as RFC 5545 3.3.5 says. */
tb_epoch_t cal_tz_to_utc(const cal_tz_t *tz, const cal_civil_t *local);
/* UTC to local civil time in tz. */
void cal_tz_to_local(const cal_tz_t *tz, tb_epoch_t utc, cal_civil_t *out);
/* The UTC instant of local midnight starting the local day that contains utc (a day is 23 to 25 hours long; if
 * midnight itself falls in a gap, the first instant of the day). */
tb_epoch_t cal_tz_midnight(const cal_tz_t *tz, tb_epoch_t utc);
/* Local midnight days_ahead days after the day containing utc (0 = the same day). */
tb_epoch_t cal_tz_midnight_after(const cal_tz_t *tz, tb_epoch_t utc, int days_ahead);

#ifdef __cplusplus
}
#endif
