/* Seed tests for the formatters (lead). Owner from here: core builder. The runner sets TZ to US Pacific. */
#include "tb_fmt.h"
#include "tb_test.h"

static const tb_epoch_t T_1404 = 1791148920 - 18 * 60;   /* 2026-10-04 14:04 PDT */

TB_TEST(fmt_time)
{
    char b[32];
    TB_EQ_STR(tb_fmt_time(b, sizeof b, T_1404, false), "2:04 PM");
    TB_EQ_STR(tb_fmt_time_short(b, sizeof b, T_1404, false), "2:04");
    TB_EQ_STR(tb_fmt_ampm(T_1404, false), "PM");
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
    TB_EQ_STR(tb_fmt_span(b, sizeof b, a, e, false), "2:30\xE2\x80\x93" "3:15 PM");
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, T_1404, T_1404 + 30, false, false), "just now");
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, T_1404, T_1404 + 120, true, false), "2m ago");
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, T_1404, T_1404 + 120, false, false), "2 min ago");
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, T_1404, T_1404 + 3600, false, false), "at 2:04 PM");
}

/* Time format 24-hour (decisions.md "Time format (2026-10-07)"): no AM or PM, hours zero-padded, midnight is 00:00. */
static tb_epoch_t at(int h, int m)
{
    return tb_local_midnight(T_1404) + h * 3600 + m * 60;
}

TB_TEST(fmt_time_24h_every_formatter)
{
    char b[48];
    TB_EQ_STR(tb_fmt_time(b, sizeof b, T_1404, true), "14:04");
    TB_EQ_STR(tb_fmt_time_short(b, sizeof b, T_1404, true), "14:04");     /* the whole time: there's no chip to split off */
    TB_EQ_STR(tb_fmt_ampm(T_1404, true), "");
    /* the same instant in 12-hour is unchanged */
    TB_EQ_STR(tb_fmt_time(b, sizeof b, T_1404, false), "2:04 PM");
    TB_EQ_STR(tb_fmt_ampm(T_1404, false), "PM");
    /* a span writes both ends whole, even when both are PM (12-hour would share the PM) */
    TB_EQ_STR(tb_fmt_span(b, sizeof b, at(14, 30), at(15, 15), true), "14:30\xE2\x80\x93" "15:15");
    TB_EQ_STR(tb_fmt_span(b, sizeof b, at(9, 5), at(9, 35), true), "09:05\xE2\x80\x93" "09:35");
    TB_EQ_STR(tb_fmt_span(b, sizeof b, at(11, 30), at(12, 15), true), "11:30\xE2\x80\x93" "12:15");
    TB_EQ_STR(tb_fmt_span(b, sizeof b, at(11, 30), at(12, 15), false), "11:30 AM\xE2\x80\x93" "12:15 PM");
    /* ago: from an hour on it names the time in the setting's format */
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, T_1404, T_1404 + 3600, false, true), "at 14:04");
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, at(9, 5), at(9, 5) + 7200, true, true), "at 09:05");
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, T_1404, T_1404 + 120, true, true), "2m ago");
    TB_EQ_STR(tb_fmt_ago(b, sizeof b, T_1404, T_1404 + 30, false, true), "just now");
}

TB_TEST(fmt_time_24h_edges)
{
    char b[32];
    TB_EQ_STR(tb_fmt_time(b, sizeof b, at(0, 0), true), "00:00");          /* midnight is 00:00, never 24:00 */
    TB_EQ_STR(tb_fmt_time(b, sizeof b, at(0, 15), true), "00:15");
    TB_EQ_STR(tb_fmt_time(b, sizeof b, at(9, 5), true), "09:05");
    TB_EQ_STR(tb_fmt_time(b, sizeof b, at(12, 0), true), "12:00");
    TB_EQ_STR(tb_fmt_time(b, sizeof b, at(23, 59), true), "23:59");
    TB_EQ_STR(tb_fmt_time_short(b, sizeof b, at(0, 15), true), "00:15");
    /* 12-hour for the same instants: midnight and noon are 12, no zero before the hour */
    TB_EQ_STR(tb_fmt_time(b, sizeof b, at(0, 15), false), "12:15 AM");
    TB_EQ_STR(tb_fmt_time(b, sizeof b, at(9, 5), false), "9:05 AM");
    TB_EQ_STR(tb_fmt_time(b, sizeof b, at(12, 0), false), "12:00 PM");
    TB_EQ_STR(tb_fmt_time(b, sizeof b, at(23, 59), false), "11:59 PM");
    TB_EQ_STR(tb_fmt_ampm(at(0, 15), false), "AM");
    TB_EQ_STR(tb_fmt_ampm(at(0, 15), true), "");
}

TB_TEST(fmt_hhmm_for_away)
{
    char b[16];
    TB_EQ_STR(tb_fmt_hhmm(b, sizeof b, 15, 30, false), "3:30");   /* Away's "Back at 3:30" has no AM or PM, as before */
    TB_EQ_STR(tb_fmt_hhmm(b, sizeof b, 15, 30, true), "15:30");
    TB_EQ_STR(tb_fmt_hhmm(b, sizeof b, 3, 40, true), "03:40");     /* zero-padded: the case that steps Bold down to 62 px */
    TB_EQ_STR(tb_fmt_hhmm(b, sizeof b, 3, 40, false), "3:40");
    TB_EQ_STR(tb_fmt_hhmm(b, sizeof b, 0, 5, false), "12:05");
    TB_EQ_STR(tb_fmt_hhmm(b, sizeof b, 0, 5, true), "00:05");
    TB_EQ_STR(tb_fmt_hhmm(b, sizeof b, 12, 0, false), "12:00");
    TB_EQ_STR(tb_fmt_hhmm(b, sizeof b, 23, 59, true), "23:59");
    TB_EQ_STR(tb_fmt_hhmm(b, sizeof b, 99, -4, true), "00:00");    /* out of range reads as 0, never overflows */
    char tiny[4];
    TB_EQ_STR(tb_fmt_hhmm(tiny, sizeof tiny, 15, 30, true), "15:");   /* a small buffer is cut, not overrun */
}
