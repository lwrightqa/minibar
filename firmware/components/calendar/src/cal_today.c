/*
 * cal_today.c: the sync window and the now / next / left questions. Owner: calendar builder. See cal_today.h.
 */
#include <stdio.h>
#include <string.h>

#include "cal_today.h"

void cal_today_window(const cal_tz_t *tz, tb_epoch_t now, tb_epoch_t *start, tb_epoch_t *end)
{
    *start = cal_tz_midnight(tz, now);
    *end = cal_tz_midnight_after(tz, now, 2);
}

const tb_meeting_t *cal_today_current(const tb_meeting_t *m, int n, tb_epoch_t now)
{
    const tb_meeting_t *best = NULL;
    for (int i = 0; i < n; i++)
        if (m[i].start <= now && now < m[i].end && (!best || m[i].start > best->start)) best = &m[i];
    return best;
}

static bool today(const tb_meeting_t *e, tb_epoch_t now, tb_epoch_t mid, tb_epoch_t next_mid)
{
    return e->end > now && e->start >= mid && e->start < next_mid;
}

const tb_meeting_t *cal_today_next(const tb_meeting_t *m, int n, tb_epoch_t now, const cal_tz_t *tz)
{
    tb_epoch_t mid = cal_tz_midnight(tz, now), next_mid = cal_tz_midnight_after(tz, now, 1);
    const tb_meeting_t *best = NULL;
    for (int i = 0; i < n; i++)
        if (today(&m[i], now, mid, next_mid) && m[i].start > now && (!best || m[i].start < best->start)) best = &m[i];
    return best;
}

int cal_today_left(const tb_meeting_t *m, int n, tb_epoch_t now, const cal_tz_t *tz)
{
    tb_epoch_t mid = cal_tz_midnight(tz, now), next_mid = cal_tz_midnight_after(tz, now, 1);
    int k = 0;
    for (int i = 0; i < n; i++)
        if (today(&m[i], now, mid, next_mid)) k++;
    return k;
}

char *cal_today_left_text(const tb_meeting_t *m, int n, tb_epoch_t now, const cal_tz_t *tz, char *buf, size_t cap)
{
    int k = cal_today_left(m, n, now, tz);
    if (k) snprintf(buf, cap, "%d meeting%s left today", k, k == 1 ? "" : "s");
    else snprintf(buf, cap, "no more meetings today");
    return buf;
}

int cal_today_trim(tb_meeting_t *m, int n, int max, tb_epoch_t now)
{
    if (max < 0) max = 0;
    if (n <= max) return n;
    int over = 0;
    for (int i = 0; i < n; i++)
        if (m[i].end <= now) over++;
    int drop_over = n - max < over ? n - max : over;
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (drop_over && m[i].end <= now) {
            drop_over--;
            continue;
        }
        if (k != i) m[k] = m[i];
        k++;
    }
    return k > max ? max : k;
}

bool cal_meetings_equal(const tb_meeting_t *a, int na, const tb_meeting_t *b, int nb)
{
    if (na != nb) return false;
    for (int i = 0; i < na; i++) {
        if (a[i].id != b[i].id || a[i].start != b[i].start || a[i].end != b[i].end || a[i].priv != b[i].priv ||
            strcmp(a[i].title, b[i].title) || strcmp(a[i].location, b[i].location))
            return false;
    }
    return true;
}
