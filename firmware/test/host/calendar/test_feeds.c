/*
 * test_feeds.c: realistic feeds (fixtures/make_fixtures.py writes them) read for the bar's two-day window.
 * Owner: calendar builder. Times in comments are Los Angeles time unless they end in Z.
 */
#include <stdlib.h>
#include <string.h>

#include "cal_fixture.h"

#define WEEK_UID_PRIYA "2p3ccf1u6v9rtlb4v5kq5o0g7d@google.com"
#define WEEK_UID_STANDUP "7kukuqrfedlm2f6ra5o5r2iqel@google.com"

/* ---------- google_week.ics, Monday 10/5 and Tuesday 10/6 ---------- */

static void check_week(const feed_result_t *r)
{
    TB_EQ_INT(r->err, CAL_OK);
    TB_EQ_INT(r->write_err, CAL_OK);
    TB_EQ_INT(r->n, 9);
    static const struct { int mo, d, h, mi, eh, emi; const char *title; } want[] = {
        {10, 5, 16, 0, 16, 30, "London sync"},          /* 17:00 London (BST) = 9:00 */
        {10, 5, 16, 30, 16, 45, "Daily standup"},       /* 9:30 */
        {10, 5, 17, 0, 17, 30, "1:1 Sam / Alex"},       /* 10:00, the new half of a split series */
        {10, 5, 20, 0, 20, 30, "1:1 Priya / Alex (moved)"},    /* 11:00 moved to 13:00 */
        {10, 6, 6, 30, 7, 30, "Release watch"},         /* 23:30 to 0:30, across midnight */
        {10, 6, 16, 0, 17, 0, "Monthly all-hands"},     /* first Tuesday, 9:00 */
        {10, 6, 18, 30, 19, 0, "Caf\xC3\xA9 ? sync"},   /* the coffee cup isn't in the fonts */
        {10, 6, 21, 0, 22, 0, ""},                      /* private: no title */
        {10, 6, 23, 0, 0, 0, "Quarterly planning (moved)"},    /* moved in from Thursday */
    };
    for (int i = 0; i < 9 && i < r->n; i++) {
        tb_epoch_t s = fx_utc(2026, want[i].mo, want[i].d, want[i].h, want[i].mi);
        TB_EQ_INT(r->m[i].start, s);
        tb_epoch_t e = fx_utc(2026, want[i].mo, want[i].d, want[i].eh, want[i].emi);
        if (e <= s) e += 86400;
        TB_EQ_INT(r->m[i].end, e);
        TB_EQ_STR(r->m[i].title, want[i].title);
    }
}

TB_TEST(google_week_reads_the_two_days)
{
    feed_result_t r;
    fx_run_file("google_week.ics", 2026, 10, 5, SELF_EMAIL, &r);
    check_week(&r);
    if (tb_test_failures) fx_dump(&r);
    /* what's not there */
    TB_TRUE(!fx_find(&r, "Team sync"));             /* the Tuesday instance is cancelled */
    TB_TRUE(!fx_find(&r, "Focus block"));           /* Free */
    TB_TRUE(!fx_find(&r, "Offsite planning"));      /* all day */
    TB_TRUE(!fx_find(&r, "Vendor demo"));           /* declined by you */
    TB_TRUE(!fx_find(&r, "Staff meeting"));         /* moved out to Friday */
    TB_TRUE(!fx_find(&r, "1:1 Priya / Alex"));      /* the master's Monday instance is replaced */
    TB_TRUE(!fx_find(&r, "Onboarding"));            /* COUNT=5 ended last week */
    TB_TRUE(!fx_find(&r, "Quarterly planning"));    /* Thursday is outside the window */
    TB_EQ_INT(fx_count(&r, "Daily standup"), 1);    /* Tuesday's is an EXDATE */
}

TB_TEST(google_week_details)
{
    feed_result_t r;
    fx_run_file("google_week.ics", 2026, 10, 5, SELF_EMAIL, &r);
    const tb_meeting_t *p = fx_find(&r, "1:1 Priya / Alex (moved)");
    TB_TRUE(p != NULL);
    if (p) {
        TB_EQ_STR(p->location, "Priya's desk");
        /* an override keeps the id of the instance it replaces (its RECURRENCE-ID: 11:00 = 18:00Z) */
        TB_EQ_INT(p->id, cal_instance_id(WEEK_UID_PRIYA, fx_utc(2026, 10, 5, 18, 0)));
        TB_FALSE(p->priv);
    }
    const tb_meeting_t *s = fx_find(&r, "Daily standup");
    if (s) TB_EQ_INT(s->id, cal_instance_id(WEEK_UID_STANDUP, fx_utc(2026, 10, 5, 16, 30)));
    const tb_meeting_t *priv = fx_at(&r, fx_utc(2026, 10, 6, 21, 0));
    TB_TRUE(priv != NULL);
    if (priv) {
        TB_TRUE(priv->priv);
        TB_EQ_STR(priv->title, "");
        TB_EQ_STR(priv->location, "");      /* "Board room" is never kept */
    }
    const tb_meeting_t *t = fx_find(&r, "Team sync");
    TB_TRUE(t == NULL);
    const tb_meeting_t *ah = fx_find(&r, "Monthly all-hands");
    if (ah) TB_EQ_STR(ah->location, "https://meet.google.com/kfq-zrvd-dxa");    /* core leaves web addresses out */
    /* ids are unique and never 0 */
    for (int i = 0; i < r.n; i++) {
        TB_TRUE(r.m[i].id != 0);
        for (int j = i + 1; j < r.n; j++) TB_TRUE(r.m[i].id != r.m[j].id);
    }
    TB_EQ_INT(r.stats.events, 20);
    TB_EQ_INT(r.stats.recurring, 10);
    TB_EQ_INT(r.stats.overrides, 4);
    TB_EQ_INT(r.stats.vtimezones, 2);
    TB_EQ_INT(r.stats.unknown_tzid, 0);
    TB_EQ_INT(r.stats.unsupported_rrule, 0);
    TB_EQ_INT(r.stats.cut_lines, 0);
    TB_TRUE(r.stats.ended);
}

TB_TEST(google_week_without_owner_email_keeps_declined)
{
    /* Without the owner's address (a non-Google feed), a declined event can't be told apart. */
    feed_result_t r;
    fx_run_file("google_week.ics", 2026, 10, 5, "", &r);
    TB_EQ_INT(r.n, 10);
    TB_TRUE(fx_find(&r, "Vendor demo") != NULL);
    /* the alarm's ATTENDEE (PARTSTAT=DECLINED) never counts as the event's, with or without the address */
    fx_run_file("google_week.ics", 2026, 10, 5, SELF_EMAIL, &r);
    TB_TRUE(fx_find(&r, "Daily standup") != NULL);
    /* the owner's address matches in any case */
    fx_run_file("google_week.ics", 2026, 10, 5, "Sam.Lee@Example.COM", &r);
    TB_EQ_INT(r.n, 9);
}

TB_TEST(google_week_next_days)
{
    feed_result_t r;
    /* Tuesday and Wednesday: the release watch (Mon 23:30 to Tue 0:30) is still in the window */
    fx_run_file("google_week.ics", 2026, 10, 6, SELF_EMAIL, &r);
    TB_TRUE(fx_find(&r, "Release watch") != NULL);
    TB_EQ_INT(fx_count(&r, "Daily standup"), 1);    /* Wednesday's */
    TB_TRUE(fx_at(&r, fx_utc(2026, 10, 7, 16, 30)) != NULL);
    TB_TRUE(!fx_find(&r, "London sync"));
    /* Thursday and Friday: the quarterly planning is gone from Thursday; the staff meeting moved to Friday */
    fx_run_file("google_week.ics", 2026, 10, 8, SELF_EMAIL, &r);
    TB_TRUE(!fx_find(&r, "Quarterly planning"));
    const tb_meeting_t *st = fx_find(&r, "Staff meeting");
    TB_TRUE(st != NULL);
    if (st) TB_EQ_INT(st->start, fx_utc(2026, 10, 9, 21, 0));
    TB_EQ_INT(fx_count(&r, "Daily standup"), 2);
    /* the next Monday: the 1:1 is back at 11:00, the staff meeting at 14:00 */
    fx_run_file("google_week.ics", 2026, 10, 12, SELF_EMAIL, &r);
    TB_TRUE(fx_at(&r, fx_utc(2026, 10, 12, 18, 0)) != NULL);
    TB_TRUE(fx_find(&r, "1:1 Priya / Alex") != NULL);
    TB_TRUE(fx_find(&r, "Staff meeting") != NULL);
    TB_TRUE(fx_find(&r, "Team sync") != NULL);  /* Tuesday 10/13 is a normal one */
}

TB_TEST(google_week_now_next_left)
{
    feed_result_t r;
    fx_run_file("google_week.ics", 2026, 10, 5, SELF_EMAIL, &r);
    cal_tz_t la = fx_tz(LA_POSIX);
    char buf[48];
    /* Monday 8:00 */
    tb_epoch_t t = fx_utc(2026, 10, 5, 15, 0);
    TB_TRUE(cal_today_current(r.m, r.n, t) == NULL);
    const tb_meeting_t *nx = cal_today_next(r.m, r.n, t, &la);
    TB_EQ_STR(nx ? nx->title : NULL, "London sync");
    TB_EQ_INT(cal_today_left(r.m, r.n, t, &la), 5);
    TB_EQ_STR(cal_today_left_text(r.m, r.n, t, &la, buf, sizeof buf), "5 meetings left today");
    /* 9:35: in the standup; London ended at 9:30 */
    t = fx_utc(2026, 10, 5, 16, 35);
    const tb_meeting_t *cur = cal_today_current(r.m, r.n, t);
    TB_EQ_STR(cur ? cur->title : NULL, "Daily standup");
    nx = cal_today_next(r.m, r.n, t, &la);
    TB_EQ_STR(nx ? nx->title : NULL, "1:1 Sam / Alex");
    TB_EQ_INT(cal_today_left(r.m, r.n, t, &la), 4);
    /* 23:45: in the release watch, nothing after it today */
    t = fx_utc(2026, 10, 6, 6, 45);
    cur = cal_today_current(r.m, r.n, t);
    TB_EQ_STR(cur ? cur->title : NULL, "Release watch");
    TB_TRUE(cal_today_next(r.m, r.n, t, &la) == NULL);
    TB_EQ_STR(cal_today_left_text(r.m, r.n, t, &la, buf, sizeof buf), "1 meeting left today");
    /* Tuesday 0:10: still in it (it began yesterday, so it isn't "left today"); next is the all-hands */
    t = fx_utc(2026, 10, 6, 7, 10);
    cur = cal_today_current(r.m, r.n, t);
    TB_EQ_STR(cur ? cur->title : NULL, "Release watch");
    nx = cal_today_next(r.m, r.n, t, &la);
    TB_EQ_STR(nx ? nx->title : NULL, "Monthly all-hands");
    TB_EQ_INT(cal_today_left(r.m, r.n, t, &la), 4);
    /* Tuesday 17:30: done for the day */
    t = fx_utc(2026, 10, 7, 0, 30);
    TB_EQ_STR(cal_today_left_text(r.m, r.n, t, &la, buf, sizeof buf), "no more meetings today");
    TB_TRUE(cal_today_next(r.m, r.n, t, &la) == NULL);
}

/* ---------- google_dst.ics ---------- */

TB_TEST(dst_eu_changed_us_not_yet)
{
    feed_result_t r;
    /* Thursday 10/22: both in summer time. London 9:00 BST = 8:00Z. */
    fx_run_file("google_dst.ics", 2026, 10, 22, SELF_EMAIL, &r);
    TB_EQ_INT(r.err, CAL_OK);
    TB_TRUE(fx_at(&r, fx_utc(2026, 10, 22, 8, 0)) != NULL);
    TB_TRUE(fx_at(&r, fx_utc(2026, 10, 23, 8, 0)) != NULL);
    const tb_meeting_t *dc = fx_find(&r, "Design crit");
    TB_TRUE(dc && dc->start == fx_utc(2026, 10, 22, 22, 0));
    /* Monday 10/26: London is back on GMT (9:00 = 9:00Z), Los Angeles still on PDT (9:00 = 16:00Z) */
    fx_run_file("google_dst.ics", 2026, 10, 26, SELF_EMAIL, &r);
    const tb_meeting_t *ls = fx_find(&r, "London standup");
    TB_TRUE(ls && ls->start == fx_utc(2026, 10, 26, 9, 0) && ls->end == fx_utc(2026, 10, 26, 9, 15));
    TB_EQ_INT(fx_count(&r, "London standup"), 2);
    const tb_meeting_t *wp = fx_find(&r, "Weekly planning");
    TB_TRUE(wp && wp->start == fx_utc(2026, 10, 26, 16, 0));
    TB_TRUE(fx_at(&r, fx_utc(2026, 10, 26, 8, 30)) != NULL);  /* night ops 1:30 PDT */
    /* every other week on Tuesday (weeks from Sunday 8/30): 10/27 is in an "on" week */
    const tb_meeting_t *bw = fx_find(&r, "Biweekly sync");
    TB_TRUE(bw && bw->start == fx_utc(2026, 10, 27, 22, 0));
    const tb_meeting_t *an = fx_find(&r, "Team anniversary");
    TB_TRUE(an && an->start == fx_utc(2026, 10, 27, 23, 0));
    /* ...and 10/20 is not */
    fx_run_file("google_dst.ics", 2026, 10, 20, SELF_EMAIL, &r);
    TB_TRUE(!fx_find(&r, "Biweekly sync"));
}

TB_TEST(dst_us_fall_back)
{
    feed_result_t r;
    /* Sunday 11/1 (a 25-hour day) and Monday 11/2. */
    fx_run_file("google_dst.ics", 2026, 11, 1, SELF_EMAIL, &r);
    TB_EQ_INT(r.err, CAL_OK);
    /* 1:30 happens twice on 11/1: the first one (PDT, 8:30Z) */
    const tb_meeting_t *n1 = fx_at(&r, fx_utc(2026, 11, 1, 8, 30));
    TB_TRUE(n1 && !strcmp(n1->title, "Night ops check"));
    TB_TRUE(fx_at(&r, fx_utc(2026, 11, 1, 9, 30)) == NULL);
    /* 11/2 1:30 PST = 9:30Z */
    TB_TRUE(fx_at(&r, fx_utc(2026, 11, 2, 9, 30)) != NULL);
    TB_EQ_INT(fx_count(&r, "Night ops check"), 2);
    /* the weekly 9:00 stays at 9:00 local: 17:00Z now */
    const tb_meeting_t *wp = fx_find(&r, "Weekly planning");
    TB_TRUE(wp && wp->start == fx_utc(2026, 11, 2, 17, 0) && wp->end == fx_utc(2026, 11, 2, 18, 0));
    /* UNTIL in UTC (11/2 7:59:59Z): 11/1 noon PST (20:00Z) is in, 11/2 noon isn't */
    TB_EQ_INT(fx_count(&r, "Lunch talk"), 1);
    const tb_meeting_t *lt = fx_find(&r, "Lunch talk");
    TB_TRUE(lt && lt->start == fx_utc(2026, 11, 1, 20, 0));
    /* the window itself is 49 hours: 11/1 0:00 PDT to 11/3 0:00 PST */
    tb_epoch_t ws, we;
    fx_la_window(2026, 11, 1, &ws, &we);
    TB_EQ_INT(ws, fx_utc(2026, 11, 1, 7, 0));
    TB_EQ_INT(we, fx_utc(2026, 11, 3, 8, 0));
}

TB_TEST(dst_exdate_with_tzid_after_the_change)
{
    feed_result_t r;
    fx_run_file("google_dst.ics", 2026, 11, 9, SELF_EMAIL, &r);
    TB_TRUE(!fx_find(&r, "Weekly planning"));       /* EXDATE;TZID=America/Los_Angeles:20261109T090000 */
    const tb_meeting_t *pc = fx_find(&r, "Product council");   /* second Tuesday, 11/10 11:00 PST */
    TB_TRUE(pc && pc->start == fx_utc(2026, 11, 10, 19, 0));
    const tb_meeting_t *bw = fx_find(&r, "Biweekly sync");
    TB_TRUE(bw && bw->start == fx_utc(2026, 11, 10, 23, 0));
    fx_run_file("google_dst.ics", 2026, 11, 16, SELF_EMAIL, &r);
    TB_TRUE(fx_find(&r, "Weekly planning") != NULL);    /* only 11/9 was skipped */
}

TB_TEST(dst_count_across_the_change)
{
    feed_result_t r;
    /* six Thursdays from 10/15: the sixth is 11/19 at 15:00 PST */
    fx_run_file("google_dst.ics", 2026, 11, 19, SELF_EMAIL, &r);
    const tb_meeting_t *dc = fx_find(&r, "Design crit");
    TB_TRUE(dc && dc->start == fx_utc(2026, 11, 19, 23, 0));
    fx_run_file("google_dst.ics", 2026, 11, 26, SELF_EMAIL, &r);
    TB_TRUE(!fx_find(&r, "Design crit"));
}

TB_TEST(dst_monthly_rules)
{
    feed_result_t r;
    fx_run_file("google_dst.ics", 2026, 10, 30, SELF_EMAIL, &r);
    const tb_meeting_t *dd = fx_find(&r, "Demo day");       /* last Friday of October */
    TB_TRUE(dd && dd->start == fx_utc(2026, 10, 30, 23, 0));
    const tb_meeting_t *ex = fx_find(&r, "Expense check");  /* the 31st */
    TB_TRUE(ex && ex->start == fx_utc(2026, 10, 31, 17, 0));
    TB_EQ_INT(fx_count(&r, "Lunch talk"), 2);
    fx_run_file("google_dst.ics", 2026, 11, 29, SELF_EMAIL, &r);
    TB_TRUE(!fx_find(&r, "Expense check"));                 /* November has no 31st */
    TB_TRUE(!fx_find(&r, "Demo day"));                      /* the last Friday is 11/27 */
    fx_run_file("google_dst.ics", 2026, 11, 27, SELF_EMAIL, &r);
    TB_TRUE(fx_find(&r, "Demo day") != NULL);
    fx_run_file("google_dst.ics", 2026, 12, 31, SELF_EMAIL, &r);
    TB_TRUE(fx_find(&r, "Expense check") != NULL);
}

TB_TEST(dst_spring_forward_gap)
{
    feed_result_t r;
    /* 3/8/2026: 2:30 doesn't exist; it's read with the offset before the gap, so it lands at 3:30 PDT (10:30Z) */
    fx_run_file("google_dst.ics", 2026, 3, 8, SELF_EMAIL, &r);
    const tb_meeting_t *es = fx_find(&r, "Early sync");
    TB_TRUE(es && es->start == fx_utc(2026, 3, 8, 10, 30) && es->end == fx_utc(2026, 3, 8, 11, 0));
    TB_TRUE(fx_at(&r, fx_utc(2026, 3, 9, 9, 30)) != NULL);  /* 3/9 2:30 PDT */
    TB_EQ_INT(fx_count(&r, "Early sync"), 2);
    TB_TRUE(!fx_find(&r, "London standup"));   /* it starts in September */
    tb_epoch_t ws, we;
    fx_la_window(2026, 3, 8, &ws, &we);
    TB_EQ_INT(we - ws, 47 * 3600);
    /* COUNT=5 from 3/6: 3/6, 3/7, 3/8, 3/9, 3/10 -- nothing on 3/11 */
    fx_run_file("google_dst.ics", 2026, 3, 11, SELF_EMAIL, &r);
    TB_TRUE(!fx_find(&r, "Early sync"));
}

/* ---------- outlook.ics (LF line ends, Windows zone names) ---------- */

TB_TEST(outlook_windows_zones_and_bysetpos)
{
    feed_result_t r;
    fx_run_file("outlook.ics", 2026, 10, 29, NULL, &r);
    TB_EQ_INT(r.err, CAL_OK);
    if (r.n != 7) fx_dump(&r);
    TB_EQ_INT(r.n, 7);
    const tb_meeting_t *m;
    m = fx_find(&r, "Architecture review");     /* every other Thursday from 9/3: 10/29 10:00 PDT */
    TB_TRUE(m && m->start == fx_utc(2026, 10, 29, 17, 0));
    if (m) TB_EQ_STR(m->location, "Conference Room 3");
    m = fx_find(&r, "Team call (Berlin)");      /* 17:00 CET (Europe changed back on 10/25) */
    TB_TRUE(m && m->start == fx_utc(2026, 10, 29, 16, 0));
    m = fx_find(&r, "New York check-in");       /* a Mozilla-style TZID: 14:00 EDT */
    TB_TRUE(m && m->start == fx_utc(2026, 10, 29, 18, 0));
    m = fx_find(&r, "Floating lunch");          /* no zone: the bar's own */
    TB_TRUE(m && m->start == fx_utc(2026, 10, 29, 19, 0));
    m = fx_find(&r, "Mystery zone");            /* unknown zone: the bar's own, counted */
    TB_TRUE(m && m->start == fx_utc(2026, 10, 29, 22, 0));
    m = fx_find(&r, "Bangalore handoff");       /* a custom zone that dropped daylight time: +05:30 */
    TB_TRUE(m && m->start == fx_utc(2026, 10, 30, 3, 30));
    m = fx_find(&r, "Month-end close");         /* the last weekday of October: Friday 10/30 */
    TB_TRUE(m && m->start == fx_utc(2026, 10, 30, 23, 0));
    TB_TRUE(!fx_find(&r, "Tentative hold"));
    TB_TRUE(!fx_find(&r, "Canceled: Budget sync"));
    TB_EQ_INT(r.stats.vtimezones, 3);
    TB_EQ_INT(r.stats.unknown_tzid, 1);
}

TB_TEST(outlook_bysetpos_other_months)
{
    feed_result_t r;
    /* the last weekday of January 2027 is Friday 1/29; of February 2027, Friday 2/26; of May 2027, Monday 5/31 */
    fx_run_file("outlook.ics", 2027, 1, 29, NULL, &r);
    TB_TRUE(fx_find(&r, "Month-end close") != NULL);
    fx_run_file("outlook.ics", 2027, 5, 30, NULL, &r);
    const tb_meeting_t *m = fx_find(&r, "Month-end close");
    TB_TRUE(m && m->start == fx_utc(2027, 5, 31, 23, 0));
    /* the architecture review's UNTIL (12/31 18:00Z) */
    fx_run_file("outlook.ics", 2026, 12, 24, NULL, &r);
    TB_TRUE(fx_find(&r, "Architecture review") != NULL);
    fx_run_file("outlook.ics", 2027, 1, 7, NULL, &r);
    TB_TRUE(!fx_find(&r, "Architecture review"));
}

/* ---------- other devices' zones ---------- */

TB_TEST(device_zone_changes_the_window_not_the_times)
{
    /* The same feed on a bar in London: the window is London's day, the meetings' instants don't move. */
    size_t len;
    char *data = fx_load("google_week.ics", &len);
    if (!data) return;
    cal_tz_t lon = fx_tz("GMT0BST,M3.5.0/1,M10.5.0");
    tb_epoch_t ws, we;
    cal_today_window(&lon, fx_utc(2026, 10, 5, 12, 0), &ws, &we);
    TB_EQ_INT(ws, fx_utc(2026, 10, 4, 23, 0));
    feed_result_t r;
    fx_run(data, len, 0, ws, we, "GMT0BST,M3.5.0/1,M10.5.0", SELF_EMAIL, &r);
    const tb_meeting_t *m = fx_find(&r, "London sync");
    TB_TRUE(m && m->start == fx_utc(2026, 10, 5, 16, 0));
    /* X-WR-TIMEZONE (Los Angeles) is the calendar's zone for floating times, whatever the bar's zone */
    TB_TRUE(fx_find(&r, "Daily standup") != NULL);
    free(data);
}
