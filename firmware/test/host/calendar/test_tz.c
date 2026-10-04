/* test_tz.c: POSIX zones, offsets, local <-> UTC through gaps and overlaps, and the IANA table. Owner: calendar
 * builder. */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cal_fixture.h"

static cal_civil_t civ(int y, int mo, int d, int h, int mi)
{
    cal_civil_t c = {.year = (int16_t)y, .month = (int8_t)mo, .day = (int8_t)d, .hour = (int8_t)h, .min = (int8_t)mi};
    return c;
}

TB_TEST(tz_parse_forms)
{
    cal_tz_t z;
    TB_TRUE(cal_tz_parse("UTC0", &z));
    TB_EQ_INT(z.std_offset, 0);
    TB_FALSE(z.has_dst);
    TB_TRUE(cal_tz_parse("PST8PDT,M3.2.0,M11.1.0", &z));
    TB_EQ_INT(z.std_offset, -8 * 3600);
    TB_EQ_INT(z.dst_offset, -7 * 3600);
    TB_TRUE(z.has_dst);
    TB_EQ_INT(z.start.m, 3);
    TB_EQ_INT(z.start.w, 2);
    TB_EQ_INT(z.start.secs, 7200);
    TB_TRUE(cal_tz_parse("IST-5:30", &z));
    TB_EQ_INT(z.std_offset, 5 * 3600 + 1800);
    TB_TRUE(cal_tz_parse("<+0545>-5:45", &z));
    TB_EQ_INT(z.std_offset, 5 * 3600 + 45 * 60);
    TB_TRUE(cal_tz_parse("<-03>3", &z));
    TB_EQ_INT(z.std_offset, -3 * 3600);
    TB_TRUE(cal_tz_parse("EST5EDT", &z));      /* no rules: the US ones */
    TB_TRUE(z.has_dst);
    TB_EQ_INT(z.end.m, 11);
    TB_TRUE(cal_tz_parse("<+1030>-10:30<+11>-11,M10.1.0,M4.1.0", &z));     /* Lord Howe: a 30-minute change */
    TB_EQ_INT(z.dst_offset - z.std_offset, 1800);
    TB_TRUE(cal_tz_parse("IST-1GMT0,M10.5.0,M3.5.0/1", &z));    /* Dublin: "daylight" is the winter, 1 h less */
    TB_EQ_INT(z.std_offset, 3600);
    TB_EQ_INT(z.dst_offset, 0);
    TB_TRUE(cal_tz_parse("IST-2IDT,M3.4.4/26,M10.5.0", &z));    /* Jerusalem: 26:00 on a Thursday */
    TB_EQ_INT(z.start.secs, 26 * 3600);
    TB_TRUE(cal_tz_parse("<-02>2<-01>,M3.5.0/-1,M10.5.0/0", &z));   /* Nuuk: a negative time */
    TB_EQ_INT(z.start.secs, -3600);
    TB_TRUE(cal_tz_parse("EST5EDT,J60/2,300/2", &z));
    TB_EQ_INT(z.start.kind, CAL_TZ_RULE_J);
    TB_EQ_INT(z.end.kind, CAL_TZ_RULE_N);
    /* refused */
    TB_FALSE(cal_tz_parse("", &z));
    TB_FALSE(cal_tz_parse(NULL, &z));
    TB_FALSE(cal_tz_parse("America/Los_Angeles", &z));
    TB_FALSE(cal_tz_parse(":Europe/Paris", &z));
    TB_FALSE(cal_tz_parse("PS8", &z));
    TB_FALSE(cal_tz_parse("PST8PDT,M13.2.0,M11.1.0", &z));
    TB_FALSE(cal_tz_parse("PST8PDT,M3.2.0", &z));
    TB_FALSE(cal_tz_parse("PST8PDT,M3.2.0,M11.1.0junk", &z));
    TB_FALSE(cal_tz_parse("<+05", &z));
    TB_EQ_INT(z.std_offset, 0);    /* a refusal leaves UTC */
}

TB_TEST(tz_us_transitions_2026)
{
    cal_tz_t la = fx_tz(LA_POSIX);
    tb_epoch_t s, e;
    cal_tz_transitions(&la, 2026, &s, &e);
    TB_EQ_INT(s, fx_utc(2026, 3, 8, 10, 0));   /* 2:00 PST */
    TB_EQ_INT(e, fx_utc(2026, 11, 1, 9, 0));   /* 2:00 PDT */
    TB_EQ_INT(cal_tz_offset_at(&la, s - 1), -8 * 3600);
    TB_EQ_INT(cal_tz_offset_at(&la, s), -7 * 3600);
    TB_EQ_INT(cal_tz_offset_at(&la, e - 1), -7 * 3600);
    TB_EQ_INT(cal_tz_offset_at(&la, e), -8 * 3600);
    TB_EQ_INT(cal_tz_offset_at(&la, fx_utc(2026, 1, 1, 0, 0)), -8 * 3600);
    TB_EQ_INT(cal_tz_offset_at(&la, fx_utc(2026, 7, 4, 0, 0)), -7 * 3600);
}

TB_TEST(tz_gap_and_overlap)
{
    cal_tz_t la = fx_tz(LA_POSIX);
    /* the gap: 2:30 on 3/8 doesn't exist; read with PST it's 3:30 PDT */
    cal_civil_t g = civ(2026, 3, 8, 2, 30);
    TB_EQ_INT(cal_tz_to_utc(&la, &g), fx_utc(2026, 3, 8, 10, 30));
    cal_civil_t back;
    cal_tz_to_local(&la, fx_utc(2026, 3, 8, 10, 30), &back);
    TB_EQ_INT(back.hour, 3);
    TB_EQ_INT(back.min, 30);
    /* just before and after the gap */
    cal_civil_t a = civ(2026, 3, 8, 1, 59);
    TB_EQ_INT(cal_tz_to_utc(&la, &a), fx_utc(2026, 3, 8, 9, 59));
    cal_civil_t b = civ(2026, 3, 8, 3, 0);
    TB_EQ_INT(cal_tz_to_utc(&la, &b), fx_utc(2026, 3, 8, 10, 0));
    /* the overlap: 1:30 on 11/1 happens twice; the first (PDT) */
    cal_civil_t o = civ(2026, 11, 1, 1, 30);
    TB_EQ_INT(cal_tz_to_utc(&la, &o), fx_utc(2026, 11, 1, 8, 30));
    cal_tz_to_local(&la, fx_utc(2026, 11, 1, 9, 30), &back);    /* the second 1:30 */
    TB_EQ_INT(back.hour, 1);
    TB_EQ_INT(back.min, 30);
    /* Dublin's overlap is in October, between IST and GMT: the first is IST */
    cal_tz_t dub = fx_tz("IST-1GMT0,M10.5.0,M3.5.0/1");
    cal_civil_t d = civ(2026, 10, 25, 1, 30);
    TB_EQ_INT(cal_tz_to_utc(&dub, &d), fx_utc(2026, 10, 25, 0, 30));
    TB_EQ_INT(cal_tz_offset_at(&dub, fx_utc(2026, 7, 1, 12, 0)), 3600);
    TB_EQ_INT(cal_tz_offset_at(&dub, fx_utc(2026, 12, 1, 12, 0)), 0);
}

TB_TEST(tz_southern_hemisphere)
{
    cal_tz_t syd = fx_tz("AEST-10AEDT,M10.1.0,M4.1.0/3");
    TB_EQ_INT(cal_tz_offset_at(&syd, fx_utc(2026, 1, 15, 0, 0)), 11 * 3600);
    TB_EQ_INT(cal_tz_offset_at(&syd, fx_utc(2026, 7, 15, 0, 0)), 10 * 3600);
    /* 2026-10-04 2:00 AEST -> 3:00 AEDT (Sunday 10/4 is the first Sunday of October) */
    tb_epoch_t s, e;
    cal_tz_transitions(&syd, 2026, &s, &e);
    TB_EQ_INT(s, fx_utc(2026, 10, 3, 16, 0));
    TB_EQ_INT(e, fx_utc(2026, 4, 4, 16, 0));   /* 2026-04-05 3:00 AEDT */
    /* Santiago changes at midnight: midnight on 9/6/2026 doesn't exist; the day starts at 1:00 */
    cal_tz_t scl = fx_tz("<-04>4<-03>,M9.1.6/24,M4.1.6/24");
    tb_epoch_t mid = cal_tz_midnight(&scl, fx_utc(2026, 9, 6, 15, 0));
    TB_EQ_INT(mid, fx_utc(2026, 9, 6, 4, 0));
    cal_civil_t c;
    cal_tz_to_local(&scl, mid, &c);
    TB_EQ_INT(c.day, 6);
    TB_EQ_INT(c.hour, 1);
}

TB_TEST(tz_midnights_and_day_lengths)
{
    cal_tz_t la = fx_tz(LA_POSIX);
    TB_EQ_INT(cal_tz_midnight(&la, fx_utc(2026, 10, 5, 15, 0)), fx_utc(2026, 10, 5, 7, 0));
    TB_EQ_INT(cal_tz_midnight(&la, fx_utc(2026, 10, 6, 6, 59)), fx_utc(2026, 10, 5, 7, 0));    /* still Monday */
    TB_EQ_INT(cal_tz_midnight(&la, fx_utc(2026, 10, 6, 7, 0)), fx_utc(2026, 10, 6, 7, 0));
    /* 11/1 is 25 hours long, 3/8 is 23 */
    tb_epoch_t a = cal_tz_midnight(&la, fx_utc(2026, 11, 1, 12, 0));
    TB_EQ_INT(cal_tz_midnight_after(&la, a, 1) - a, 25 * 3600);
    a = cal_tz_midnight(&la, fx_utc(2026, 3, 8, 12, 0));
    TB_EQ_INT(cal_tz_midnight_after(&la, a, 1) - a, 23 * 3600);
    /* across a year */
    TB_EQ_INT(cal_tz_midnight_after(&la, fx_utc(2026, 12, 31, 20, 0), 1), fx_utc(2027, 1, 1, 8, 0));
}

TB_TEST(tz_matches_the_c_library_for_many_zones)
{
    /* Compare offsets and local times with glibc's own implementation of the same POSIX rules, every 3 hours over
     * 2026 and 2027, for a spread of zones (both hemispheres, half hours, odd change times). */
    static const char *const zones[] = {
        "PST8PDT,M3.2.0,M11.1.0", "EST5EDT,M3.2.0,M11.1.0", "GMT0BST,M3.5.0/1,M10.5.0", "CET-1CEST,M3.5.0,M10.5.0/3",
        "AEST-10AEDT,M10.1.0,M4.1.0/3", "NZST-12NZDT,M9.5.0,M4.1.0/3", "IST-5:30", "<+0545>-5:45",
        "IST-1GMT0,M10.5.0,M3.5.0/1", "IST-2IDT,M3.4.4/26,M10.5.0", "<-02>2<-01>,M3.5.0/-1,M10.5.0/0",
        "<-04>4<-03>,M9.1.6/24,M4.1.6/24", "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0", "EET-2EEST,M3.5.4/24,M10.5.5/1",
        "<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45", "AKST9AKDT,M3.2.0,M11.1.0", "HST10",
    };
    const char *old = getenv("TZ");
    char saved[128] = "";
    if (old) strncpy(saved, old, sizeof(saved) - 1);
    for (size_t z = 0; z < sizeof(zones) / sizeof(zones[0]); z++) {
        setenv("TZ", zones[z], 1);
        tzset();
        cal_tz_t tz = fx_tz(zones[z]);
        int bad = 0;
        for (tb_epoch_t t = fx_utc(2026, 1, 1, 0, 0); t < fx_utc(2028, 1, 1, 0, 0) && bad < 3; t += 3 * 3600 + 17) {
            time_t tt = (time_t)t;
            struct tm tm;
            localtime_r(&tt, &tm);
            if (cal_tz_offset_at(&tz, t) != tm.tm_gmtoff) {
                TB_FAIL_AT("%s at %lld: %d != %ld", zones[z], (long long)t, (int)cal_tz_offset_at(&tz, t), tm.tm_gmtoff);
                bad++;
            }
            /* and back: local -> UTC gives t whenever the local time is unambiguous */
            cal_civil_t c;
            cal_tz_to_local(&tz, t, &c);
            tb_epoch_t u = cal_tz_to_utc(&tz, &c);
            if (u != t && cal_tz_offset_at(&tz, t - 7200) == cal_tz_offset_at(&tz, t + 7200)) {
                TB_FAIL_AT("%s round trip at %lld: %lld", zones[z], (long long)t, (long long)u);
                bad++;
            }
        }
    }
    if (saved[0]) setenv("TZ", saved, 1);
    else unsetenv("TZ");
    tzset();
}

TB_TEST(tz_table)
{
    TB_EQ_STR(cal_tz_posix_for("America/Los_Angeles"), "PST8PDT,M3.2.0,M11.1.0");
    TB_EQ_STR(cal_tz_posix_for("Europe/London"), "GMT0BST,M3.5.0/1,M10.5.0");
    TB_EQ_STR(cal_tz_posix_for("Asia/Kolkata"), "IST-5:30");
    TB_EQ_STR(cal_tz_posix_for("Asia/Calcutta"), "IST-5:30");     /* a link some browsers still report */
    TB_EQ_STR(cal_tz_posix_for("US/Pacific"), "PST8PDT,M3.2.0,M11.1.0");
    TB_EQ_STR(cal_tz_posix_for("UTC"), "UTC0");
    TB_EQ_STR(cal_tz_posix_for("Etc/UTC"), "UTC0");
    TB_EQ_STR(cal_tz_posix_for("america/new_york"), "EST5EDT,M3.2.0,M11.1.0");  /* any capitalization */
    TB_TRUE(cal_tz_posix_for("Mars/Olympus_Mons") == NULL);
    TB_TRUE(cal_tz_posix_for("") == NULL);
    TB_TRUE(cal_tz_posix_for(NULL) == NULL);
    TB_TRUE(cal_tz_posix_for("Factory") == NULL);
    TB_TRUE(cal_tz_table_size() > 500);
}

TB_TEST(tz_table_every_entry_parses_and_is_found)
{
    int n = cal_tz_table_size();
    const char *prev = NULL;
    for (int i = 0; i < n; i++) {
        const char *posix;
        const char *name = cal_tz_table_name(i, &posix);
        TB_TRUE(name && posix);
        if (!name || !posix) break;
        cal_tz_t tz;
        if (!cal_tz_parse(posix, &tz)) TB_FAIL_AT("%s: %s doesn't parse", name, posix);
        if (prev && strcmp(prev, name) >= 0) TB_FAIL_AT("table not sorted at %s", name);
        if (cal_tz_posix_for(name) != posix) TB_FAIL_AT("%s isn't found by binary search", name);
        prev = name;
    }
    TB_TRUE(cal_tz_table_name(n, NULL) == NULL);
}

TB_TEST(tz_civil_helpers)
{
    cal_civil_t c;
    cal_secs_to_civil(-1, &c);
    TB_EQ_INT(c.year, 1969);
    TB_EQ_INT(c.hour, 23);
    TB_EQ_INT(c.sec, 59);
    cal_civil_t x = civ(2026, 2, 29, 0, 0);
    TB_FALSE(cal_civil_valid(&x));
    x = civ(2028, 2, 29, 23, 59);
    TB_TRUE(cal_civil_valid(&x));
    TB_EQ_INT(cal_civil_to_secs(&x), fx_utc(2028, 2, 29, 23, 59));
    cal_civil_t y = civ(2028, 3, 1, 0, 0);
    TB_EQ_INT(cal_civil_cmp(&x, &y), -1);
    TB_EQ_INT(cal_civil_cmp(&y, &x), 1);
    TB_EQ_INT(cal_civil_cmp(&x, &x), 0);
    TB_EQ_INT(cal_days_in_month(2026, 13), 0);
}
