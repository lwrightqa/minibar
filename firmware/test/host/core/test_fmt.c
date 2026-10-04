/* Seed tests for the formatters (lead). Owner from here: core builder. The runner sets TZ to US Pacific. */
#include "tb_fmt.h"
#include "tb_test.h"

static const tb_epoch_t T_1404 = 1791148920 - 18 * 60;   /* 2026-10-04 14:04 PDT */

TB_TEST(fmt_time)
{
    char b[32];
    TB_EQ_STR(tb_fmt_time(b, sizeof b, T_1404), "2:04 PM");
    TB_EQ_STR(tb_fmt_time_short(b, sizeof b, T_1404), "2:04");
    TB_EQ_STR(tb_fmt_ampm(T_1404), "PM");
    TB_EQ_STR(tb_fmt_date_long(b, sizeof b, T_1404), "Sunday, October 4");
    TB_EQ_STR(tb_fmt_rfc3339(b, sizeof b, T_1404), "2026-10-04T14:04:00-07:00");
}

TB_TEST(fmt_durations)
{
    char b[32];
    TB_EQ_STR(tb_fmt_mmss(b, sizeof b, 18 * 60 + 42), "18:42");
    TB_EQ_STR(tb_fmt_mmss(b, sizeof b, 300), "5:00");
    TB_EQ_STR(tb_fmt_hm(b, sizeof b, 45), "45m");
    TB_EQ_STR(tb_fmt_hm(b, sizeof b, 65), "1h 5m");
    TB_EQ_STR(tb_fmt_hm(b, sizeof b, 620), "10h 20m");
    TB_EQ_INT(tb_mins_up(1), 1);
    TB_EQ_INT(tb_mins_up(60000), 1);
    TB_EQ_INT(tb_mins_up(60001), 2);
}

TB_TEST(fmt_span_and_ago)
{
    char b[48];
    tb_epoch_t a = T_1404 + 26 * 60, e = a + 45 * 60;   /* 2:30 to 3:15 PM */
    TB_EQ_STR(tb_fmt_span(b, sizeof b, a, e), "2:30\xE2\x80\x93" "3:15 PM");
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, T_1404, T_1404 + 30, false), "just now");
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, T_1404, T_1404 + 120, true), "2m ago");
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, T_1404, T_1404 + 120, false), "2 min ago");
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, T_1404, T_1404 + 3600, false), "at 2:04 PM");
}
