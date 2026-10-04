/*
 * cal_rrule.c: RRULE parsing and expansion. Owner: calendar builder. See cal_rrule.h.
 *
 * Expansion follows RFC 5545 3.3.10 period by period: each period (a day, a WKST-aligned week, a month or a year,
 * stepped by INTERVAL) yields its candidate dates from BYMONTH, BYMONTHDAY and BYDAY (expanding or limiting as the
 * RFC's table says for each FREQ), BYSETPOS picks from them, and each date takes DTSTART's time of day.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cal_rrule.h"

/* ---------- parsing ---------- */

static int day_code(const char *p)
{
    static const char *const codes[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    for (int i = 0; i < 7; i++)
        if (toupper((unsigned char)p[0]) == codes[i][0] && toupper((unsigned char)p[1]) == codes[i][1]) return i;
    return -1;
}

/* A signed integer in [lo, hi] spanning exactly [p, end). */
static bool parse_int(const char *p, const char *end, long lo, long hi, long *out)
{
    if (p >= end) return false;
    bool neg = false;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    if (p >= end || end - p > 7) return false;
    long v = 0;
    for (; p < end; p++) {
        if (!isdigit((unsigned char)*p)) return false;
        v = v * 10 + (*p - '0');
    }
    if (neg) v = -v;
    if (v < lo || v > hi) return false;
    *out = v;
    return true;
}

/* YYYYMMDD[THHMMSS[Z]] */
static bool parse_until(const char *p, const char *end, cal_rrule_t *r)
{
    size_t n = (size_t)(end - p);
    if (n != 8 && n != 15 && n != 16) return false;
    for (size_t i = 0; i < n; i++) {
        if (i == 8) {
            if (p[i] != 'T' && p[i] != 't') return false;
        } else if (i == 15) {
            if (p[i] != 'Z' && p[i] != 'z') return false;
        } else if (!isdigit((unsigned char)p[i])) {
            return false;
        }
    }
#define D2(o) ((p[o] - '0') * 10 + (p[(o) + 1] - '0'))
    cal_civil_t c = {.year = (int16_t)(D2(0) * 100 + D2(2)), .month = (int8_t)D2(4), .day = (int8_t)D2(6)};
    if (n == 8) {
        c.hour = 23;
        c.min = 59;
        c.sec = 59;
    } else {
        c.hour = (int8_t)D2(9);
        c.min = (int8_t)D2(11);
        c.sec = (int8_t)D2(13);
        if (c.sec == 60) c.sec = 59;
    }
#undef D2
    if (!cal_civil_valid(&c)) return false;
    r->has_until = true;
    r->until = c;
    r->until_is_utc = n == 16;
    if (r->until_is_utc) r->until_utc = cal_civil_to_secs(&c);
    return true;
}

static bool key_is(const char *k, size_t kn, const char *name)
{
    return strlen(name) == kn && !strncasecmp(k, name, kn);
}

bool cal_rrule_parse(const char *value, cal_rrule_t *out)
{
    cal_rrule_t r;
    memset(&r, 0, sizeof(r));
    memset(out, 0, sizeof(*out));
    r.interval = 1;
    r.wkst = 1;
    if (!value) return false;
    bool has_ord = false;
    const char *p = value;
    while (*p) {
        const char *part_end = strchr(p, ';');
        if (!part_end) part_end = p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(part_end - p));
        if (part_end == p) {    /* ";;" or a trailing ";" */
            p = *part_end ? part_end + 1 : part_end;
            continue;
        }
        if (!eq) return false;
        const char *k = p, *v = eq + 1;
        size_t kn = (size_t)(eq - p);
        long n;
        if (v == part_end) return false;    /* "BYDAY=" */
        if (key_is(k, kn, "FREQ")) {
            size_t vn = (size_t)(part_end - v);
            if (vn == 5 && !strncasecmp(v, "DAILY", 5)) r.freq = CAL_FREQ_DAILY;
            else if (vn == 6 && !strncasecmp(v, "WEEKLY", 6)) r.freq = CAL_FREQ_WEEKLY;
            else if (vn == 7 && !strncasecmp(v, "MONTHLY", 7)) r.freq = CAL_FREQ_MONTHLY;
            else if (vn == 6 && !strncasecmp(v, "YEARLY", 6)) r.freq = CAL_FREQ_YEARLY;
            else return false;  /* HOURLY, MINUTELY, SECONDLY: not for meetings on a bar */
        } else if (key_is(k, kn, "INTERVAL")) {
            if (!parse_int(v, part_end, 1, 10000, &n)) return false;
            r.interval = (uint16_t)n;
        } else if (key_is(k, kn, "COUNT")) {
            if (!parse_int(v, part_end, 1, 1000000, &n)) return false;
            r.count = (int32_t)n;
        } else if (key_is(k, kn, "UNTIL")) {
            if (!parse_until(v, part_end, &r)) return false;
        } else if (key_is(k, kn, "WKST")) {
            if (part_end - v != 2 || (n = day_code(v)) < 0) return false;
            r.wkst = (uint8_t)n;
        } else if (key_is(k, kn, "BYDAY")) {
            const char *q = v;
            while (q < part_end) {
                const char *c = memchr(q, ',', (size_t)(part_end - q));
                if (!c) c = part_end;
                if (c - q < 2 || r.n_byday >= CAL_RRULE_BYDAY_MAX) return false;
                int wd = day_code(c - 2);
                if (wd < 0) return false;
                long ord = 0;
                if (c - 2 > q) {
                    if (!parse_int(q, c - 2, -53, 53, &ord) || ord == 0) return false;
                    has_ord = true;
                }
                r.byday[r.n_byday].ord = (int8_t)ord;
                r.byday[r.n_byday].wd = (uint8_t)wd;
                r.n_byday++;
                q = c + 1;
            }
        } else if (key_is(k, kn, "BYMONTHDAY")) {
            const char *q = v;
            while (q < part_end) {
                const char *c = memchr(q, ',', (size_t)(part_end - q));
                if (!c) c = part_end;
                if (!parse_int(q, c, -31, 31, &n) || n == 0 || r.n_bymonthday >= CAL_RRULE_BYMONTHDAY_MAX) return false;
                r.bymonthday[r.n_bymonthday++] = (int8_t)n;
                q = c + 1;
            }
        } else if (key_is(k, kn, "BYMONTH")) {
            const char *q = v;
            while (q < part_end) {
                const char *c = memchr(q, ',', (size_t)(part_end - q));
                if (!c) c = part_end;
                if (!parse_int(q, c, 1, 12, &n)) return false;
                r.bymonth_mask |= (uint16_t)(1u << n);
                q = c + 1;
            }
        } else if (key_is(k, kn, "BYSETPOS")) {
            const char *q = v;
            while (q < part_end) {
                const char *c = memchr(q, ',', (size_t)(part_end - q));
                if (!c) c = part_end;
                if (!parse_int(q, c, -366, 366, &n) || n == 0 || r.n_bysetpos >= CAL_RRULE_BYSETPOS_MAX) return false;
                r.bysetpos[r.n_bysetpos++] = (int16_t)n;
                q = c + 1;
            }
        } else if (kn >= 2 && !strncasecmp(k, "X-", 2)) {
            /* experimental parts are ignored */
        } else {
            return false;   /* BYWEEKNO, BYYEARDAY, BYHOUR, BYMINUTE, BYSECOND, RSCALE, SKIP, or garbage */
        }
        p = *part_end ? part_end + 1 : part_end;
    }
    if (r.freq == CAL_FREQ_NONE) return false;
    /* RFC 5545: ordinals only with MONTHLY and YEARLY; no BYMONTHDAY with WEEKLY. */
    if (has_ord && r.freq != CAL_FREQ_MONTHLY && r.freq != CAL_FREQ_YEARLY) return false;
    if (r.n_bymonthday && r.freq == CAL_FREQ_WEEKLY) return false;
    if (r.freq == CAL_FREQ_MONTHLY) {
        for (int i = 0; i < r.n_byday; i++)
            if (abs(r.byday[i].ord) > 5) return false;
    }
    *out = r;
    return true;
}

/* ---------- expansion ---------- */

#define SET_MAX 372     /* a year of BYMONTHDAY across every month is at most 366 dates */

typedef struct {
    int32_t d[SET_MAX];
    int n;
    int examined;       /* days looked at for this period (what the step budget is charged) */
} dayset_t;

static int wd_of(int32_t day)
{
    int w = (int)((day + 4) % 7);
    return w < 0 ? w + 7 : w;
}

static bool month_ok(const cal_rrule_t *r, int m)
{
    return !r->bymonth_mask || (r->bymonth_mask & (1u << m));
}

static bool monthday_ok(const cal_rrule_t *r, int d, int dim)
{
    if (!r->n_bymonthday) return true;
    for (int i = 0; i < r->n_bymonthday; i++) {
        int md = r->bymonthday[i];
        if (md == d || (md < 0 && dim + 1 + md == d)) return true;
    }
    return false;
}

/* BYDAY with ordinals counted inside a span of len days, where this day is the pos-th (1-based). */
static bool byday_ok(const cal_rrule_t *r, int wd, int pos, int len)
{
    if (!r->n_byday) return true;
    for (int i = 0; i < r->n_byday; i++) {
        if (r->byday[i].wd != wd) continue;
        int o = r->byday[i].ord;
        if (o == 0) return true;
        if (o > 0 && (pos - 1) / 7 + 1 == o) return true;
        if (o < 0 && (len - pos) / 7 + 1 == -o) return true;
    }
    return false;
}

static void add(dayset_t *s, int32_t day)
{
    if (s->n < SET_MAX) s->d[s->n++] = day;
}

/* The days of month (y, m) the rule picks (MONTHLY, and YEARLY with BYMONTH). */
static void month_days(const cal_rrule_t *r, int y, int m, int dt_day, dayset_t *s)
{
    int dim = cal_days_in_month(y, m);
    int32_t first = cal_days_from_civil(y, m, 1);
    s->examined += r->n_bymonthday || r->n_byday ? dim : 1;
    if (r->n_bymonthday || r->n_byday) {
        for (int d = 1; d <= dim; d++)
            if (monthday_ok(r, d, dim) && byday_ok(r, wd_of(first + d - 1), d, dim)) add(s, first + d - 1);
    } else if (dt_day <= dim) {
        add(s, first + dt_day - 1);
    }
}

static void year_days(const cal_rrule_t *r, int y, int dt_month, int dt_day, dayset_t *s)
{
    if (r->bymonth_mask || r->n_bymonthday) {
        for (int m = 1; m <= 12; m++) {
            if (r->bymonth_mask ? !(r->bymonth_mask & (1u << m)) : false) continue;
            month_days(r, y, m, dt_day, s);
        }
    } else if (r->n_byday) {
        int len = cal_is_leap(y) ? 366 : 365;
        int32_t first = cal_days_from_civil(y, 1, 1);
        s->examined += len;
        for (int i = 1; i <= len; i++)
            if (byday_ok(r, wd_of(first + i - 1), i, len)) add(s, first + i - 1);
    } else {
        s->examined += 1;
        if (dt_day <= cal_days_in_month(y, dt_month)) add(s, cal_days_from_civil(y, dt_month, dt_day));
    }
}

static int cmp_i32(const void *a, const void *b)
{
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return x < y ? -1 : x > y;
}

static void apply_setpos(const cal_rrule_t *r, dayset_t *s)
{
    if (!r->n_bysetpos || !s->n) return;
    int32_t keep[CAL_RRULE_BYSETPOS_MAX];
    int k = 0;
    for (int i = 0; i < r->n_bysetpos; i++) {
        int p = r->bysetpos[i];
        int idx = p > 0 ? p - 1 : s->n + p;
        if (idx >= 0 && idx < s->n) keep[k++] = s->d[idx];
    }
    qsort(keep, (size_t)k, sizeof(keep[0]), cmp_i32);
    s->n = 0;
    for (int i = 0; i < k; i++)
        if (i == 0 || keep[i] != keep[i - 1]) s->d[s->n++] = keep[i];
}

typedef struct {
    const cal_rrule_t *r;
    const cal_tz_t *tz;
    int32_t dur;
    tb_epoch_t ws, we;
    cal_occ_fn fn;
    void *ctx;
    int32_t emitted;    /* toward COUNT */
    int passed;         /* to fn */
} walk_t;

/* One occurrence. Returns false when the walk is over. */
static bool emit(walk_t *w, const cal_civil_t *c)
{
    const cal_rrule_t *r = w->r;
    tb_epoch_t utc = cal_tz_to_utc(w->tz, c);
    if (r->has_until) {
        if (r->until_is_utc ? utc > r->until_utc : cal_civil_cmp(c, &r->until) > 0) return false;
    }
    if (utc >= w->we) return false;
    w->emitted++;
    if (utc + w->dur > w->ws) {
        w->passed++;
        if (!w->fn(w->ctx, c)) return false;
    }
    return !(r->count && w->emitted >= r->count);
}

static int32_t floor_div32(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return (int32_t)q;
}

int cal_rrule_expand(const cal_rrule_t *r, const cal_civil_t *dtstart, const cal_tz_t *tz, int32_t duration_s,
                     tb_epoch_t window_start, tb_epoch_t window_end, uint32_t *steps, cal_occ_fn fn, void *ctx)
{
    return cal_rrule_expand_ex(r, dtstart, tz, duration_s, window_start, window_end, steps, NULL, NULL, fn, ctx);
}

int cal_rrule_expand_ex(const cal_rrule_t *r, const cal_civil_t *dtstart, const cal_tz_t *tz, int32_t duration_s,
                        tb_epoch_t window_start, tb_epoch_t window_end, uint32_t *steps, cal_tick_fn tick,
                        void *tick_ctx, cal_occ_fn fn, void *ctx)
{
    if (!r || !dtstart || !tz || !fn || r->freq == CAL_FREQ_NONE || !cal_civil_valid(dtstart)) return 0;
    walk_t w = {.r = r, .tz = tz, .dur = duration_s > 0 ? duration_s : 0, .ws = window_start, .we = window_end,
                .fn = fn, .ctx = ctx};
    const int interval = r->interval ? r->interval : 1;
    const int32_t d0 = cal_days_from_civil(dtstart->year, dtstart->month, dtstart->day);
    const int32_t w0 = d0 - (wd_of(d0) - r->wkst + 7) % 7;    /* the week holding DTSTART, from WKST */
    const int y0 = dtstart->year, m0 = dtstart->month;
    /* Nothing local can be in the window after this day (offsets are within 26 h). */
    const int32_t last_day = floor_div32(window_end, 86400) + 2;

    /* Skip whole periods before the window when COUNT doesn't need them counted. */
    int64_t k = 0;
    tb_epoch_t dt_utc = cal_tz_to_utc(tz, dtstart);
    bool skip_first = false;
    if (!r->count && dt_utc + w.dur <= window_start) {
        int32_t target = floor_div32(window_start - w.dur, 86400) - 2;
        int ty, tm, td;
        cal_civil_from_days(target, &ty, &tm, &td);
        int64_t k0 = 0;
        switch (r->freq) {
        case CAL_FREQ_DAILY: k0 = floor_div32(target - d0, interval); break;
        case CAL_FREQ_WEEKLY: k0 = floor_div32(target - w0, 7 * interval); break;
        case CAL_FREQ_MONTHLY: k0 = floor_div32((int64_t)(ty * 12 + tm) - (y0 * 12 + m0), interval); break;
        case CAL_FREQ_YEARLY: k0 = floor_div32(ty - y0, interval); break;
        default: break;
        }
        k0 -= 1;
        if (k0 > 0) {
            k = k0;
            skip_first = true;  /* DTSTART is in a skipped period, before the window */
        }
    }

    /* DTSTART is always the first instance. */
    if (!skip_first && !emit(&w, dtstart)) return w.passed;

    dayset_t set;
    uint32_t since_tick = 0;
    for (;; k++) {
        if (steps && !*steps) return -1;
        set.n = 0;
        set.examined = 0;
        int32_t period_first;
        switch (r->freq) {
        case CAL_FREQ_DAILY: {
            int32_t day = d0 + (int32_t)(k * interval);
            period_first = day;
            set.examined = 1;
            int y, m, d;
            cal_civil_from_days(day, &y, &m, &d);
            if (month_ok(r, m) && monthday_ok(r, d, cal_days_in_month(y, m)) && byday_ok(r, wd_of(day), 1, 1))
                add(&set, day);
            break;
        }
        case CAL_FREQ_WEEKLY: {
            int32_t ws = w0 + (int32_t)(k * 7 * interval);
            period_first = ws;
            set.examined = 7;
            for (int i = 0; i < 7; i++) {
                int32_t day = ws + i;
                int wd = wd_of(day);
                bool pick = r->n_byday ? byday_ok(r, wd, 1, 1) : wd == wd_of(d0);
                if (!pick) continue;
                int y, m, d;
                cal_civil_from_days(day, &y, &m, &d);
                if (month_ok(r, m)) add(&set, day);
            }
            break;
        }
        case CAL_FREQ_MONTHLY: {
            int64_t mi = (int64_t)y0 * 12 + (m0 - 1) + k * interval;
            int y = (int)(mi / 12), m = (int)(mi % 12) + 1;
            if (y > 9999) return w.passed;
            period_first = cal_days_from_civil(y, m, 1);
            if (month_ok(r, m)) month_days(r, y, m, dtstart->day, &set);
            break;
        }
        case CAL_FREQ_YEARLY: {
            int64_t y = y0 + k * interval;
            if (y > 9999) return w.passed;
            period_first = cal_days_from_civil((int)y, 1, 1);
            year_days(r, (int)y, m0, dtstart->day, &set);
            break;
        }
        default:
            return w.passed;
        }
        /* The budget is charged by the days looked at, so a YEARLY rule with BYDAY (a whole year a period) can't
         * run 366 times longer than a DAILY one on the same budget. */
        uint32_t cost = set.examined > 0 ? (uint32_t)set.examined : 1;
        if (steps) *steps -= cost < *steps ? cost : *steps;
        since_tick += cost;
        if (tick && since_tick >= CAL_TICK_DAYS) {
            since_tick = 0;
            tick(tick_ctx);
        }
        if (period_first > last_day) return w.passed;
        apply_setpos(r, &set);
        for (int i = 0; i < set.n; i++) {
            if (set.d[i] < d0 || set.d[i] > last_day) continue;
            int y, m, d;
            cal_civil_from_days(set.d[i], &y, &m, &d);
            cal_civil_t c = {.year = (int16_t)y, .month = (int8_t)m, .day = (int8_t)d,
                             .hour = dtstart->hour, .min = dtstart->min, .sec = dtstart->sec};
            if (cal_civil_cmp(&c, dtstart) <= 0) continue;  /* DTSTART went first */
            if (!emit(&w, &c)) return w.passed;
        }
    }
}
