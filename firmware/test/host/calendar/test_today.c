/* test_today.c: the window, now / next / left, trimming, the saved copy's format, and result wording.
 * Owner: calendar builder. */
#include <stdlib.h>
#include <string.h>

#include "cal_fixture.h"
#include "cal_status.h"
#include "cal_store.h"

static tb_meeting_t mk(uint32_t id, tb_epoch_t start, int minutes, const char *title)
{
    tb_meeting_t m;
    memset(&m, 0, sizeof(m));
    m.id = id;
    m.start = start;
    m.end = start + minutes * 60;
    strncpy(m.title, title, sizeof(m.title) - 1);
    return m;
}

TB_TEST(today_window)
{
    cal_tz_t la = fx_tz(LA_POSIX);
    tb_epoch_t ws, we;
    cal_today_window(&la, fx_utc(2026, 10, 5, 15, 0), &ws, &we);
    TB_EQ_INT(ws, fx_utc(2026, 10, 5, 7, 0));
    TB_EQ_INT(we, fx_utc(2026, 10, 7, 7, 0));
    /* just before local midnight it's still the same day */
    cal_today_window(&la, fx_utc(2026, 10, 6, 6, 59), &ws, &we);
    TB_EQ_INT(ws, fx_utc(2026, 10, 5, 7, 0));
    /* Kolkata, Kathmandu, Auckland */
    cal_tz_t kol = fx_tz("IST-5:30");
    cal_today_window(&kol, fx_utc(2026, 10, 5, 12, 0), &ws, &we);
    TB_EQ_INT(ws, fx_utc(2026, 10, 4, 18, 30));
    TB_EQ_INT(we - ws, 2 * 86400);
    cal_tz_t akl = fx_tz("NZST-12NZDT,M9.5.0,M4.1.0/3");
    cal_today_window(&akl, fx_utc(2026, 10, 5, 12, 0), &ws, &we);
    TB_EQ_INT(ws, fx_utc(2026, 10, 5, 11, 0));  /* 10/6 0:00 NZDT */
}

TB_TEST(today_overlaps_and_back_to_back)
{
    cal_tz_t la = fx_tz(LA_POSIX);
    tb_epoch_t nine = fx_utc(2026, 10, 5, 16, 0);
    tb_meeting_t m[4] = {
        mk(1, nine, 60, "Long"),                /* 9:00 - 10:00 */
        mk(2, nine + 1800, 15, "Inside"),       /* 9:30 - 9:45 */
        mk(3, nine + 3600, 30, "Back to back"), /* 10:00 - 10:30 */
        mk(4, nine + 3600, 0, "Zero"),          /* 10:00, no length */
    };
    /* of two that overlap, the one that started last */
    const tb_meeting_t *c = cal_today_current(m, 4, nine + 1900);
    TB_EQ_STR(c ? c->title : NULL, "Inside");
    c = cal_today_current(m, 4, nine + 2800);
    TB_EQ_STR(c ? c->title : NULL, "Long");
    /* at 10:00 exactly the long one is over and the next has begun; a zero-length meeting is never in progress */
    c = cal_today_current(m, 4, nine + 3600);
    TB_EQ_STR(c ? c->title : NULL, "Back to back");
    const tb_meeting_t *n = cal_today_next(m, 4, nine + 1900, &la);
    TB_TRUE(n && n->start == nine + 3600);
    TB_EQ_INT(cal_today_left(m, 4, nine + 1900, &la), 4);
    TB_EQ_INT(cal_today_left(m, 4, nine + 3600, &la), 1);  /* the zero-length one ended as it began */
    TB_TRUE(cal_today_current(NULL, 0, nine) == NULL);
    TB_TRUE(cal_today_next(NULL, 0, nine, &la) == NULL);
}

TB_TEST(today_next_is_today_only)
{
    cal_tz_t la = fx_tz(LA_POSIX);
    tb_meeting_t m[2] = {
        mk(1, fx_utc(2026, 10, 6, 16, 0), 30, "Tomorrow 9:00"),
        mk(2, fx_utc(2026, 10, 5, 23, 0), 30, "Today 16:00"),
    };
    const tb_meeting_t *n = cal_today_next(m, 2, fx_utc(2026, 10, 5, 22, 0), &la);
    TB_EQ_STR(n ? n->title : NULL, "Today 16:00");
    n = cal_today_next(m, 2, fx_utc(2026, 10, 5, 23, 45), &la);
    TB_TRUE(n == NULL);    /* "Next up" covers the rest of today only (decisions.md) */
    char buf[40];
    TB_EQ_STR(cal_today_left_text(m, 2, fx_utc(2026, 10, 5, 23, 45), &la, buf, sizeof buf), "no more meetings today");
    /* after midnight, tomorrow's is today's */
    n = cal_today_next(m, 2, fx_utc(2026, 10, 6, 8, 0), &la);
    TB_EQ_STR(n ? n->title : NULL, "Tomorrow 9:00");
}

TB_TEST(today_trim)
{
    tb_meeting_t m[40];
    tb_epoch_t t0 = fx_utc(2026, 10, 5, 15, 0);
    for (int i = 0; i < 40; i++) m[i] = mk((uint32_t)i + 1, t0 + i * 1800, 25, "x");
    /* now = after the 10th: drop the 8 that ended first (40 - 32), keep order */
    int n = cal_today_trim(m, 40, 32, t0 + 10 * 1800);
    TB_EQ_INT(n, 32);
    TB_EQ_INT(m[0].id, 9);
    TB_EQ_INT(m[31].id, 40);
    /* fewer ended than needed: drop all ended, then the latest */
    for (int i = 0; i < 40; i++) m[i] = mk((uint32_t)i + 1, t0 + i * 1800, 25, "x");
    n = cal_today_trim(m, 40, 32, t0 + 3 * 1800);
    TB_EQ_INT(n, 32);
    TB_EQ_INT(m[0].id, 4);
    TB_EQ_INT(m[31].id, 35);
    /* under the limit: untouched */
    TB_EQ_INT(cal_today_trim(m, 10, 32, t0), 10);
    TB_EQ_INT(cal_today_trim(m, 10, 0, t0), 0);
}

TB_TEST(today_equal)
{
    tb_meeting_t a[2] = {mk(1, 100, 10, "A"), mk(2, 200, 10, "B")};
    tb_meeting_t b[2];
    memcpy(b, a, sizeof(a));
    TB_TRUE(cal_meetings_equal(a, 2, b, 2));
    TB_FALSE(cal_meetings_equal(a, 2, b, 1));
    strcpy(b[1].location, "Room 1");
    TB_FALSE(cal_meetings_equal(a, 2, b, 2));
    memcpy(b, a, sizeof(a));
    b[0].end++;
    TB_FALSE(cal_meetings_equal(a, 2, b, 2));
    TB_TRUE(cal_meetings_equal(NULL, 0, NULL, 0));
}

TB_TEST(store_round_trip)
{
    static tb_meeting_t m[TB_MEETINGS_MAX], back[TB_MEETINGS_MAX];
    for (int i = 0; i < TB_MEETINGS_MAX; i++) {
        m[i] = mk(0x9000u + (uint32_t)i, fx_utc(2026, 10, 5, 15, 0) + i * 900, 15 + i, "");
        /* long titles of 2-byte characters: cut to 80 bytes, on a character */
        for (int k = 0; k < 60; k++) strcat(m[i].title, "\xC3\xA9");
        snprintf(m[i].location, sizeof(m[i].location), "Room %d", i);
        m[i].priv = i % 5 == 0;
    }
    static uint8_t buf[CAL_STORE_BYTES_MAX];
    size_t len = cal_store_pack(m, TB_MEETINGS_MAX, 1791158400, buf, sizeof(buf));
    TB_TRUE(len > 0 && len <= CAL_STORE_BYTES_MAX);
    tb_epoch_t last = 0;
    int n = cal_store_unpack(buf, len, back, TB_MEETINGS_MAX, &last);
    TB_EQ_INT(n, TB_MEETINGS_MAX);
    TB_EQ_INT(last, 1791158400);
    for (int i = 0; i < n; i++) {
        TB_EQ_INT(back[i].id, m[i].id);
        TB_EQ_INT(back[i].start, m[i].start);
        TB_EQ_INT(back[i].end, m[i].end);
        TB_EQ_INT(back[i].priv, m[i].priv);
        TB_EQ_INT(strlen(back[i].title), 80);
        TB_EQ_STR(back[i].location, m[i].location);
    }
    /* a typical day is small */
    tb_meeting_t few[5];
    for (int i = 0; i < 5; i++) few[i] = mk((uint32_t)i + 1, 1000 + i, 30, "Daily standup");
    len = cal_store_pack(few, 5, 0, buf, sizeof(buf));
    TB_TRUE(len < 200);
    /* empty */
    len = cal_store_pack(NULL, 0, 5, buf, sizeof(buf));
    TB_EQ_INT(len, CAL_STORE_HEADER);
    TB_EQ_INT(cal_store_unpack(buf, len, back, 4, &last), 0);
    TB_EQ_INT(last, 5);
    /* too small a buffer */
    TB_EQ_INT(cal_store_pack(few, 5, 0, buf, 30), 0);
}

TB_TEST(store_refuses_damage)
{
    tb_meeting_t few[3];
    for (int i = 0; i < 3; i++) few[i] = mk((uint32_t)i + 1, 1000 + i, 30, "Daily standup");
    uint8_t buf[512], bad[512];
    size_t len = cal_store_pack(few, 3, 7, buf, sizeof(buf));
    tb_meeting_t out[3];
    tb_epoch_t last;
    TB_EQ_INT(cal_store_unpack(buf, len, out, 3, &last), 3);
    TB_EQ_INT(cal_store_unpack(buf, len - 1, out, 3, &last), -1);      /* short */
    memcpy(bad, buf, len);
    bad[len] = 0;
    TB_EQ_INT(cal_store_unpack(bad, len + 1, out, 3, &last), -1);      /* trailing byte */
    memcpy(bad, buf, len);
    bad[2] = 9;
    TB_EQ_INT(cal_store_unpack(bad, len, out, 3, &last), -1);          /* other version */
    memcpy(bad, buf, len);
    bad[3] = 200;
    TB_EQ_INT(cal_store_unpack(bad, len, out, 3, &last), -1);          /* count over the limit */
    memcpy(bad, buf, len);
    bad[CAL_STORE_HEADER + 17] = 250;
    TB_EQ_INT(cal_store_unpack(bad, len, out, 3, &last), -1);          /* title length over the limit */
    TB_EQ_INT(cal_store_unpack(NULL, 0, out, 3, &last), -1);
    /* more entries than room: the first max */
    TB_EQ_INT(cal_store_unpack(buf, len, out, 2, &last), 2);
    /* every single-byte corruption is either refused or reads as valid strings (sanitizers watch) */
    for (size_t i = 0; i < len; i++) {
        for (int v = 0; v < 256; v += 51) {
            memcpy(bad, buf, len);
            bad[i] = (uint8_t)v;
            int n = cal_store_unpack(bad, len, out, 3, &last);
            for (int k = 0; k < n; k++) TB_TRUE(strlen(out[k].title) < sizeof(out[k].title) && out[k].id != 0);
        }
    }
}

TB_TEST(status_codes_and_wording)
{
    TB_EQ_INT(cal_sync_err_from_http(200), CAL_SYNC_OK);
    TB_EQ_INT(cal_sync_err_from_http(206), CAL_SYNC_OK);
    TB_EQ_INT(cal_sync_err_from_http(401), CAL_SYNC_REJECTED);
    TB_EQ_INT(cal_sync_err_from_http(403), CAL_SYNC_REJECTED);
    TB_EQ_INT(cal_sync_err_from_http(404), CAL_SYNC_REJECTED);
    TB_EQ_INT(cal_sync_err_from_http(410), CAL_SYNC_REJECTED);
    TB_EQ_INT(cal_sync_err_from_http(429), CAL_SYNC_UNREACHABLE);
    TB_EQ_INT(cal_sync_err_from_http(500), CAL_SYNC_UNREACHABLE);
    TB_EQ_INT(cal_sync_err_from_http(503), CAL_SYNC_UNREACHABLE);
    TB_EQ_INT(cal_sync_err_from_http(-1), CAL_SYNC_UNREACHABLE);
    TB_EQ_STR(cal_sync_err_code(CAL_SYNC_REJECTED), "calendar_rejected");
    TB_EQ_STR(cal_sync_err_code(CAL_SYNC_UNREACHABLE), "calendar_unreachable");
    TB_EQ_STR(cal_sync_err_code(CAL_SYNC_NOT_A_CALENDAR), "not_a_calendar");
    TB_EQ_STR(cal_sync_err_code(CAL_SYNC_OFFLINE), "offline");
    TB_EQ_STR(cal_sync_err_code(CAL_SYNC_TOO_LARGE), "calendar_unreachable");
    TB_TRUE(cal_sync_err_code(CAL_SYNC_OK) == NULL);
    /* api.md 11.2's sentence for Google; no "Google" for other servers */
    TB_EQ_STR(cal_sync_err_message(CAL_SYNC_REJECTED, true, true),
              "Google didn't recognize that address. It may have been reset in Google Calendar.");
    TB_TRUE(strstr(cal_sync_err_message(CAL_SYNC_REJECTED, false, true), "Google") == NULL);
    TB_EQ_STR(cal_sync_err_message(CAL_SYNC_OFFLINE, true, true), "MiniBar isn't online, so it can't check the address.");
    TB_EQ_STR(cal_sync_err_message(CAL_SYNC_OFFLINE, true, false), "MiniBar isn't online, so it can't sync.");
    /* every sentence fits the status's message buffers */
    for (int e = CAL_SYNC_REJECTED; e <= CAL_SYNC_TOO_LARGE; e++) {
        for (int g = 0; g < 2; g++) {
            TB_TRUE(strlen(cal_sync_err_message((cal_sync_err_t)e, g, true)) < sizeof(((cal_status_t *)0)->check_message));
            TB_TRUE(strlen(cal_sync_err_message((cal_sync_err_t)e, g, false)) < sizeof(((cal_status_t *)0)->error_message));
        }
    }
}
