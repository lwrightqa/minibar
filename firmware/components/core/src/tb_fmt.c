/*
 * tb_fmt.c: the mock-up's formatters. Owner: core builder.
 * The lead wrote first versions so ui and the tests have something real; check them against the mock-up
 * (fmt, fmtShort, ampm, mmss, hm, minsUp, span, ago) and extend the tests.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tb_fmt.h"

static struct tm local_tm(tb_epoch_t t)
{
    time_t tt = (time_t)t;
    struct tm tm;
    localtime_r(&tt, &tm);
    return tm;
}

static int hour12(const struct tm *tm)
{
    int h = tm->tm_hour % 12;
    return h == 0 ? 12 : h;
}

/* Time format (decisions.md "Time format (2026-10-07)"): 12-hour "3:30 PM" (hour without a zero, AM or PM) or 24-hour
 * "15:30" (zero-padded hour, no AM or PM; midnight is 00:00). h24 comes from settings.display.time_format. */
static void put_hm(char *buf, size_t cap, const struct tm *tm, bool h24, bool with_ampm)
{
    if (h24) snprintf(buf, cap, "%02d:%02d", tm->tm_hour, tm->tm_min);
    else if (with_ampm) snprintf(buf, cap, "%d:%02d %s", hour12(tm), tm->tm_min, tm->tm_hour < 12 ? "AM" : "PM");
    else snprintf(buf, cap, "%d:%02d", hour12(tm), tm->tm_min);
}

char *tb_fmt_time(char *buf, size_t cap, tb_epoch_t t, bool h24)
{
    struct tm tm = local_tm(t);
    put_hm(buf, cap, &tm, h24, true);
    return buf;
}

char *tb_fmt_time_short(char *buf, size_t cap, tb_epoch_t t, bool h24)
{
    struct tm tm = local_tm(t);
    put_hm(buf, cap, &tm, h24, false);
    return buf;
}

const char *tb_fmt_ampm(tb_epoch_t t, bool h24)
{
    if (h24) return "";
    struct tm tm = local_tm(t);
    return tm.tm_hour < 12 ? "AM" : "PM";
}

char *tb_fmt_hhmm(char *buf, size_t cap, int hh, int mm, bool h24)
{
    if (hh < 0 || hh > 23) hh = 0;
    if (mm < 0 || mm > 59) mm = 0;
    if (h24) snprintf(buf, cap, "%02d:%02d", hh, mm);
    else snprintf(buf, cap, "%d:%02d", hh % 12 == 0 ? 12 : hh % 12, mm);   /* no AM or PM, as "Back at 3:30" always was */
    return buf;
}

char *tb_fmt_mmss(char *buf, size_t cap, int32_t seconds)
{
    if (seconds < 0) seconds = 0;
    snprintf(buf, cap, "%d:%02d", (int)(seconds / 60), (int)(seconds % 60));
    return buf;
}

char *tb_fmt_hm(char *buf, size_t cap, int32_t minutes)
{
    if (minutes < 0) minutes = 0;
    if (minutes >= 60) snprintf(buf, cap, "%dh %dm", (int)(minutes / 60), (int)(minutes % 60));
    else snprintf(buf, cap, "%dm", (int)minutes);
    return buf;
}

int32_t tb_mins_up(tb_ms_t ms)
{
    int64_t m = (ms + 59999) / 60000;
    return m < 1 ? 1 : (int32_t)m;
}

char *tb_fmt_span(char *buf, size_t cap, tb_epoch_t a, tb_epoch_t b, bool h24)
{
    char x[16], y[16];
    /* 24-hour has no AM or PM to share: both ends are written whole. */
    if (!h24 && strcmp(tb_fmt_ampm(a, false), tb_fmt_ampm(b, false)) == 0)
        snprintf(buf, cap, "%s\xE2\x80\x93%s", tb_fmt_time_short(x, sizeof x, a, false), tb_fmt_time(y, sizeof y, b, false));
    else
        snprintf(buf, cap, "%s\xE2\x80\x93%s", tb_fmt_time(x, sizeof x, a, h24), tb_fmt_time(y, sizeof y, b, h24));
    return buf;
}

char *tb_fmt_ago(char *buf, size_t cap, tb_epoch_t then, tb_epoch_t now, bool short_form, bool h24)
{
    int64_t m = (now - then) / 60;
    char t[16];
    if (m < 1) snprintf(buf, cap, "just now");
    else if (m < 60) snprintf(buf, cap, "%d%s ago", (int)m, short_form ? "m" : " min");
    else snprintf(buf, cap, "at %s", tb_fmt_time(t, sizeof t, then, h24));
    return buf;
}

char *tb_fmt_date_long(char *buf, size_t cap, tb_epoch_t t)
{
    static const char *const days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *const months[] = {"January", "February", "March", "April", "May", "June", "July",
                                         "August", "September", "October", "November", "December"};
    struct tm tm = local_tm(t);
    snprintf(buf, cap, "%s, %s %d", days[tm.tm_wday], months[tm.tm_mon], tm.tm_mday);
    return buf;
}

int32_t tb_local_yyyymmdd(tb_epoch_t t)
{
    struct tm tm = local_tm(t);
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

tb_epoch_t tb_local_midnight(tb_epoch_t t)
{
    struct tm tm = local_tm(t);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return (tb_epoch_t)mktime(&tm);
}

char *tb_fmt_rfc3339(char *buf, size_t cap, tb_epoch_t t)
{
    struct tm tm = local_tm(t);
    /* The offset from UTC, computed portably (tm_gmtoff isn't everywhere). */
    time_t tt = (time_t)t;
    struct tm g;
    gmtime_r(&tt, &g);
    g.tm_isdst = tm.tm_isdst;
    long off = (long)difftime(mktime(&tm), mktime(&g));
    char sign = off < 0 ? '-' : '+';
    if (off < 0) off = -off;
    snprintf(buf, cap, "%04d-%02d-%02dT%02d:%02d:%02d%c%02ld:%02ld", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, sign, off / 3600, (off % 3600) / 60);
    return buf;
}
