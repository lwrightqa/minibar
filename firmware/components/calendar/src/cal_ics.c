/*
 * cal_ics.c: the streaming iCalendar reader. Owner: calendar builder. Skeleton stubs; see cal_ics.h.
 */
#include <stdlib.h>
#include <string.h>

#include "cal_ics.h"

struct cal_feed {
    tb_epoch_t window_start, window_end;
    cal_tz_t device_tz;
    char self_email[128];
    cal_stats_t stats;
    bool seen_vcalendar;
    /* TODO(calendar): the line assembler (CAL_LINE_MAX), the current VEVENT's fields, candidates, override keys. */
};

cal_feed_t *cal_feed_new(tb_epoch_t window_start, tb_epoch_t window_end, const cal_tz_t *device_tz, const char *self_email)
{
    cal_feed_t *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->window_start = window_start;
    f->window_end = window_end;
    if (device_tz) f->device_tz = *device_tz;
    if (self_email) strncpy(f->self_email, self_email, sizeof(f->self_email) - 1);
    return f;
}

void cal_feed_free(cal_feed_t *f)
{
    free(f);
}

cal_err_t cal_feed_write(cal_feed_t *f, const char *data, size_t len)
{
    (void)f; (void)data; (void)len;
    return CAL_OK;  /* TODO(calendar) */
}

cal_err_t cal_feed_finish(cal_feed_t *f, tb_meeting_t *out, int max, int *n)
{
    (void)f; (void)out; (void)max;
    *n = 0;
    return CAL_OK;  /* TODO(calendar) */
}

const cal_stats_t *cal_feed_stats(const cal_feed_t *f)
{
    return &f->stats;
}

uint32_t cal_instance_id(const char *uid, tb_epoch_t start)
{
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)uid; p && *p; p++) h = (h ^ *p) * 16777619u;
    for (int i = 0; i < 8; i++) h = (h ^ (uint8_t)((uint64_t)start >> (8 * i))) * 16777619u;
    return h ? h : 1;
}
