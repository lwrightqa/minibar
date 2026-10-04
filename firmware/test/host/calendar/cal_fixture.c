/* cal_fixture.c: helpers for the calendar host tests. Owner: calendar builder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cal_fixture.h"

char *fx_load(const char *name, size_t *len)
{
    /* ctest sets TB_FIXTURES; run on its own, the runner uses the source folder it was built from. */
    const char *dir = getenv("TB_FIXTURES");
#ifdef TB_FIXTURES_DIR
    if (!dir || !dir[0]) dir = TB_FIXTURES_DIR;
#endif
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir ? dir : "fixtures", name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        TB_FAIL_AT("can't open fixture %s", path);
        *len = 0;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    *len = got;
    return buf;
}

char *fx_to_lf(const char *data, size_t len, size_t *out_len)
{
    char *out = malloc(len + 1);
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\r' && i + 1 < len && data[i + 1] == '\n') continue;
        out[o++] = data[i];
    }
    out[o] = '\0';
    *out_len = o;
    return out;
}

tb_epoch_t fx_utc(int y, int mo, int d, int h, int mi)
{
    return (tb_epoch_t)cal_days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60;
}

cal_tz_t fx_tz(const char *posix)
{
    cal_tz_t tz;
    if (!cal_tz_parse(posix, &tz)) TB_FAIL_AT("POSIX zone didn't parse: %s", posix);
    return tz;
}

void fx_la_window(int y, int mo, int d, tb_epoch_t *ws, tb_epoch_t *we)
{
    cal_tz_t la = fx_tz(LA_POSIX);
    /* noon UTC + 8 h is always inside the Los Angeles day y-m-d */
    cal_today_window(&la, fx_utc(y, mo, d, 20, 0), ws, we);
}

void fx_run(const char *data, size_t len, size_t chunk, tb_epoch_t ws, tb_epoch_t we, const char *device_posix,
            const char *self_email, feed_result_t *r)
{
    memset(r, 0, sizeof(*r));
    cal_tz_t tz = fx_tz(device_posix ? device_posix : LA_POSIX);
    cal_feed_t *f = cal_feed_new(ws, we, &tz, self_email);
    if (!f) {
        TB_FAIL_AT("%s", "cal_feed_new failed");
        return;
    }
    if (!chunk) chunk = len ? len : 1;
    for (size_t i = 0; i < len; i += chunk) {
        size_t n = len - i < chunk ? len - i : chunk;
        cal_err_t e = cal_feed_write(f, data + i, n);
        if (e != CAL_OK && r->write_err == CAL_OK) r->write_err = e;
    }
    r->err = cal_feed_finish(f, r->m, CAL_CANDIDATES_MAX, &r->n);
    r->stats = *cal_feed_stats(f);
    cal_feed_free(f);
}

void fx_run_file(const char *name, int y, int mo, int d, const char *self_email, feed_result_t *r)
{
    size_t len;
    char *data = fx_load(name, &len);
    tb_epoch_t ws, we;
    fx_la_window(y, mo, d, &ws, &we);
    if (data) fx_run(data, len, 0, ws, we, LA_POSIX, self_email, r);
    else memset(r, 0, sizeof(*r));
    free(data);
}

const tb_meeting_t *fx_find(const feed_result_t *r, const char *title)
{
    for (int i = 0; i < r->n; i++)
        if (!strcmp(r->m[i].title, title)) return &r->m[i];
    return NULL;
}

int fx_count(const feed_result_t *r, const char *title)
{
    int k = 0;
    for (int i = 0; i < r->n; i++)
        if (!strcmp(r->m[i].title, title)) k++;
    return k;
}

const tb_meeting_t *fx_at(const feed_result_t *r, tb_epoch_t start)
{
    for (int i = 0; i < r->n; i++)
        if (r->m[i].start == start) return &r->m[i];
    return NULL;
}

void fx_dump(const feed_result_t *r)
{
    printf("    %d meetings (err %d):\n", r->n, r->err);
    for (int i = 0; i < r->n; i++) {
        cal_civil_t s, e;
        cal_secs_to_civil(r->m[i].start, &s);
        cal_secs_to_civil(r->m[i].end, &e);
        printf("      %04d-%02d-%02d %02d:%02dZ - %02d:%02dZ %s%s [%s] id %08x\n", s.year, s.month, s.day, s.hour,
               s.min, e.hour, e.min, r->m[i].title, r->m[i].priv ? " (private)" : "", r->m[i].location,
               (unsigned)r->m[i].id);
    }
}
