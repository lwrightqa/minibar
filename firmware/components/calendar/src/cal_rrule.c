/*
 * cal_rrule.c: RRULE parsing and expansion. Owner: calendar builder. Skeleton stubs; see cal_rrule.h.
 */
#include <string.h>

#include "cal_rrule.h"

bool cal_rrule_parse(const char *value, cal_rrule_t *out)
{
    memset(out, 0, sizeof(*out));
    out->interval = 1;
    out->wkst = 1;
    (void)value;
    return false;   /* TODO(calendar) */
}

int cal_rrule_expand(const cal_rrule_t *r, const cal_civil_t *dtstart, const cal_tz_t *tz, int32_t duration_s,
                     tb_epoch_t window_start, tb_epoch_t window_end, uint32_t max_steps, cal_occ_fn fn, void *ctx)
{
    (void)r; (void)dtstart; (void)tz; (void)duration_s; (void)window_start; (void)window_end; (void)max_steps;
    (void)fn; (void)ctx;
    return 0;   /* TODO(calendar) */
}
