/*
 * test_stream.c: the reader as a stream: any chunking, CRLF or LF, folding, huge lines, garbage, cut-off feeds, big
 * feeds full of history. Owner: calendar builder.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cal_fixture.h"

static bool same(const feed_result_t *a, const feed_result_t *b)
{
    if (a->n != b->n || a->err != b->err) return false;
    for (int i = 0; i < a->n; i++) {
        if (a->m[i].id != b->m[i].id || a->m[i].start != b->m[i].start || a->m[i].end != b->m[i].end ||
            strcmp(a->m[i].title, b->m[i].title) || strcmp(a->m[i].location, b->m[i].location) ||
            a->m[i].priv != b->m[i].priv)
            return false;
    }
    return true;
}

TB_TEST(stream_any_chunking_gives_the_same_result)
{
    static const char *const files[] = {"google_week.ics", "google_dst.ics", "outlook.ics"};
    static const size_t chunks[] = {1, 2, 3, 7, 74, 75, 76, 1000, 4096};
    for (size_t f = 0; f < 3; f++) {
        size_t len;
        char *data = fx_load(files[f], &len);
        if (!data) continue;
        tb_epoch_t ws, we;
        fx_la_window(2026, 10, 5, &ws, &we);
        static feed_result_t whole, part;
        fx_run(data, len, 0, ws, we, LA_POSIX, SELF_EMAIL, &whole);
        TB_EQ_INT(whole.err, CAL_OK);
        for (size_t c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
            fx_run(data, len, chunks[c], ws, we, LA_POSIX, SELF_EMAIL, &part);
            if (!same(&whole, &part)) TB_FAIL_AT("%s differs when fed %zu bytes at a time", files[f], chunks[c]);
        }
        free(data);
    }
}

TB_TEST(stream_crlf_and_lf_are_the_same)
{
    size_t len, lf_len;
    char *data = fx_load("google_week.ics", &len);
    if (!data) return;
    char *lf = fx_to_lf(data, len, &lf_len);
    TB_TRUE(lf_len < len);
    tb_epoch_t ws, we;
    fx_la_window(2026, 10, 5, &ws, &we);
    static feed_result_t a, b;
    fx_run(data, len, 0, ws, we, LA_POSIX, SELF_EMAIL, &a);
    fx_run(lf, lf_len, 13, ws, we, LA_POSIX, SELF_EMAIL, &b);
    TB_TRUE(same(&a, &b));
    TB_EQ_INT(b.n, 9);
    free(data);
    free(lf);
}

static void run_text(const char *text, size_t chunk, feed_result_t *r)
{
    tb_epoch_t ws, we;
    fx_la_window(2026, 10, 5, &ws, &we);
    fx_run(text, strlen(text), chunk, ws, we, LA_POSIX, SELF_EMAIL, r);
}

#define CAL_HEAD "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
#define CAL_TAIL "END:VCALENDAR\r\n"

TB_TEST(stream_text_escapes_folding_and_cleaning)
{
    static feed_result_t r;
    run_text(CAL_HEAD
             "BEGIN:VEVENT\r\n"
             "UID:esc-1\r\n"
             "DTSTART:20261005T170000Z\r\n"
             "DTEND:20261005T173000Z\r\n"
             "SUMMARY:Plan\\, build\\; ship \\\\ review\\nnext steps\r\n"
             "LOCATION:Room \xE2\x80\x9C" "A\xE2\x80\x9D \xE2\x80\x93 2nd floor\r\n"
             "END:VEVENT\r\n" CAL_TAIL,
             0, &r);
    TB_EQ_INT(r.n, 1);
    TB_EQ_STR(r.m[0].title, "Plan, build; ship \\ review next steps");
    TB_EQ_STR(r.m[0].location, "Room \"A\" - 2nd floor");     /* tb_text_clean's mapping */
    /* a fold with a tab, a fold right after the property name, and parameters with quotes and colons */
    run_text(CAL_HEAD
             "BEGIN:VEVENT\r\n"
             "UID:fold-1\r\n"
             "DTSTART;TZID=\"America/Los_Angeles\";X-NOTE=\"a:b;c\":20261005T1\r\n"
             "\t00000\r\n"
             "DTEND;TZID=America/Los_Angeles:20261005T103000\r\n"
             "SUMMARY\r\n"
             " :Folded\r\n"
             "  title\r\n"
             "END:VEVENT\r\n" CAL_TAIL,
             1, &r);
    TB_EQ_INT(r.n, 1);
    TB_EQ_INT(r.m[0].start, fx_utc(2026, 10, 5, 17, 0));
    TB_EQ_STR(r.m[0].title, "Folded title");
    /* a byte-order mark, blank lines, lowercase names, trailing spaces */
    run_text("\xEF\xBB\xBF" "BEGIN:VCALENDAR\r\n\r\n"
             "begin:vevent\r\n"
             "uid:lower-1\r\n"
             "dtstart:20261005T170000Z \r\n"
             "duration:PT45M\r\n"
             "summary:Lower case\r\n"
             "end:vevent\r\n"
             "END:VCALENDAR\r\n",
             0, &r);
    TB_EQ_INT(r.err, CAL_OK);
    TB_EQ_INT(r.n, 1);
    TB_EQ_INT(r.m[0].end - r.m[0].start, 45 * 60);
    TB_EQ_STR(r.m[0].title, "Lower case");
}

TB_TEST(stream_titles_and_locations_are_cut_on_characters)
{
    /* a 300-byte title of 2-byte characters: kept to TB_TITLE_BYTES - 1 at most, never half a character */
    char text[4096];
    char title[700] = "";
    for (int i = 0; i < 150; i++) strcat(title, "\xC3\xA9");
    snprintf(text, sizeof(text), CAL_HEAD "BEGIN:VEVENT\r\nUID:cut\r\nDTSTART:20261005T170000Z\r\nDTEND:20261005T180000Z\r\n"
             "SUMMARY:%s\r\nLOCATION:%s\r\nEND:VEVENT\r\n" CAL_TAIL, title, title);
    static feed_result_t r;
    run_text(text, 0, &r);
    TB_EQ_INT(r.n, 1);
    size_t n = strlen(r.m[0].title);
    TB_TRUE(n <= TB_TITLE_BYTES - 1 && n % 2 == 0 && n >= TB_TITLE_BYTES - 3);
    n = strlen(r.m[0].location);
    TB_TRUE(n <= TB_LOCATION_BYTES - 1 && n % 2 == 0);
}

TB_TEST(stream_huge_description_and_attachment)
{
    /* A 600 KB DESCRIPTION folded every 75 bytes and a 300 KB ATTACH line stream past without being kept. */
    size_t cap = 2 * 1024 * 1024, o = 0;
    char *buf = malloc(cap);
    o += (size_t)snprintf(buf + o, cap - o, CAL_HEAD "BEGIN:VEVENT\r\nUID:big-1\r\nDTSTART:20261005T170000Z\r\n"
                          "DTEND:20261005T180000Z\r\nDESCRIPTION:");
    for (int i = 0; i < 8000; i++) o += (size_t)snprintf(buf + o, cap - o, "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod\r\n ");
    o += (size_t)snprintf(buf + o, cap - o, "end\r\nATTACH;ENCODING=BASE64;VALUE=BINARY:");
    for (int i = 0; i < 300000; i++) buf[o++] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"[i % 32];
    o += (size_t)snprintf(buf + o, cap - o, "\r\nSUMMARY:After the big stuff\r\nEND:VEVENT\r\n" CAL_TAIL);
    tb_epoch_t ws, we;
    fx_la_window(2026, 10, 5, &ws, &we);
    static feed_result_t r;
    fx_run(buf, o, 1460, ws, we, LA_POSIX, SELF_EMAIL, &r);
    TB_EQ_INT(r.err, CAL_OK);
    TB_EQ_INT(r.n, 1);
    TB_EQ_STR(r.m[0].title, "After the big stuff");
    TB_EQ_INT(r.stats.cut_lines, 0);    /* skipped, not cut: they were never kept */
    /* a kept line that's too long is cut and counted, and the event still reads */
    o = 0;
    o += (size_t)snprintf(buf + o, cap - o, CAL_HEAD "BEGIN:VEVENT\r\nUID:big-2\r\nDTSTART:20261005T170000Z\r\n"
                          "DTEND:20261005T180000Z\r\nSUMMARY:");
    for (int i = 0; i < 10000; i++) buf[o++] = 'x';
    o += (size_t)snprintf(buf + o, cap - o, "\r\nEND:VEVENT\r\n" CAL_TAIL);
    fx_run(buf, o, 0, ws, we, LA_POSIX, SELF_EMAIL, &r);
    TB_EQ_INT(r.n, 1);
    TB_EQ_INT(r.stats.cut_lines, 1);
    TB_EQ_INT(strlen(r.m[0].title), TB_TITLE_BYTES - 1);
    free(buf);
}

TB_TEST(stream_years_of_history_is_fast_and_small)
{
    /* What a heavy Google calendar looks like: 20,000 past events, 300 weekly series from years ago (some with
     * hundreds of EXDATEs and overrides), then this week's. About 9 MB. */
    size_t cap = 24 * 1024 * 1024, o = 0;
    char *buf = malloc(cap);
    o += (size_t)snprintf(buf + o, cap - o, CAL_HEAD "X-WR-TIMEZONE:America/Los_Angeles\r\n");
    for (int i = 0; i < 20000; i++) {
        int y = 2012 + i / 2000, d = 1 + i % 28, m = 1 + (i / 28) % 12;
        o += (size_t)snprintf(buf + o, cap - o,
                              "BEGIN:VEVENT\r\nDTSTART;TZID=America/Los_Angeles:%04d%02d%02dT%02d0000\r\n"
                              "DTEND;TZID=America/Los_Angeles:%04d%02d%02dT%02d3000\r\nUID:past-%d@google.com\r\n"
                              "ATTENDEE;CUTYPE=INDIVIDUAL;ROLE=REQ-PARTICIPANT;PARTSTAT=ACCEPTED;CN=%s:mailto:%s\r\n"
                              "DESCRIPTION:Notes from a meeting long ago\\, with details nobody will read again. "
                              "Notes from a meeting long ago\\, with details\r\n nobody will read again.\r\n"
                              "SUMMARY:Old meeting %d\r\nEND:VEVENT\r\n",
                              y, m, d, 9 + i % 8, y, m, d, 9 + i % 8, i, SELF_EMAIL, SELF_EMAIL, i);
    }
    for (int s = 0; s < 300; s++) {
        o += (size_t)snprintf(buf + o, cap - o,
                              "BEGIN:VEVENT\r\nDTSTART;TZID=America/Los_Angeles:2015%02d%02dT%02d0000\r\n"
                              "DTEND;TZID=America/Los_Angeles:2015%02d%02dT%02d3000\r\nRRULE:FREQ=WEEKLY%s\r\n",
                              1 + s % 12, 1 + s % 28, 8 + s % 9, 1 + s % 12, 1 + s % 28, 8 + s % 9,
                              s % 3 ? ";UNTIL=20200101T000000Z" : "");
        for (int x = 0; x < (s == 0 ? 400 : 3); x++)
            o += (size_t)snprintf(buf + o, cap - o, "EXDATE;TZID=America/Los_Angeles:2016%02d%02dT%02d0000\r\n",
                                  1 + x % 12, 1 + x % 28, 8 + s % 9);
        o += (size_t)snprintf(buf + o, cap - o, "UID:series-%d@google.com\r\nSUMMARY:Series %d\r\nEND:VEVENT\r\n", s, s);
        for (int v = 0; v < 2; v++)
            o += (size_t)snprintf(buf + o, cap - o,
                                  "BEGIN:VEVENT\r\nDTSTART;TZID=America/Los_Angeles:2016%02d%02dT120000\r\n"
                                  "DTEND;TZID=America/Los_Angeles:2016%02d%02dT123000\r\nUID:series-%d@google.com\r\n"
                                  "RECURRENCE-ID;TZID=America/Los_Angeles:2016%02d%02dT%02d0000\r\nSUMMARY:Moved\r\n"
                                  "END:VEVENT\r\n",
                                  2 + v, 3, 2 + v, 3, s, 2 + v, 3, 8 + s % 9);
    }
    o += (size_t)snprintf(buf + o, cap - o,
                          "BEGIN:VEVENT\r\nDTSTART;TZID=America/Los_Angeles:20261005T100000\r\n"
                          "DTEND;TZID=America/Los_Angeles:20261005T110000\r\nUID:today@google.com\r\n"
                          "SUMMARY:Today's meeting\r\nEND:VEVENT\r\n" CAL_TAIL);
    TB_TRUE(o < cap);
    tb_epoch_t ws, we;
    fx_la_window(2026, 10, 5, &ws, &we);
    static feed_result_t r;
    clock_t t0 = clock();
    fx_run(buf, o, 2048, ws, we, LA_POSIX, SELF_EMAIL, &r);
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("    %zu bytes, %u events, %d meetings in %.2f s on the host (with sanitizers)\n", o,
           (unsigned)r.stats.events, r.n, secs);
    TB_EQ_INT(r.err, CAL_OK);
    TB_EQ_INT(r.stats.events, 20000 + 300 * 3 + 1);
    TB_TRUE(fx_find(&r, "Today's meeting") != NULL);
    /* the 100 open-ended series land on Mondays..Sundays of this window: each weekday's share */
    TB_TRUE(r.n > 10 && r.n <= CAL_CANDIDATES_MAX);
    TB_EQ_INT(r.stats.rrule_budget_hit, 0);
    TB_EQ_INT(r.stats.dropped_exdates, 0);     /* old EXDATEs aren't kept at all */
    TB_EQ_INT(r.stats.dropped_overrides, 0);   /* nor old overrides */
    TB_TRUE(secs < 30.0);
    free(buf);
}

TB_TEST(stream_too_many_instances_keeps_the_earliest)
{
    /* 100 meetings in the window: the 64 earliest are kept, the latest are dropped and counted */
    size_t cap = 64 * 1024, o = 0;
    char *buf = malloc(cap);
    o += (size_t)snprintf(buf + o, cap - o, CAL_HEAD);
    for (int i = 99; i >= 0; i--)   /* written latest first */
        o += (size_t)snprintf(buf + o, cap - o, "BEGIN:VEVENT\r\nUID:many-%d\r\nDTSTART:20261005T%02d%02d00Z\r\n"
                              "DURATION:PT10M\r\nSUMMARY:M%d\r\nEND:VEVENT\r\n", i, 7 + i / 6, (i % 6) * 10, i);
    o += (size_t)snprintf(buf + o, cap - o, CAL_TAIL);
    static feed_result_t r;
    tb_epoch_t ws, we;
    fx_la_window(2026, 10, 5, &ws, &we);
    fx_run(buf, o, 0, ws, we, LA_POSIX, SELF_EMAIL, &r);
    TB_EQ_INT(r.n, CAL_CANDIDATES_MAX);
    TB_EQ_STR(r.m[0].title, "M0");
    TB_EQ_STR(r.m[63].title, "M63");
    TB_EQ_INT(r.stats.dropped_candidates, 36);
    free(buf);
}

TB_TEST(stream_garbage_is_not_a_calendar)
{
    static feed_result_t r;
    size_t len;
    /* a sign-in page: refused as soon as 4 KB went by without BEGIN:VCALENDAR */
    char *html = fx_load("google_signin.html", &len);
    if (html) {
        tb_epoch_t ws, we;
        fx_la_window(2026, 10, 5, &ws, &we);
        fx_run(html, len, 512, ws, we, LA_POSIX, SELF_EMAIL, &r);
        TB_EQ_INT(r.write_err, CAL_ERR_NOT_A_CALENDAR);
        TB_EQ_INT(r.err, CAL_ERR_NOT_A_CALENDAR);
        TB_TRUE(r.stats.bytes <= 4096 + 512);
        free(html);
    }
    /* empty, short text, JSON */
    run_text("", 0, &r);
    TB_EQ_INT(r.err, CAL_ERR_NOT_A_CALENDAR);
    run_text("Not Found\n", 0, &r);
    TB_EQ_INT(r.err, CAL_ERR_NOT_A_CALENDAR);
    run_text("{\"error\":\"invalid\"}", 1, &r);
    TB_EQ_INT(r.err, CAL_ERR_NOT_A_CALENDAR);
    TB_EQ_INT(r.n, 0);
    /* random bytes, many seeds and chunk sizes: never a crash (the sanitizers watch), never a meeting */
    unsigned seed = 12345;
    char *rnd = malloc(65536);
    for (int t = 0; t < 20; t++) {
        for (int i = 0; i < 65536; i++) {
            seed = seed * 1103515245u + 12345u;
            rnd[i] = (char)(seed >> 16);
        }
        tb_epoch_t ws, we;
        fx_la_window(2026, 10, 5, &ws, &we);
        fx_run(rnd, 65536, 1 + (size_t)t * 97, ws, we, LA_POSIX, SELF_EMAIL, &r);
        TB_EQ_INT(r.err, CAL_ERR_NOT_A_CALENDAR);
        /* the same bytes after a real header: read as a (useless) calendar, still no crash */
        char *mix = malloc(65536 + 64);
        int h = snprintf(mix, 64, "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n");
        memcpy(mix + h, rnd, 65536);
        fx_run(mix, 65536 + (size_t)h, 1 + (size_t)t * 31, ws, we, LA_POSIX, SELF_EMAIL, &r);
        TB_EQ_INT(r.err, CAL_OK);
        free(mix);
    }
    free(rnd);
}

TB_TEST(stream_hostile_lines)
{
    static feed_result_t r;
    /* broken values are ignored one by one; the rest of the feed still reads */
    run_text(CAL_HEAD
             "BEGIN:VEVENT\r\nUID:bad-1\r\nDTSTART:2026100517000Z\r\nSUMMARY:Bad start\r\nEND:VEVENT\r\n"
             "BEGIN:VEVENT\r\nUID:bad-2\r\nDTSTART:20261305T170000Z\r\nSUMMARY:Month 13\r\nEND:VEVENT\r\n"
             "BEGIN:VEVENT\r\nUID:bad-3\r\nDTSTART:20261005T170000Z\r\nDURATION:P1Q\r\nRRULE:FREQ=SOMETIMES\r\n"
             "EXDATE:garbage,20261005T1\r\nSUMMARY:Odd but fine\r\nEND:VEVENT\r\n"
             "BEGIN:VEVENT\r\nUID:bad-4\r\nDTSTART;TZID=\"unterminated:20261005T170000Z\r\nSUMMARY:Bad param\r\n"
             "END:VEVENT\r\n"
             "BEGIN:VEVENT\r\nNO COLON HERE AT ALL\r\n:no name\r\n;also no name\r\nDTSTART:20261005T180000Z\r\n"
             "SUMMARY:No UID\r\nEND:VEVENT\r\n"
             "BEGIN:VEVENT\r\nUID:nested\r\nDTSTART:20261005T190000Z\r\nBEGIN:VALARM\r\nBEGIN:X-INNER\r\n"
             "DTSTART:20261005T010000Z\r\nEND:X-INNER\r\nEND:VALARM\r\nSUMMARY:Nested\r\nEND:VEVENT\r\n"
             "END:VEVENT\r\nEND:VTIMEZONE\r\nEND:STANDARD\r\n"
             "BEGIN:VTODO\r\nDTSTART:20261005T200000Z\r\nSUMMARY:A to-do\r\nBEGIN:VEVENT\r\nEND:VEVENT\r\nEND:VTODO\r\n"
             CAL_TAIL,
             3, &r);
    TB_EQ_INT(r.err, CAL_OK);
    TB_EQ_INT(r.n, 3);
    TB_TRUE(fx_find(&r, "Odd but fine") != NULL);
    TB_TRUE(fx_find(&r, "No UID") != NULL);
    const tb_meeting_t *n = fx_find(&r, "Nested");
    TB_TRUE(n && n->start == fx_utc(2026, 10, 5, 19, 0));
    TB_TRUE(!fx_find(&r, "A to-do"));
    TB_EQ_INT(r.stats.unsupported_rrule, 1);
    /* a VEVENT that never ends, then another */
    run_text(CAL_HEAD "BEGIN:VEVENT\r\nUID:a\r\nDTSTART:20261005T170000Z\r\nSUMMARY:Unended\r\n"
             "BEGIN:VEVENT\r\nUID:b\r\nDTSTART:20261005T180000Z\r\nSUMMARY:Next\r\nEND:VEVENT\r\n" CAL_TAIL, 0, &r);
    TB_EQ_INT(r.n, 2);
    /* thousands of nested BEGINs don't overflow anything */
    size_t cap = 400000, o = 0;
    char *buf = malloc(cap);
    o += (size_t)snprintf(buf, cap, CAL_HEAD "BEGIN:VEVENT\r\nUID:deep\r\nDTSTART:20261005T170000Z\r\n");
    for (int i = 0; i < 10000; i++) o += (size_t)snprintf(buf + o, cap - o, "BEGIN:X\r\n");
    for (int i = 0; i < 10000; i++) o += (size_t)snprintf(buf + o, cap - o, "END:X\r\n");
    o += (size_t)snprintf(buf + o, cap - o, "SUMMARY:Deep\r\nEND:VEVENT\r\n" CAL_TAIL);
    tb_epoch_t ws, we;
    fx_la_window(2026, 10, 5, &ws, &we);
    fx_run(buf, o, 0, ws, we, LA_POSIX, SELF_EMAIL, &r);
    TB_EQ_INT(r.n, 1);
    TB_TRUE(fx_find(&r, "Deep") != NULL);
    free(buf);
}

TB_TEST(stream_cut_off_feeds_never_crash)
{
    /* Every prefix of a real feed (as a dropped connection would leave it) reads without a crash, and a prefix
     * never invents a meeting the whole feed doesn't have... except masters whose override hadn't arrived yet. */
    size_t len;
    char *data = fx_load("google_week.ics", &len);
    if (!data) return;
    tb_epoch_t ws, we;
    fx_la_window(2026, 10, 5, &ws, &we);
    static feed_result_t whole, part;
    fx_run(data, len, 0, ws, we, LA_POSIX, SELF_EMAIL, &whole);
    int checked = 0;
    for (size_t cut = 0; cut < len; cut += 53) {
        fx_run(data, cut, 0, ws, we, LA_POSIX, SELF_EMAIL, &part);
        if (cut < 15) {
            TB_EQ_INT(part.err, CAL_ERR_NOT_A_CALENDAR);
            continue;
        }
        TB_EQ_INT(part.err, CAL_OK);
        TB_FALSE(part.stats.ended);
        TB_TRUE(part.n <= whole.n + 2);
        checked++;
    }
    TB_TRUE(checked > 300);
    free(data);
}

TB_TEST(stream_api_misuse)
{
    TB_EQ_INT(cal_feed_write(NULL, "x", 1), CAL_ERR_NO_MEMORY);
    int n = 5;
    TB_EQ_INT(cal_feed_finish(NULL, NULL, 0, &n), CAL_ERR_NO_MEMORY);
    TB_EQ_INT(n, 0);
    cal_feed_free(NULL);
    /* finish twice, write after finish */
    cal_feed_t *f = cal_feed_new(fx_utc(2026, 10, 5, 7, 0), fx_utc(2026, 10, 7, 7, 0), NULL, NULL);
    const char *t = CAL_HEAD "BEGIN:VEVENT\r\nUID:x\r\nDTSTART:20261005T170000Z\r\nEND:VEVENT\r\n" CAL_TAIL;
    TB_EQ_INT(cal_feed_write(f, t, strlen(t)), CAL_OK);
    tb_meeting_t m[4];
    TB_EQ_INT(cal_feed_finish(f, m, 4, &n), CAL_OK);
    TB_EQ_INT(n, 1);
    TB_EQ_INT(m[0].end, m[0].start);    /* no DTEND or DURATION: zero length */
    TB_EQ_INT(cal_feed_write(f, t, strlen(t)), CAL_OK);
    TB_EQ_INT(cal_feed_finish(f, m, 4, &n), CAL_OK);
    TB_EQ_INT(n, 1);
    TB_EQ_INT(cal_feed_finish(f, m, 0, &n), CAL_OK);   /* max 0 */
    TB_EQ_INT(n, 0);
    cal_feed_free(f);
}
