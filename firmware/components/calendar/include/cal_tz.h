/*
 * cal_tz.h: civil dates and time zones without the C library's global TZ, so the calendar can convert an event's
 * TZID while the screen keeps the device's own zone. Pure C.
 *
 * Owner: calendar builder.
 *
 * Zones are POSIX TZ rules ("PST8PDT,M3.2.0,M11.1.0"). IANA names (what Google's TZID and the API's time_zone use)
 * map to them through a built-in table generated from posix_tz_db (api.md section 15), trimmed if flash gets tight.
 * main also uses cal_tz_posix_for() to set the device zone: setenv("TZ", posix, 1); tzset().
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

/* A parsed POSIX TZ rule. */
typedef struct {
    int32_t std_offset;         /* seconds EAST of UTC (note: POSIX strings write west, "PST8" is -28800) */
    int32_t dst_offset;
    bool has_dst;
    struct { uint8_t m, w, d; int32_t secs; } start, end;   /* Mm.w.d/time rules; Jn and n forms are rare, see .c */
} cal_tz_t;

/* Days since 1970-01-01 for a proleptic Gregorian date, and back. */
int32_t cal_days_from_civil(int y, int m, int d);
void cal_civil_from_days(int32_t days, int *y, int *m, int *d);
/* 0 = Sunday. */
int cal_weekday(int y, int m, int d);
int cal_days_in_month(int y, int m);

/* Parse a POSIX TZ string. Returns false if it isn't one we understand (the caller falls back to UTC). */
bool cal_tz_parse(const char *posix, cal_tz_t *out);
/* The POSIX rule for an IANA name ("America/Los_Angeles", "UTC", "Etc/UTC"), or NULL if not in the table. */
const char *cal_tz_posix_for(const char *iana);
/* The offset in effect at a UTC instant, in seconds east of UTC. */
int32_t cal_tz_offset_at(const cal_tz_t *tz, tb_epoch_t utc);
/* Local civil time in tz to UTC. In a spring-forward gap the time is moved forward by the gap; in a fall-back
 * overlap the first (daylight) instance is used (RFC 5545 3.3.5). */
tb_epoch_t cal_tz_to_utc(const cal_tz_t *tz, const cal_civil_t *local);
/* UTC to local civil time in tz. */
void cal_tz_to_local(const cal_tz_t *tz, tb_epoch_t utc, cal_civil_t *out);

#ifdef __cplusplus
}
#endif
