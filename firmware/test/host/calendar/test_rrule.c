/* test_rrule.c: RRULE parsing and expansion. Owner: calendar builder. */
#include <string.h>

#include "cal_fixture.h"

#define MAX_OCC 400

typedef struct {
    cal_civil_t occ[MAX_OCC];
    int n;
    int stop_after;     /* 0 = never */
} occs_t;

static bool collect(void *ctx, const cal_civil_t *c)
{
    occs_t *o = ctx;
    if (o->n < MAX_OCC) o->occ[o->n] = *c;
    o->n++;
    return !(o->stop_after && o->n >= o->stop_after);
}

static cal_civil_t civ(int y, int mo, int d, int h, int mi)
{
    cal_civil_t c = {.year = (int16_t)y, .month = (int8_t)mo, .day = (int8_t)d, .hour = (int8_t)h, .min = (int8_t)mi};
    return c;
}

/* Expand rule from dtstart (UTC zone) over [ws, we). */
static int expand_utc(const char *rule, cal_civil_t dt, tb_epoch_t ws, tb_epoch_t we, occs_t *o)
{
    memset(o, 0, sizeof(*o));
    cal_rrule_t r;
    if (!cal_rrule_parse(rule, &r)) {
        TB_FAIL_AT("rule didn't parse: %s", rule);
        return -2;
    }
    cal_tz_t utc;
    cal_tz_utc(&utc);
    uint32_t steps = 100000;
    return cal_rrule_expand(&r, &dt, &utc, 1800, ws, we, &steps, collect, o);
}

static bool has(const occs_t *o, int y, int mo, int d)
{
    for (int i = 0; i < o->n && i < MAX_OCC; i++)
        if (o->occ[i].year == y && o->occ[i].month == mo && o->occ[i].day == d) return true;
    return false;
}

TB_TEST(rrule_parse)
{
    cal_rrule_t r;
    TB_TRUE(cal_rrule_parse("FREQ=WEEKLY;BYDAY=MO,WE;UNTIL=20261231T235959Z", &r));
    TB_EQ_INT(r.freq, CAL_FREQ_WEEKLY);
    TB_EQ_INT(r.n_byday, 2);
    TB_EQ_INT(r.byday[0].wd, 1);
    TB_EQ_INT(r.byday[1].wd, 3);
    TB_TRUE(r.has_until && r.until_is_utc);
    TB_EQ_INT(r.until_utc, fx_utc(2026, 12, 31, 23, 59) + 59);
    TB_EQ_INT(r.interval, 1);
    TB_EQ_INT(r.wkst, 1);
    TB_TRUE(cal_rrule_parse("freq=monthly;byday=-1fr;interval=2", &r));    /* case-insensitive */
    TB_EQ_INT(r.byday[0].ord, -1);
    TB_EQ_INT(r.byday[0].wd, 5);
    TB_EQ_INT(r.interval, 2);
    TB_TRUE(cal_rrule_parse("FREQ=YEARLY;BYMONTH=3,11;BYDAY=2SU", &r));
    TB_EQ_INT(r.bymonth_mask, (1 << 3) | (1 << 11));
    TB_TRUE(cal_rrule_parse("FREQ=DAILY;UNTIL=20261102", &r));
    TB_FALSE(r.until_is_utc);
    TB_EQ_INT(r.until.hour, 23);
    TB_TRUE(cal_rrule_parse("FREQ=MONTHLY;BYMONTHDAY=1,15,-1;COUNT=10", &r));
    TB_EQ_INT(r.n_bymonthday, 3);
    TB_EQ_INT(r.bymonthday[2], -1);
    TB_EQ_INT(r.count, 10);
    TB_TRUE(cal_rrule_parse("FREQ=WEEKLY;X-NAME=whatever;BYDAY=TU;", &r));     /* X- parts ignored, trailing ; */
    /* refused */
    TB_FALSE(cal_rrule_parse("", &r));
    TB_FALSE(cal_rrule_parse("BYDAY=MO", &r));                     /* no FREQ */
    TB_FALSE(cal_rrule_parse("FREQ=HOURLY", &r));
    TB_FALSE(cal_rrule_parse("FREQ=YEARLY;BYWEEKNO=20", &r));
    TB_FALSE(cal_rrule_parse("FREQ=YEARLY;BYYEARDAY=100", &r));
    TB_FALSE(cal_rrule_parse("FREQ=DAILY;BYHOUR=9,17", &r));
    TB_FALSE(cal_rrule_parse("FREQ=WEEKLY;BYDAY=2MO", &r));        /* ordinals only for MONTHLY and YEARLY */
    TB_FALSE(cal_rrule_parse("FREQ=WEEKLY;BYMONTHDAY=3", &r));
    TB_FALSE(cal_rrule_parse("FREQ=MONTHLY;BYDAY=6MO", &r));
    TB_FALSE(cal_rrule_parse("FREQ=MONTHLY;BYMONTHDAY=0", &r));
    TB_FALSE(cal_rrule_parse("FREQ=MONTHLY;BYMONTHDAY=32", &r));
    TB_FALSE(cal_rrule_parse("FREQ=DAILY;INTERVAL=0", &r));
    TB_FALSE(cal_rrule_parse("FREQ=DAILY;COUNT=x", &r));
    TB_FALSE(cal_rrule_parse("FREQ=DAILY;UNTIL=2026", &r));
    TB_FALSE(cal_rrule_parse("FREQ=DAILY;UNTIL=20261340", &r));
    TB_FALSE(cal_rrule_parse("FREQ=WEEKLY;BYDAY=XX", &r));
    TB_FALSE(cal_rrule_parse("FREQ=WEEKLY;BYDAY=", &r));
    TB_FALSE(cal_rrule_parse("FREQ", &r));
    TB_FALSE(cal_rrule_parse("FREQ=DAILY;RSCALE=CHINESE", &r));
    TB_EQ_INT(r.freq, CAL_FREQ_NONE);   /* a refusal clears out */
}

TB_TEST(rrule_daily_interval_count_until)
{
    occs_t o;
    tb_epoch_t ws = fx_utc(2026, 1, 1, 0, 0), we = fx_utc(2026, 2, 1, 0, 0);
    TB_EQ_INT(expand_utc("FREQ=DAILY;INTERVAL=3;COUNT=4", civ(2026, 1, 1, 9, 0), ws, we, &o), 4);
    TB_TRUE(has(&o, 2026, 1, 1) && has(&o, 2026, 1, 4) && has(&o, 2026, 1, 7) && has(&o, 2026, 1, 10));
    TB_EQ_INT(expand_utc("FREQ=DAILY;UNTIL=20260105T090000Z", civ(2026, 1, 1, 9, 0), ws, we, &o), 5);    /* inclusive */
    TB_EQ_INT(expand_utc("FREQ=DAILY;UNTIL=20260105T085959Z", civ(2026, 1, 1, 9, 0), ws, we, &o), 4);
    TB_EQ_INT(expand_utc("FREQ=DAILY;UNTIL=20260105", civ(2026, 1, 1, 9, 0), ws, we, &o), 5);           /* a DATE */
    /* weekdays only, through BYDAY on DAILY */
    TB_EQ_INT(expand_utc("FREQ=DAILY;BYDAY=MO,TU,WE,TH,FR", civ(2026, 1, 5, 9, 0), ws, fx_utc(2026, 1, 19, 0, 0), &o), 10);
    /* COUNT counts instances before the window too */
    TB_EQ_INT(expand_utc("FREQ=DAILY;COUNT=10", civ(2026, 1, 1, 9, 0), fx_utc(2026, 1, 8, 0, 0), we, &o), 3);
    TB_TRUE(has(&o, 2026, 1, 8) && has(&o, 2026, 1, 10) && !has(&o, 2026, 1, 11));
}

TB_TEST(rrule_weekly_wkst_matters)
{
    /* RFC 5545's own example: every other week on Tuesday and Sunday, from Tuesday 1997-08-05, 4 instances.
     * WKST=MO gives Aug 5, 10, 19, 24; WKST=SU gives Aug 5, 17, 19, 31. */
    occs_t o;
    tb_epoch_t ws = fx_utc(1997, 8, 1, 0, 0), we = fx_utc(1997, 10, 1, 0, 0);
    expand_utc("FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=TU,SU;WKST=MO", civ(1997, 8, 5, 9, 0), ws, we, &o);
    TB_EQ_INT(o.n, 4);
    TB_TRUE(has(&o, 1997, 8, 5) && has(&o, 1997, 8, 10) && has(&o, 1997, 8, 19) && has(&o, 1997, 8, 24));
    expand_utc("FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=TU,SU;WKST=SU", civ(1997, 8, 5, 9, 0), ws, we, &o);
    TB_EQ_INT(o.n, 4);
    TB_TRUE(has(&o, 1997, 8, 5) && has(&o, 1997, 8, 17) && has(&o, 1997, 8, 19) && has(&o, 1997, 8, 31));
    /* no BYDAY: DTSTART's weekday */
    expand_utc("FREQ=WEEKLY;COUNT=3", civ(2026, 10, 7, 9, 0), fx_utc(2026, 10, 1, 0, 0), fx_utc(2026, 12, 1, 0, 0), &o);
    TB_TRUE(has(&o, 2026, 10, 7) && has(&o, 2026, 10, 14) && has(&o, 2026, 10, 21));
    TB_EQ_INT(o.n, 3);
}

TB_TEST(rrule_monthly)
{
    occs_t o;
    tb_epoch_t ws = fx_utc(2026, 1, 1, 0, 0), we = fx_utc(2027, 1, 1, 0, 0);
    /* the 31st: only the seven months that have one */
    expand_utc("FREQ=MONTHLY", civ(2026, 1, 31, 10, 0), ws, we, &o);
    TB_EQ_INT(o.n, 7);
    TB_FALSE(has(&o, 2026, 2, 28));
    /* BYMONTHDAY=-1: the last day of each month */
    expand_utc("FREQ=MONTHLY;BYMONTHDAY=-1", civ(2026, 1, 31, 10, 0), ws, we, &o);
    TB_EQ_INT(o.n, 12);
    TB_TRUE(has(&o, 2026, 2, 28) && has(&o, 2026, 4, 30));
    /* second Tuesday, last Friday */
    expand_utc("FREQ=MONTHLY;BYDAY=2TU", civ(2026, 1, 13, 10, 0), ws, we, &o);
    TB_EQ_INT(o.n, 12);
    TB_TRUE(has(&o, 2026, 10, 13) && has(&o, 2026, 11, 10) && has(&o, 2026, 12, 8));
    expand_utc("FREQ=MONTHLY;BYDAY=-1FR", civ(2026, 1, 30, 10, 0), ws, we, &o);
    TB_TRUE(has(&o, 2026, 10, 30) && has(&o, 2026, 11, 27) && has(&o, 2026, 7, 31));
    /* the first and third Monday */
    expand_utc("FREQ=MONTHLY;BYDAY=1MO,3MO", civ(2026, 1, 5, 10, 0), ws, fx_utc(2026, 3, 1, 0, 0), &o);
    TB_EQ_INT(o.n, 4);
    TB_TRUE(has(&o, 2026, 1, 5) && has(&o, 2026, 1, 19) && has(&o, 2026, 2, 2) && has(&o, 2026, 2, 16));
    /* BYDAY and BYMONTHDAY together: Friday the 13th */
    expand_utc("FREQ=MONTHLY;BYDAY=FR;BYMONTHDAY=13", civ(2026, 2, 13, 10, 0), ws, we, &o);
    TB_EQ_INT(o.n, 3);
    TB_TRUE(has(&o, 2026, 2, 13) && has(&o, 2026, 3, 13) && has(&o, 2026, 11, 13));
    /* last weekday of the month */
    expand_utc("FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1", civ(2026, 1, 30, 10, 0), ws, we, &o);
    TB_EQ_INT(o.n, 12);
    TB_TRUE(has(&o, 2026, 5, 29) && has(&o, 2026, 8, 31) && has(&o, 2026, 10, 30));
    /* every 3 months */
    expand_utc("FREQ=MONTHLY;INTERVAL=3", civ(2026, 1, 15, 10, 0), ws, we, &o);
    TB_EQ_INT(o.n, 4);
    TB_TRUE(has(&o, 2026, 10, 15));
}

TB_TEST(rrule_yearly)
{
    occs_t o;
    tb_epoch_t ws = fx_utc(2020, 1, 1, 0, 0), we = fx_utc(2033, 1, 1, 0, 0);
    /* February 29: leap years only */
    expand_utc("FREQ=YEARLY", civ(2020, 2, 29, 10, 0), ws, we, &o);
    TB_EQ_INT(o.n, 4);  /* 2020, 2024, 2028, 2032 */
    TB_TRUE(has(&o, 2028, 2, 29));
    /* US Thanksgiving */
    expand_utc("FREQ=YEARLY;BYMONTH=11;BYDAY=4TH", civ(2020, 11, 26, 12, 0), ws, fx_utc(2027, 1, 1, 0, 0), &o);
    TB_TRUE(has(&o, 2026, 11, 26) && has(&o, 2025, 11, 27) && has(&o, 2021, 11, 25));
    /* BYMONTH with DTSTART's day */
    expand_utc("FREQ=YEARLY;BYMONTH=1,7", civ(2026, 1, 10, 12, 0), fx_utc(2026, 1, 1, 0, 0), fx_utc(2027, 1, 1, 0, 0), &o);
    TB_EQ_INT(o.n, 2);
    TB_TRUE(has(&o, 2026, 7, 10));
    /* BYDAY with an ordinal and no BYMONTH counts in the year: the 20th Monday of 2026 is 5/18 */
    expand_utc("FREQ=YEARLY;BYDAY=20MO", civ(2026, 5, 18, 12, 0), fx_utc(2026, 1, 1, 0, 0), fx_utc(2028, 1, 1, 0, 0), &o);
    TB_TRUE(has(&o, 2026, 5, 18) && has(&o, 2027, 5, 17));
    /* every other year */
    expand_utc("FREQ=YEARLY;INTERVAL=2", civ(2020, 10, 27, 16, 0), ws, we, &o);
    TB_TRUE(has(&o, 2026, 10, 27) && !has(&o, 2027, 10, 27));
}

TB_TEST(rrule_dtstart_counts_even_off_rule)
{
    /* RFC 5545: DTSTART is the first instance even when it doesn't match (a Wednesday for a Monday rule). */
    occs_t o;
    expand_utc("FREQ=WEEKLY;BYDAY=MO;COUNT=3", civ(2026, 10, 7, 9, 0), fx_utc(2026, 10, 1, 0, 0),
               fx_utc(2026, 12, 1, 0, 0), &o);
    TB_EQ_INT(o.n, 3);
    TB_TRUE(has(&o, 2026, 10, 7) && has(&o, 2026, 10, 12) && has(&o, 2026, 10, 19));
    TB_FALSE(has(&o, 2026, 10, 26));
}

TB_TEST(rrule_window_and_duration)
{
    /* An instance that started before the window and is still on counts (a 2-hour meeting at 23:00). */
    cal_rrule_t r;
    TB_TRUE(cal_rrule_parse("FREQ=DAILY", &r));
    cal_tz_t utc;
    cal_tz_utc(&utc);
    occs_t o = {0};
    cal_civil_t dt = civ(2026, 1, 1, 23, 0);
    uint32_t steps = 1000;
    int n = cal_rrule_expand(&r, &dt, &utc, 7200, fx_utc(2026, 10, 5, 0, 0), fx_utc(2026, 10, 6, 0, 0), &steps, collect, &o);
    TB_EQ_INT(n, 2);
    TB_TRUE(has(&o, 2026, 10, 4) && has(&o, 2026, 10, 5));
    /* ...but one that ended exactly at the window's start doesn't */
    memset(&o, 0, sizeof(o));
    steps = 1000;
    n = cal_rrule_expand(&r, &dt, &utc, 3600, fx_utc(2026, 10, 5, 0, 0), fx_utc(2026, 10, 6, 0, 0), &steps, collect, &o);
    TB_EQ_INT(n, 1);
    /* fn can stop the walk */
    memset(&o, 0, sizeof(o));
    o.stop_after = 2;
    steps = 1000;
    n = cal_rrule_expand(&r, &dt, &utc, 60, fx_utc(2026, 10, 1, 0, 0), fx_utc(2026, 11, 1, 0, 0), &steps, collect, &o);
    TB_EQ_INT(n, 2);
}

TB_TEST(rrule_old_rules_are_cheap)
{
    /* A daily rule from 1990 (no COUNT) skips straight to the window: a handful of steps. */
    cal_rrule_t r;
    TB_TRUE(cal_rrule_parse("FREQ=DAILY", &r));
    cal_tz_t la = fx_tz(LA_POSIX);
    occs_t o = {0};
    cal_civil_t dt = civ(1990, 1, 1, 9, 0);
    uint32_t steps = 100;
    int n = cal_rrule_expand(&r, &dt, &la, 1800, fx_utc(2026, 10, 5, 7, 0), fx_utc(2026, 10, 7, 7, 0), &steps, collect, &o);
    TB_EQ_INT(n, 2);
    TB_TRUE(steps > 90);
    TB_TRUE(has(&o, 2026, 10, 5) && has(&o, 2026, 10, 6));
    /* the same with COUNT has to walk (and the budget stops a hostile one) */
    TB_TRUE(cal_rrule_parse("FREQ=DAILY;COUNT=1000000", &r));
    steps = 5000;
    memset(&o, 0, sizeof(o));
    n = cal_rrule_expand(&r, &dt, &la, 1800, fx_utc(2026, 10, 5, 7, 0), fx_utc(2026, 10, 7, 7, 0), &steps, collect, &o);
    TB_EQ_INT(n, -1);
    TB_EQ_INT(steps, 0);
    /* a rule that never matches ends at the window instead of running forever */
    TB_TRUE(cal_rrule_parse("FREQ=YEARLY;BYMONTH=2;BYMONTHDAY=30", &r));
    steps = 100000;
    memset(&o, 0, sizeof(o));
    n = cal_rrule_expand(&r, &dt, &la, 1800, fx_utc(2026, 10, 5, 7, 0), fx_utc(2026, 10, 7, 7, 0), &steps, collect, &o);
    TB_EQ_INT(n, 0);
    TB_TRUE(steps > 99000);
}

TB_TEST(rrule_local_time_across_dst)
{
    /* Weekly 9:00 Los Angeles from September: 16:00Z in October, 17:00Z in November. */
    cal_rrule_t r;
    TB_TRUE(cal_rrule_parse("FREQ=WEEKLY;BYDAY=MO", &r));
    cal_tz_t la = fx_tz(LA_POSIX);
    occs_t o = {0};
    cal_civil_t dt = civ(2026, 9, 7, 9, 0);
    uint32_t steps = 1000;
    cal_rrule_expand(&r, &dt, &la, 3600, fx_utc(2026, 10, 25, 0, 0), fx_utc(2026, 11, 10, 0, 0), &steps, collect, &o);
    TB_EQ_INT(o.n, 3);
    for (int i = 0; i < o.n; i++) TB_EQ_INT(o.occ[i].hour, 9);
    TB_EQ_INT(cal_tz_to_utc(&la, &o.occ[0]), fx_utc(2026, 10, 26, 16, 0));
    TB_EQ_INT(cal_tz_to_utc(&la, &o.occ[1]), fx_utc(2026, 11, 2, 17, 0));
}
