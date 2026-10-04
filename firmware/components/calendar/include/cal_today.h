/*
 * cal_today.h: the window a sync reads, and the "now / next / rest of today" questions about a list of meetings, in an
 * explicit zone (the mock-up's currentEvent(), nextEvent(), todays() and leftText()). Pure C.
 *
 * Owner: calendar builder.
 *
 * core answers the same questions for the screen with the C library's zone (tb_app_current_meeting(),
 * tb_app_next_meeting(), tb_app_meetings_left(), tb_app_left_text()), which main sets to the same POSIX rule; these
 * versions take the zone as an argument so calendar and net can use them on any task and the host tests can run
 * them in many zones. The rules are the mock-up's:
 *   - now:  a meeting in progress (start <= now < end); of two that overlap, the one that started last. A meeting that
 *           began yesterday evening and runs past midnight is in progress today.
 *   - next: the first meeting that starts later today (local date of now).
 *   - left: meetings not over yet that start today (so one that began yesterday doesn't count, even while it runs).
 */
#pragma once

#include "cal_tz.h"
#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The days a sync keeps: local midnight today to local midnight after tomorrow (two local days, 46 to 50 hours). */
void cal_today_window(const cal_tz_t *tz, tb_epoch_t now, tb_epoch_t *start, tb_epoch_t *end);

const tb_meeting_t *cal_today_current(const tb_meeting_t *m, int n, tb_epoch_t now);
const tb_meeting_t *cal_today_next(const tb_meeting_t *m, int n, tb_epoch_t now, const cal_tz_t *tz);
int cal_today_left(const tb_meeting_t *m, int n, tb_epoch_t now, const cal_tz_t *tz);
/* "3 meetings left today", "1 meeting left today", "no more meetings today". Returns buf. */
char *cal_today_left_text(const tb_meeting_t *m, int n, tb_epoch_t now, const cal_tz_t *tz, char *buf, size_t cap);

/* Keep at most max meetings of a list sorted by start, in place: if there are more, the ones already over at now
 * go first, then the latest. Returns the new count (the order is kept). */
int cal_today_trim(tb_meeting_t *m, int n, int max, tb_epoch_t now);

/* Two lists hold the same meetings (ids, times, titles, locations, private flags) in the same order. */
bool cal_meetings_equal(const tb_meeting_t *a, int na, const tb_meeting_t *b, int nb);

#ifdef __cplusplus
}
#endif
