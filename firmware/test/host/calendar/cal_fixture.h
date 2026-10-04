/* cal_fixture.h: helpers for the calendar host tests (fixture files, running a feed, UTC instants). Owner: calendar
 * builder. */
#pragma once

#include "cal_ics.h"
#include "cal_today.h"
#include "cal_tz.h"
#include "tb_test.h"

#define LA_POSIX "PST8PDT,M3.2.0,M11.1.0"
#define SELF_EMAIL "sam.lee@example.com"

typedef struct {
    tb_meeting_t m[CAL_CANDIDATES_MAX];
    int n;
    cal_err_t write_err;    /* the first error cal_feed_write() returned */
    cal_err_t err;          /* cal_feed_finish() */
    cal_stats_t stats;
} feed_result_t;

/* Read test/host/calendar/fixtures/<name> (TB_FIXTURES); NULL and a failed check if missing. free() it. */
char *fx_load(const char *name, size_t *len);
/* The same bytes with every CRLF turned into LF. free() it. */
char *fx_to_lf(const char *data, size_t len, size_t *out_len);

/* A UTC instant. */
tb_epoch_t fx_utc(int y, int mo, int d, int h, int mi);
/* A zone from a POSIX string (fails the test if it doesn't parse). */
cal_tz_t fx_tz(const char *posix);
/* The sync window for the Los Angeles day y-m-d (local midnight to local midnight two days later). */
void fx_la_window(int y, int mo, int d, tb_epoch_t *ws, tb_epoch_t *we);

/* Run a whole feed through a reader, chunk bytes at a time (0 = all at once). */
void fx_run(const char *data, size_t len, size_t chunk, tb_epoch_t ws, tb_epoch_t we, const char *device_posix,
            const char *self_email, feed_result_t *r);
/* Run a fixture file for the Los Angeles window starting y-m-d. */
void fx_run_file(const char *name, int y, int mo, int d, const char *self_email, feed_result_t *r);

/* The first meeting with this title (NULL if none), and how many have it. */
const tb_meeting_t *fx_find(const feed_result_t *r, const char *title);
int fx_count(const feed_result_t *r, const char *title);
/* The first meeting starting at this instant. */
const tb_meeting_t *fx_at(const feed_result_t *r, tb_epoch_t start);
/* Print a result (for debugging a failing test). */
void fx_dump(const feed_result_t *r);
