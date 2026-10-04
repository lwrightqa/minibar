/*
 * cal_ics.h: a streaming iCalendar (RFC 5545) reader that turns a feed into today's and tomorrow's meetings, without
 * ever holding the whole file. Google's secret feed carries every past event and can run to megabytes (api.md 15).
 * Pure C: the HTTPS fetch (cal_sync.h) pushes the body through cal_feed_write() as it arrives.
 *
 * Owner: calendar builder.
 *
 * What it must handle:
 *   - CRLF or LF line ends; line unfolding (a line starting with a space or tab continues the previous one);
 *     property parameters (;TZID=...;VALUE=DATE), quoted parameter values, escaped text (\n \, \; \\).
 *   - VEVENT inside VCALENDAR; VTIMEZONE blocks are skipped (TZIDs resolve through cal_tz_posix_for(); an unknown
 *     TZID falls back to the device zone, and is counted in stats).
 *   - DTSTART / DTEND / DURATION as UTC (…Z), with TZID, floating (device zone), or all-day (VALUE=DATE: ignored,
 *     decisions.md "which calendar events count").
 *   - RRULE (cal_rrule.h), EXDATE (several lines, several values each, any of the three forms), RECURRENCE-ID
 *     overrides (in any order relative to their master; an override replaces that instance, may move it into or out
 *     of the window, or cancel it with STATUS:CANCELLED).
 *   - What counts (decisions.md): timed events, not TRANSP:TRANSPARENT ("Free"), not STATUS:CANCELLED, and not
 *     declined by you (the ATTENDEE whose mailto: matches self_email has PARTSTAT=DECLINED). CLASS:PRIVATE or
 *     CONFIDENTIAL marks the meeting private (no title or location ever shown).
 *   - Long lines: only the properties above are buffered (up to CAL_LINE_MAX bytes, then cut); everything else,
 *     including huge DESCRIPTIONs and ATTACHments, streams past without being stored.
 *   - Garbage: a feed that never shows BEGIN:VCALENDAR in its first 4 KB is "not_a_calendar".
 */
#pragma once

#include "cal_rrule.h"
#include "cal_tz.h"
#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAL_LINE_MAX        1024    /* longest kept logical line */
#define CAL_CANDIDATES_MAX  64      /* instances in the window before overrides are applied */
#define CAL_OVERRIDES_MAX   256     /* RECURRENCE-ID keys remembered (8 bytes each) */
#define CAL_EXDATES_MAX     64      /* per event */

typedef enum {
    CAL_OK = 0,
    CAL_ERR_NOT_A_CALENDAR,     /* api.md 11.2 not_a_calendar */
    CAL_ERR_NO_MEMORY,
} cal_err_t;

typedef struct {
    uint32_t events;            /* VEVENTs seen */
    uint32_t recurring;
    uint32_t unsupported_rrule;
    uint32_t unknown_tzid;
    uint32_t cut_lines;
    uint32_t dropped_candidates;    /* window had more than CAL_CANDIDATES_MAX instances */
} cal_stats_t;

/* Opaque-ish reader state (large: allocate it, in PSRAM on the device). */
typedef struct cal_feed cal_feed_t;

/*
 * Start reading a feed. window_start..window_end is in UTC (main/calendar use local midnight today to local
 * midnight after tomorrow). device_tz: the bar's zone, for floating times and unknown TZIDs. self_email: the
 * calendar owner's address, for PARTSTAT (cal_url_info_t.self_email), or "" if unknown.
 */
cal_feed_t *cal_feed_new(tb_epoch_t window_start, tb_epoch_t window_end, const cal_tz_t *device_tz, const char *self_email);
void cal_feed_free(cal_feed_t *f);

/* Feed the next chunk of the body (any size, any split, even mid-character). */
cal_err_t cal_feed_write(cal_feed_t *f, const char *data, size_t len);

/* End of body: apply overrides and EXDATEs, keep what counts, sort by start, and copy at most max meetings into out.
 * Returns CAL_OK and sets *n, or CAL_ERR_NOT_A_CALENDAR. */
cal_err_t cal_feed_finish(cal_feed_t *f, tb_meeting_t *out, int max, int *n);

const cal_stats_t *cal_feed_stats(const cal_feed_t *f);

/* The id a meeting instance gets: FNV-1a of the UID, then of the instance's UTC start. Never 0. */
uint32_t cal_instance_id(const char *uid, tb_epoch_t start);

#ifdef __cplusplus
}
#endif
