/*
 * cal_tz.c: civil-date arithmetic and POSIX time zones. Owner: calendar builder.
 * The date helpers are Howard Hinnant's days_from_civil (written by the lead); the zone code is ours.
 */
#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "cal_tz.h"

#include "cal_tz_table.inc"

/* ---------- civil dates ---------- */

int32_t cal_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

void cal_civil_from_days(int32_t z, int *y, int *m, int *d)
{
    z += 719468;
    const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int32_t yy = (int32_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

int cal_weekday(int y, int m, int d)
{
    int32_t z = cal_days_from_civil(y, m, d);
    return (int)(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);
}

bool cal_is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int cal_days_in_month(int y, int m)
{
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m < 1 || m > 12) return 0;
    if (m == 2) return cal_is_leap(y) ? 29 : 28;
    return dim[m - 1];
}

static int32_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return (int32_t)q;
}

tb_epoch_t cal_civil_to_secs(const cal_civil_t *c)
{
    return (tb_epoch_t)cal_days_from_civil(c->year, c->month, c->day) * 86400 + c->hour * 3600 + c->min * 60 + c->sec;
}

void cal_secs_to_civil(tb_epoch_t t, cal_civil_t *out)
{
    int32_t days = floor_div(t, 86400);
    int32_t secs = (int32_t)(t - (tb_epoch_t)days * 86400);
    int y, m, d;
    cal_civil_from_days(days, &y, &m, &d);
    out->year = (int16_t)y;
    out->month = (int8_t)m;
    out->day = (int8_t)d;
    out->hour = (int8_t)(secs / 3600);
    out->min = (int8_t)(secs % 3600 / 60);
    out->sec = (int8_t)(secs % 60);
}

int cal_civil_cmp(const cal_civil_t *a, const cal_civil_t *b)
{
    tb_epoch_t x = cal_civil_to_secs(a), y = cal_civil_to_secs(b);
    return x < y ? -1 : x > y;
}

bool cal_civil_valid(const cal_civil_t *c)
{
    return c->month >= 1 && c->month <= 12 && c->day >= 1 && c->day <= cal_days_in_month(c->year, c->month) &&
           c->hour >= 0 && c->hour <= 23 && c->min >= 0 && c->min <= 59 && c->sec >= 0 && c->sec <= 59;
}

/* ---------- POSIX TZ strings ---------- */

void cal_tz_utc(cal_tz_t *out)
{
    memset(out, 0, sizeof(*out));
}

/* "PST", "<+0530>" or "<-03>". */
static bool parse_name(const char **pp)
{
    const char *p = *pp;
    if (*p == '<') {
        const char *q = strchr(p, '>');
        if (!q || q - p < 2) return false;
        *pp = q + 1;
        return true;
    }
    const char *q = p;
    while (isalpha((unsigned char)*q)) q++;
    if (q - p < 3) return false;
    *pp = q;
    return true;
}

/* [+-]hh[:mm[:ss]], hours up to max_h. */
static bool parse_hms(const char **pp, int max_h, int32_t *out)
{
    const char *p = *pp;
    int sign = 1;
    if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
    if (!isdigit((unsigned char)*p)) return false;
    int h = 0, n = 0;
    while (isdigit((unsigned char)*p) && n < 3) h = h * 10 + (*p++ - '0'), n++;
    if (h > max_h) return false;
    int mm = 0, ss = 0;
    if (*p == ':') {
        p++;
        if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return false;
        mm = (p[0] - '0') * 10 + (p[1] - '0');
        p += 2;
        if (*p == ':') {
            p++;
            if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return false;
            ss = (p[0] - '0') * 10 + (p[1] - '0');
            p += 2;
        }
    }
    if (mm > 59 || ss > 59) return false;
    *out = sign * (h * 3600 + mm * 60 + ss);
    *pp = p;
    return true;
}

static bool parse_uint(const char **pp, int max, int *out)
{
    const char *p = *pp;
    if (!isdigit((unsigned char)*p)) return false;
    int v = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p++ - '0');
        if (v > max) return false;
    }
    *out = v;
    *pp = p;
    return true;
}

/* Mm.w.d[/time], Jn[/time] or n[/time]. */
static bool parse_rule(const char **pp, cal_tz_rule_t *r)
{
    const char *p = *pp;
    memset(r, 0, sizeof(*r));
    int a, b, c;
    if (*p == 'M') {
        p++;
        if (!parse_uint(&p, 12, &a) || a < 1 || *p++ != '.') return false;
        if (!parse_uint(&p, 5, &b) || b < 1 || *p++ != '.') return false;
        if (!parse_uint(&p, 6, &c)) return false;
        r->kind = CAL_TZ_RULE_M;
        r->m = (uint8_t)a;
        r->w = (uint8_t)b;
        r->d = (uint8_t)c;
    } else if (*p == 'J') {
        p++;
        if (!parse_uint(&p, 365, &a) || a < 1) return false;
        r->kind = CAL_TZ_RULE_J;
        r->yday = (uint16_t)a;
    } else {
        if (!parse_uint(&p, 365, &a)) return false;
        r->kind = CAL_TZ_RULE_N;
        r->yday = (uint16_t)a;
    }
    r->secs = 2 * 3600;
    if (*p == '/') {
        p++;
        if (!parse_hms(&p, 167, &r->secs)) return false;
    }
    *pp = p;
    return true;
}

bool cal_tz_parse(const char *posix, cal_tz_t *out)
{
    cal_tz_utc(out);
    if (!posix) return false;
    const char *p = posix;
    if (*p == ':') return false;    /* ":Europe/Paris" (a file reference): not a rule */
    cal_tz_t z;
    memset(&z, 0, sizeof(z));
    int32_t west;
    if (!parse_name(&p) || !parse_hms(&p, 24, &west)) return false;
    z.std_offset = -west;
    z.dst_offset = z.std_offset;
    if (*p) {
        if (!parse_name(&p)) return false;
        z.has_dst = true;
        z.dst_offset = z.std_offset + 3600;
        if (*p && *p != ',') {
            if (!parse_hms(&p, 24, &west)) return false;
            z.dst_offset = -west;
        }
        if (*p == ',') {
            p++;
            if (!parse_rule(&p, &z.start) || *p++ != ',' || !parse_rule(&p, &z.end)) return false;
        } else {
            /* No rules: the US rules, as glibc assumes. */
            z.start = (cal_tz_rule_t){.kind = CAL_TZ_RULE_M, .m = 3, .w = 2, .d = 0, .secs = 7200};
            z.end = (cal_tz_rule_t){.kind = CAL_TZ_RULE_M, .m = 11, .w = 1, .d = 0, .secs = 7200};
        }
        if (*p) return false;
    }
    *out = z;
    return true;
}

/* ---------- the IANA table ---------- */

static const char *tz_name(int i)
{
    return TZ_NAMES + TZ_NAME_OFF[i];
}

static const char *tz_rule(int i)
{
    return TZ_RULES + TZ_RULE_OFF[TZ_NAME_RULE[i]];
}

int cal_tz_table_size(void)
{
    return TZ_NAME_COUNT;
}

const char *cal_tz_table_name(int i, const char **posix)
{
    if (i < 0 || i >= TZ_NAME_COUNT) {
        if (posix) *posix = NULL;
        return NULL;
    }
    if (posix) *posix = tz_rule(i);
    return tz_name(i);
}

const char *cal_tz_posix_for(const char *iana)
{
    if (!iana || !*iana) return NULL;
    int lo = 0, hi = TZ_NAME_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(iana, tz_name(mid));
        if (!c) return tz_rule(mid);
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    for (int i = 0; i < TZ_NAME_COUNT; i++)
        if (!strcasecmp(iana, tz_name(i))) return tz_rule(i);
    return NULL;
}

/* ---------- offsets and conversions ---------- */

/* The change-over as local seconds (civil time read as UTC) in the given year. */
static tb_epoch_t rule_local(const cal_tz_rule_t *r, int year)
{
    int32_t days;
    if (r->kind == CAL_TZ_RULE_M) {
        int wd1 = cal_weekday(year, r->m, 1);
        int day = 1 + (r->d - wd1 + 7) % 7 + (r->w - 1) * 7;
        int dim = cal_days_in_month(year, r->m);
        while (day > dim) day -= 7;
        days = cal_days_from_civil(year, r->m, day);
    } else if (r->kind == CAL_TZ_RULE_J) {
        days = cal_days_from_civil(year, 1, 1) + r->yday - 1 + (cal_is_leap(year) && r->yday >= 60 ? 1 : 0);
    } else {
        days = cal_days_from_civil(year, 1, 1) + r->yday;
    }
    return (tb_epoch_t)days * 86400 + r->secs;
}

void cal_tz_transitions(const cal_tz_t *tz, int year, tb_epoch_t *dst_start, tb_epoch_t *dst_end)
{
    /* The start's time is in standard time, the end's in daylight time (the time in effect before each). */
    *dst_start = rule_local(&tz->start, year) - tz->std_offset;
    *dst_end = rule_local(&tz->end, year) - tz->dst_offset;
}

static int year_of(tb_epoch_t t)
{
    int y, m, d;
    cal_civil_from_days(floor_div(t, 86400), &y, &m, &d);
    return y;
}

static bool in_dst(const cal_tz_t *tz, tb_epoch_t utc)
{
    tb_epoch_t s, e;
    cal_tz_transitions(tz, year_of(utc + tz->std_offset), &s, &e);
    if (s == e) return false;
    if (s < e) return utc >= s && utc < e;
    return !(utc >= e && utc < s);     /* southern hemisphere, Dublin: the "dst" period spans the new year */
}

int32_t cal_tz_offset_at(const cal_tz_t *tz, tb_epoch_t utc)
{
    if (!tz->has_dst) return tz->std_offset;
    return in_dst(tz, utc) ? tz->dst_offset : tz->std_offset;
}

tb_epoch_t cal_tz_to_utc(const cal_tz_t *tz, const cal_civil_t *l)
{
    tb_epoch_t local = cal_civil_to_secs(l);
    if (!tz->has_dst || tz->dst_offset == tz->std_offset) return local - tz->std_offset;
    tb_epoch_t a = local - tz->std_offset, b = local - tz->dst_offset;
    bool va = cal_tz_offset_at(tz, a) == tz->std_offset;
    bool vb = cal_tz_offset_at(tz, b) == tz->dst_offset;
    if (va && vb) return a < b ? a : b;     /* overlap: the earlier instant */
    if (va) return a;
    if (vb) return b;
    return a > b ? a : b;                   /* gap: read with the offset before it, so the later instant */
}

void cal_tz_to_local(const cal_tz_t *tz, tb_epoch_t utc, cal_civil_t *out)
{
    cal_secs_to_civil(utc + cal_tz_offset_at(tz, utc), out);
}

tb_epoch_t cal_tz_midnight_after(const cal_tz_t *tz, tb_epoch_t utc, int days_ahead)
{
    cal_civil_t c;
    cal_tz_to_local(tz, utc, &c);
    int32_t days = cal_days_from_civil(c.year, c.month, c.day) + days_ahead;
    int y, m, d;
    cal_civil_from_days(days, &y, &m, &d);
    cal_civil_t mid = {.year = (int16_t)y, .month = (int8_t)m, .day = (int8_t)d};
    return cal_tz_to_utc(tz, &mid);
}

tb_epoch_t cal_tz_midnight(const cal_tz_t *tz, tb_epoch_t utc)
{
    return cal_tz_midnight_after(tz, utc, 0);
}
