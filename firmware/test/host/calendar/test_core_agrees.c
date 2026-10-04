/*
 * test_core_agrees.c: the meetings this component hands over, asked "now / next / left" through core's own queries
 * (what the screen and GET /api/v1/status use, with the C library's zone) and through cal_today.h (explicit zone):
 * the answers must be the same at every minute of the window. Owner: calendar builder.
 */
#include <stdlib.h>
#include <string.h>

#include "cal_fixture.h"
#include "tb_app.h"

static void check_file(const char *file, int y, int mo, int d)
{
    static feed_result_t r;
    fx_run_file(file, y, mo, d, SELF_EMAIL, &r);
    TB_EQ_INT(r.err, CAL_OK);
    static tb_app_t a;
    memset(&a, 0, sizeof(a));
    int n = r.n < TB_MEETINGS_MAX ? r.n : TB_MEETINGS_MAX;
    memcpy(a.meetings, r.m, (size_t)n * sizeof(r.m[0]));
    a.n_meetings = (uint8_t)n;
    cal_tz_t la = fx_tz(LA_POSIX);  /* the runner sets TZ to the same rule */
    tb_epoch_t ws, we;
    fx_la_window(y, mo, d, &ws, &we);
    int bad = 0;
    for (tb_epoch_t t = ws - 3600; t < we + 3600 && bad < 5; t += 60) {
        tb_clock_t now = {.mono = 1, .wall = t, .valid = true};
        const tb_meeting_t *c1 = tb_app_current_meeting(&a, &now), *c2 = cal_today_current(a.meetings, n, t);
        const tb_meeting_t *n1 = tb_app_next_meeting(&a, &now), *n2 = cal_today_next(a.meetings, n, t, &la);
        int l1 = tb_app_meetings_left(&a, &now), l2 = cal_today_left(a.meetings, n, t, &la);
        char t1[48], t2[48];
        tb_app_left_text(&a, &now, t1, sizeof t1);
        cal_today_left_text(a.meetings, n, t, &la, t2, sizeof t2);
        if (c1 != c2 || n1 != n2 || l1 != l2 || strcmp(t1, t2)) {
            TB_FAIL_AT("%s at %lld: current %p/%p next %p/%p left %d/%d", file, (long long)t, (const void *)c1,
                       (const void *)c2, (const void *)n1, (const void *)n2, l1, l2);
            bad++;
        }
    }
}

TB_TEST(core_and_calendar_agree_on_now_next_left)
{
    check_file("google_week.ics", 2026, 10, 5);
    check_file("google_dst.ics", 2026, 11, 1);     /* a 25-hour day */
    check_file("google_dst.ics", 2026, 3, 8);      /* a 23-hour day */
    check_file("outlook.ics", 2026, 10, 29);
}
