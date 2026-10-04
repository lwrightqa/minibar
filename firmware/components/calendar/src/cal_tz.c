/*
 * cal_tz.c: civil-date arithmetic and POSIX time zones. Owner: calendar builder.
 * The lead wrote the date helpers (Howard Hinnant's days_from_civil); the zone functions are stubs.
 */
#include <string.h>

#include "cal_tz.h"

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

int cal_days_in_month(int y, int m)
{
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    return dim[(m - 1) % 12];
}

bool cal_tz_parse(const char *posix, cal_tz_t *out)
{
    memset(out, 0, sizeof(*out));
    (void)posix;
    return false;   /* TODO(calendar) */
}

const char *cal_tz_posix_for(const char *iana)
{
    if (!iana) return NULL;
    if (!strcmp(iana, "UTC") || !strcmp(iana, "Etc/UTC")) return "UTC0";
    return NULL;    /* TODO(calendar): the generated table */
}

int32_t cal_tz_offset_at(const cal_tz_t *tz, tb_epoch_t utc)
{
    (void)utc;
    return tz->std_offset;  /* TODO(calendar): DST rules */
}

tb_epoch_t cal_tz_to_utc(const cal_tz_t *tz, const cal_civil_t *l)
{
    tb_epoch_t local = (tb_epoch_t)cal_days_from_civil(l->year, l->month, l->day) * 86400 + l->hour * 3600 + l->min * 60 + l->sec;
    return local - tz->std_offset;  /* TODO(calendar): DST, gaps and overlaps */
}

void cal_tz_to_local(const cal_tz_t *tz, tb_epoch_t utc, cal_civil_t *out)
{
    tb_epoch_t t = utc + cal_tz_offset_at(tz, utc);
    int32_t days = (int32_t)(t >= 0 ? t / 86400 : (t - 86399) / 86400);
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
