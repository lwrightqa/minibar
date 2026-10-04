/*
 * cal_ics.h: a streaming iCalendar (RFC 5545) reader that turns a feed into the meetings of a short window (today and
 * tomorrow), without ever holding the whole file. Google's secret feed carries every past event and can run to
 * megabytes (api.md 15). Pure C: the HTTPS fetch (cal_sync.h) pushes the body through cal_feed_write() as it arrives.
 *
 * Owner: calendar builder.
 *
 * What it handles:
 *   - CRLF or LF line ends; line unfolding (a line starting with a space or tab continues the previous one, even in
 *     the middle of a UTF-8 character); a UTF-8 byte-order mark; property parameters (;TZID=...;VALUE=DATE), quoted
 *     parameter values, escaped text (\n \N \, \; \\).
 *   - VEVENT inside VCALENDAR. Nested components (VALARM inside an event, VTODO, VJOURNAL...) are skipped whole, so an
 *     alarm's ATTENDEE or DESCRIPTION never counts as the event's.
 *   - Time zones: a TZID resolves through the IANA table (cal_tz_posix_for(); Google and Apple write IANA names), then
 *     through the feed's own VTIMEZONE blocks (Outlook writes Windows names such as "Pacific Standard Time" with a
 *     VTIMEZONE whose yearly STANDARD and DAYLIGHT rules become a POSIX-style rule), then with any "/prefix/" taken
 *     off ("/mozilla.org/20050126_1/Europe/London"). A TZID none of these know uses the calendar's zone and is counted
 *     in stats. The calendar's zone is X-WR-TIMEZONE when the feed names a known one, else the device zone; floating
 *     times (no TZID, no Z) use it too.
 *   - DTSTART / DTEND / DURATION as UTC (...Z), with TZID, floating, or all-day (VALUE=DATE: ignored, decisions.md
 *     "which calendar events count").
 *   - RRULE (cal_rrule.h); EXDATE (several lines, several values each, UTC, TZID, floating or DATE); RECURRENCE-ID
 *     overrides in any order relative to their master: an override replaces that instance, may move it into or out of
 *     the window, or cancel it with STATUS:CANCELLED. RDATE is not supported (calendar apps rarely write it).
 *   - What counts (decisions.md): timed events, not TRANSP:TRANSPARENT ("Free"), not STATUS:CANCELLED, and not
 *     declined by you (the ATTENDEE whose mailto: matches self_email has PARTSTAT=DECLINED). CLASS:PRIVATE or
 *     CONFIDENTIAL marks the meeting private; its title and location are dropped here, so they are never stored.
 *   - Titles and locations: unescaped, cleaned as tb_text_clean() does, cut to the meeting's buffers on a character
 *     boundary, and characters the fonts can't draw become "?" (tb_text.h).
 *   - Long lines: only the properties above are buffered (up to CAL_LINE_MAX bytes, then cut and counted); everything
 *     else, including huge DESCRIPTIONs and ATTACHments, streams past without being stored.
 *   - Garbage: a feed that never shows BEGIN:VCALENDAR in its first 4 KB is "not_a_calendar", and cal_feed_write()
 *     says so at once, so the fetch can stop early.
 *
 * Memory: one cal_feed_t of about 30 KB (allocate it in PSRAM on the device; cal_feed_new() uses calloc, which the
 * device's SPIRAM_USE_MALLOC sends to PSRAM for blocks this size). Nothing else is allocated.
 *
 * Instance ids (tb_meeting_t.id, which core keys "set aside" on): cal_instance_id(UID, the instance's original UTC
 * start), so an instance moved by an override keeps its id (the override's RECURRENCE-ID is that original start).
 */
#pragma once

#include "cal_rrule.h"
#include "cal_tz.h"
#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAL_LINE_MAX        4096    /* longest kept logical line (EXDATE lines can list many dates) */
#define CAL_CANDIDATES_MAX  64      /* instances in the window before overrides are applied */
#define CAL_OVERRIDES_MAX   256     /* RECURRENCE-ID keys remembered (only those near the window) */
#define CAL_EXDATES_MAX     64      /* per event (only those near the window are kept) */
#define CAL_VTIMEZONES_MAX  8       /* VTIMEZONE blocks remembered per feed */
#define CAL_RRULE_STEPS_EVENT 100000u   /* periods one RRULE may examine */
#define CAL_RRULE_STEPS_FEED  2000000u  /* ...and all of a feed's RRULEs together */

typedef enum {
    CAL_OK = 0,
    CAL_ERR_NOT_A_CALENDAR,     /* api.md 11.2 not_a_calendar */
    CAL_ERR_NO_MEMORY,
} cal_err_t;

typedef struct {
    uint64_t bytes;             /* body bytes read */
    uint32_t lines;             /* logical lines */
    uint32_t events;            /* VEVENTs seen */
    uint32_t recurring;         /* ...with an RRULE */
    uint32_t overrides;         /* ...with a RECURRENCE-ID */
    uint32_t unsupported_rrule; /* RRULEs cal_rrule_parse() refused (first instance used alone) */
    uint32_t rrule_budget_hit;  /* RRULEs stopped by the step budget */
    uint32_t unknown_tzid;
    uint32_t vtimezones;        /* VTIMEZONE blocks turned into rules */
    uint32_t cut_lines;
    uint32_t dropped_candidates;    /* the window had more than CAL_CANDIDATES_MAX instances (latest dropped) */
    uint32_t dropped_overrides;     /* more than CAL_OVERRIDES_MAX RECURRENCE-IDs near the window */
    uint32_t dropped_exdates;       /* more than CAL_EXDATES_MAX EXDATEs near the window in one event */
    bool ended;                 /* END:VCALENDAR was seen (a complete feed) */
} cal_stats_t;

/* Opaque reader state (large: see Memory above). */
typedef struct cal_feed cal_feed_t;

/*
 * Start reading a feed. window_start..window_end is in UTC (cal_sync uses local midnight today to local midnight after
 * tomorrow, cal_today_window()). device_tz: the bar's zone, for floating times and unknown TZIDs (NULL = UTC).
 * self_email: the calendar owner's address, for PARTSTAT (cal_url_info_t.self_email), or NULL / "" if unknown.
 * Returns NULL if out of memory.
 */
cal_feed_t *cal_feed_new(tb_epoch_t window_start, tb_epoch_t window_end, const cal_tz_t *device_tz, const char *self_email);
void cal_feed_free(cal_feed_t *f);

/* Feed the next chunk of the body (any size, any split, even mid-character). CAL_ERR_NOT_A_CALENDAR as soon as 4 KB
 * have gone by without BEGIN:VCALENDAR (and for every later call). */
cal_err_t cal_feed_write(cal_feed_t *f, const char *data, size_t len);

/* End of body: apply overrides and EXDATEs, keep what counts, sort by start (then end, then id), and copy at most max
 * meetings into out (the earliest). Returns CAL_OK and sets *n, or CAL_ERR_NOT_A_CALENDAR (*n = 0). Call it once. */
cal_err_t cal_feed_finish(cal_feed_t *f, tb_meeting_t *out, int max, int *n);

const cal_stats_t *cal_feed_stats(const cal_feed_t *f);

/* The id a meeting instance gets: FNV-1a of the UID, then of the instance's UTC start (8 bytes, little-endian).
 * Never 0. */
uint32_t cal_instance_id(const char *uid, tb_epoch_t start);

#ifdef __cplusplus
}
#endif
