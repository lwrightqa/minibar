/*
 * test_sec_review.c: the 2026-10-04 defensive security review's calendar checks, now part of the suite, with the
 * checks added when its findings were fixed. Owner: lead developer.
 *
 * The secret address is never shown in full (decisions.md, api.md 11.1), and a hostile feed can't hold the CPU:
 * recurrence walks are charged by the days they examine, and the reader calls the device's tick from inside them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cal_fixture.h"
#include "cal_ics.h"
#include "cal_rrule.h"
#include "cal_url.h"
#include "tb_test.h"

#define ELLIPSIS_ICS "\xE2\x80\xA6.ics"

/* ======================================================================================================== */
/* 2. The masked address                                                                                    */
/* ======================================================================================================== */

/* The review: a feed whose private token is the file name itself got the whole token back in the masked form's
 * `file` (up to 63 bytes), which GET /api/v1/calendar returns to every paired device. */
TB_TEST(sec_masked_address_never_carries_the_token_in_file)
{
    static cal_url_info_t info;
    TB_EQ_INT(cal_url_check("https://cal.example.com/feeds/8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a.ics", &info), CAL_URL_OK);
    TB_TRUE(strstr(info.file, "8c1d5e2a9b7f") == NULL);
    TB_EQ_STR(info.file, ELLIPSIS_ICS);
    TB_EQ_STR(info.ending, "3f2a");     /* the token's last four, so two addresses can still be told apart */
    TB_EQ_STR(info.host, "cal.example.com");
}

/* Google's address keeps the token out of host and file, and ending is 4 characters. */
TB_TEST(sec_masked_google_address)
{
    static cal_url_info_t info;
    TB_EQ_INT(cal_url_check("https://calendar.google.com/calendar/ical/you%40example.com/private-8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a/basic.ics", &info), CAL_URL_OK);
    TB_EQ_STR(info.file, "basic.ics");
    TB_EQ_STR(info.ending, "3f2a");
    TB_TRUE(strstr(info.host, "8c1d") == NULL);
}

TB_TEST(sec_masked_file_names_and_tokens)
{
    static cal_url_info_t info;
    /* names show */
    static const char *names[] = {"basic", "calendar", "Calendar", "team-standup", "0", "holidays_2026", "MyCalendar",
                                  "events", "invite", "feed", "export", "schedule", "ICAL"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char url[160], want[64];
        snprintf(url, sizeof url, "https://cal.example.com/u/1234abcd/%s.ics", names[i]);
        snprintf(want, sizeof want, "%s.ics", names[i]);
        TB_EQ_INT(cal_url_check(url, &info), CAL_URL_OK);
        TB_EQ_STR(info.file, want);
    }
    /* anything long or random-looking doesn't */
    static const char *tokens[] = {
        "8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a",     /* hex */
        "a1b2c3d4",                             /* letters and digits run together */
        "xkqzvbnm",                             /* no vowels to speak of */
        "Zm9vYmFyYmF6cXV4",                     /* base64 */
        "aBcDeFgH",                             /* mixed case all through */
        "kibotanulemapo",                       /* a word over 10 letters */
        "12345678",                             /* more than 4 digits */
        "My%20Calendar",                        /* percent-encoded: can't tell, so hidden */
        "tok_ABCDEFGHJKLM",
        "AbCdEfGhIjKlMnOpQrStUvWx",
    };
    for (size_t i = 0; i < sizeof tokens / sizeof tokens[0]; i++) {
        char url[200];
        snprintf(url, sizeof url, "https://cal.example.com/u/%s.ics", tokens[i]);
        TB_EQ_INT(cal_url_check(url, &info), CAL_URL_OK);
        TB_EQ_STR(info.file, ELLIPSIS_ICS);
        size_t n = strlen(tokens[i]);
        TB_EQ_STR(info.ending, tokens[i] + n - 4);
    }
}

/* Whatever a provider's address looks like, no 8 characters of its secret part reach the masked form. */
TB_TEST(sec_masked_form_never_holds_8_characters_of_the_secret)
{
    static const struct { const char *url, *secret; } cases[] = {
        {"https://calendar.google.com/calendar/ical/you%40example.com/private-8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a/basic.ics",
         "8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a"},
        {"https://outlook.office365.com/owa/calendar/9f8e7d6c4b3a@example.com/Q0FMRU5EQVJTRUNSRVQxMjM0NTY3ODk/calendar.ics",
         "Q0FMRU5EQVJTRUNSRVQxMjM0NTY3ODk"},
        {"webcal://p52-caldav.icloud.com/published/2/MTIzNDU2Nzg5MDEyMzQ1NjE2NzE4MTkyMDIx.ics",
         "MTIzNDU2Nzg5MDEyMzQ1NjE2NzE4MTkyMDIx"},
        {"https://trello.com/calendar/5f8e2c1a9b3d4e7f6a0b1c2d/5f8e2c1a9b3d4e7f6a0b1c2e/0f1e2d3c4b5a69788796a5b4c3d2e1f0.ics",
         "0f1e2d3c4b5a69788796a5b4c3d2e1f0"},
        {"https://app.asana.com/-/calendar/1199887766554433/a3f8c2e19b7d4f60.ics", "a3f8c2e19b7d4f60"},
        {"https://ics.teamup.com/feed/ksd8f7g6h5j4k3l2/0.ics", "ksd8f7g6h5j4k3l2"},
        {"https://user.fm/calendar/v1-b7e2a9c4d1f8e3a6/Calendar.ics", "b7e2a9c4d1f8e3a6"},
    };
    static cal_url_info_t info;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        TB_EQ_INT(cal_url_check(cases[i].url, &info), CAL_URL_OK);
        char shown[256];
        snprintf(shown, sizeof shown, "%s/%s/%s", info.host, info.file, info.ending);
        const char *sec = cases[i].secret;
        size_t n = strlen(sec);
        bool leaked = false;
        for (size_t k = 0; k + 8 <= n; k++) {
            char part[9];
            memcpy(part, sec + k, 8);
            part[8] = '\0';
            if (strstr(shown, part)) leaked = true;
        }
        TB_FALSE(leaked);
        if (leaked) printf("    case %zu shows %s\n", i, shown);
        TB_EQ_INT(strlen(info.ending), 4);
    }
}

/* Holds today: the reader stays in bounds on a long folded line and a line with no colon, and keeps its cut-line
 * count. */
TB_TEST(sec_ics_long_and_malformed_lines_stay_in_bounds)
{
    cal_feed_t *f = cal_feed_new(0, 4000000000LL, NULL, "me@example.com");
    TB_TRUE(f != NULL);
    const char *head = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:";
    cal_feed_write(f, head, strlen(head));
    static char chunk[1000];
    memset(chunk, 'A', sizeof chunk);
    for (int i = 0; i < 20; i++) {
        cal_feed_write(f, chunk, sizeof chunk);
        cal_feed_write(f, "\r\n ", 3);  /* folded */
    }
    const char *tail = "\r\nATTENDEE;PARTSTAT=\"\r\nEXDATE;TZID=\":,,,,\r\nNOCOLON\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    cal_feed_write(f, tail, strlen(tail));
    static tb_meeting_t out[8];
    int n = 0;
    TB_EQ_INT(cal_feed_finish(f, out, 8, &n), CAL_OK);
    TB_TRUE(cal_feed_stats(f)->cut_lines >= 1);
    cal_feed_free(f);
}

/* ======================================================================================================== */
/* 7. CPU time between yields                                                                               */
/* ======================================================================================================== */

static bool occ_fn(void *ctx, const cal_civil_t *c)
{
    (void)c;
    (*(int *)ctx)++;
    return true;
}

TB_TEST(sec_rrule_budget_is_charged_per_day_examined)
{
    cal_tz_t utc;
    cal_tz_utc(&utc);
    cal_civil_t dt = {.year = 1, .month = 1, .day = 1, .hour = 9};
    tb_epoch_t ws = fx_utc(2026, 10, 4, 0, 0), we = ws + 2 * 86400;
    cal_rrule_t r;
    int n = 0;
    /* weekly with every day: 7 a week, so a budget of 7000 is 1000 weeks */
    TB_TRUE(cal_rrule_parse("FREQ=WEEKLY;COUNT=1000000;BYDAY=SU,MO,TU,WE,TH,FR,SA", &r));
    uint32_t steps = 7000;
    TB_EQ_INT(cal_rrule_expand(&r, &dt, &utc, 1800, ws, we, &steps, occ_fn, &n), -1);
    TB_EQ_INT(steps, 0);
    /* yearly with a weekday: a whole year a period, so 100,000 days run out after under 300 years (counted by
     * periods, the walk went on to the year 9999 and returned normally) */
    TB_TRUE(cal_rrule_parse("FREQ=YEARLY;COUNT=1000000;BYDAY=MO", &r));
    steps = 100000;
    n = 0;
    TB_EQ_INT(cal_rrule_expand(&r, &dt, &utc, 1800, ws, we, &steps, occ_fn, &n), -1);
    TB_EQ_INT(steps, 0);
    /* a real series with COUNT still walks to today well within an event's budget */
    TB_TRUE(cal_rrule_parse("FREQ=WEEKLY;COUNT=5200;BYDAY=MO,TU,WE,TH,FR", &r));
    cal_civil_t d2018 = {.year = 2018, .month = 1, .day = 1, .hour = 9};
    steps = CAL_RRULE_STEPS_EVENT;
    n = 0;
    TB_EQ_INT(cal_rrule_expand(&r, &d2018, &utc, 1800, fx_utc(2026, 10, 5, 0, 0), fx_utc(2026, 10, 7, 0, 0), &steps,
                               occ_fn, &n), 2);
    TB_TRUE(steps > CAL_RRULE_STEPS_EVENT - 4000);
}

typedef struct {
    int ticks;
    double last, max_gap;
} tick_probe_t;

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void probe_tick(void *ctx)
{
    tick_probe_t *p = ctx;
    double t = now_s();
    if (t - p->last > p->max_gap) p->max_gap = t - p->last;
    p->last = t;
    p->ticks++;
}

/* The review: the device yielded only every 32 KB read, and one cal_feed_write() could expand recurrences for about
 * 0.6 s on the host (much longer on the S3) without coming back: near the 10 s task watchdog. The reader now calls
 * the device's tick from inside the expansion, and the budget is charged per day, so the whole feed is short too. */
TB_TEST(sec_hostile_feed_ticks_inside_one_write_and_ends_soon)
{
    static char feed[64 * 1024];
    size_t o = (size_t)snprintf(feed, sizeof feed, "BEGIN:VCALENDAR\r\n");
    for (int i = 0; i < 40; i++)
        o += (size_t)snprintf(feed + o, sizeof feed - o,
                              "BEGIN:VEVENT\r\nUID:e%d\r\nDTSTART:00010101T090000Z\r\nDTEND:00010101T093000Z\r\n"
                              "RRULE:FREQ=%s;COUNT=1000000;BYDAY=SU,MO,TU,WE,TH,FR,SA\r\nEND:VEVENT\r\n",
                              i, i % 2 ? "WEEKLY" : "YEARLY");
    o += (size_t)snprintf(feed + o, sizeof feed - o, "END:VCALENDAR\r\n");
    tb_epoch_t ws = fx_utc(2026, 10, 4, 0, 0), we = ws + 2 * 86400;
    cal_feed_t *f = cal_feed_new(ws, we, NULL, "");
    tick_probe_t p = {.last = now_s()};
    cal_feed_set_tick(f, probe_tick, &p);
    double t0 = now_s();
    cal_feed_write(f, feed, o);         /* one write, as a buffered TLS read hands over */
    double secs = now_s() - t0;
    probe_tick(&p);
    const cal_stats_t *st = cal_feed_stats(f);
    printf("    %zu bytes in one write: %.3f s on the host (sanitizers on), %d ticks, longest gap %.1f ms, %u rules "
           "stopped by the budget\n", o, secs, p.ticks, p.max_gap * 1000, (unsigned)st->rrule_budget_hit);
    TB_TRUE(st->rrule_budget_hit >= 20);    /* the feed's budget ran out */
    TB_TRUE(p.ticks > 100);
    TB_TRUE(p.max_gap < 0.05);              /* cal_sync's 50 ms is measured on the device's clock; here, sanitizers */
    TB_TRUE(secs < 5.0);
    cal_feed_free(f);
}

TB_TEST(sec_reader_ticks_on_plain_bytes_too)
{
    cal_feed_t *f = cal_feed_new(0, 4000000000LL, NULL, NULL);
    tick_probe_t p = {.last = now_s()};
    cal_feed_set_tick(f, probe_tick, &p);
    static char big[64 * 1024];
    memcpy(big, "BEGIN:VCALENDAR\r\nX-JUNK:", 25);
    memset(big + 25, 'x', sizeof big - 25);
    cal_feed_write(f, big, sizeof big);
    TB_EQ_INT(p.ticks, (int)(sizeof big / CAL_TICK_BYTES));
    cal_feed_free(f);
}
