/*
 * tb_fmt.h: the mock-up's time and duration formatters (fmt, fmtShort, ampm, mmss, hm, minsUp, span, ago, and the
 * Clock screen's date). Used by core (toast text), ui (screen text) and net (nothing user-facing; the Remote page
 * formats in JavaScript).
 *
 * Owner: core builder. Pure C. Local time comes from localtime_r(), so the process time zone must be set
 * (setenv("TZ", posix) + tzset(); main does it from settings.device.time_zone through calendar/cal_tz.h).
 * All functions write a NUL-terminated string into buf (cap bytes) and return buf.
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every time of day takes h24, the Time format setting (settings.display.time_format, decisions.md "Time format"):
 * false = 12-hour (the default), true = 24-hour. No screen formats a time itself. */
/* fmt(d): 12-hour "2:04 PM" (en-US, hour without a leading zero, 2-digit minutes); 24-hour "14:04" (hour zero-padded:
 * "09:05", midnight "00:00", never 24:00). */
char *tb_fmt_time(char *buf, size_t cap, tb_epoch_t t, bool h24);
/* fmtShort(d): the time without its AM or PM, "2:04"; in 24-hour it is the whole time, "14:04". */
char *tb_fmt_time_short(char *buf, size_t cap, tb_epoch_t t, bool h24);
/* ampm(d): "AM" or "PM"; "" in 24-hour, so every AM/PM chip and suffix disappears. */
const char *tb_fmt_ampm(tb_epoch_t t, bool h24);
/* A typed time (Away's "Back at", wire form HH:MM in 24 hours): "3:30" in 12-hour (no AM or PM, as before), "15:30" or
 * "03:30" in 24-hour. Out-of-range input is read as 0. */
char *tb_fmt_hhmm(char *buf, size_t cap, int hh, int mm, bool h24);
/* mmss(s): "18:42", "5:00", "125:00" (minutes aren't wrapped into hours). */
char *tb_fmt_mmss(char *buf, size_t cap, int32_t seconds);
/* hm(mins): "45m", "1h 5m", "10h 20m". Bold Signal drops the mock-up's hours-only rule from 10 h (decisions.md):
 * "10h 20m" fits the 160 px value. */
char *tb_fmt_hm(char *buf, size_t cap, int32_t minutes);
/* minsUp(ms): durations on the automatic screens round up, at least 1. */
int32_t tb_mins_up(tb_ms_t ms);
/* span(a, b): "2:30–3:15 PM" when both share AM/PM, else "11:30 AM–12:15 PM" (en dash U+2013); 24-hour "14:30–15:15". */
char *tb_fmt_span(char *buf, size_t cap, tb_epoch_t a, tb_epoch_t b, bool h24);
/* ago(d, short): "just now", "2 min ago" / "2m ago" (short, for a tile foot), "at 2:04 PM" from an hour on. */
char *tb_fmt_ago(char *buf, size_t cap, tb_epoch_t then, tb_epoch_t now, bool short_form, bool h24);
/* The Clock screen's kicker: "Sunday, October 4". */
char *tb_fmt_date_long(char *buf, size_t cap, tb_epoch_t t);
/* Local calendar date as yyyymmdd, and the local midnight that starts it (UTC epoch). */
int32_t tb_local_yyyymmdd(tb_epoch_t t);
tb_epoch_t tb_local_midnight(tb_epoch_t t);
/* RFC 3339 with the local UTC offset, to the second (api.md 2.3): "2026-10-04T14:12:00-07:00". */
char *tb_fmt_rfc3339(char *buf, size_t cap, tb_epoch_t t);

#ifdef __cplusplus
}
#endif
