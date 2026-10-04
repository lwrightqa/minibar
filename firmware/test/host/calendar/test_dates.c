/* Seed tests for the calendar's date helpers and address codes (lead). Owner from here: calendar builder. */
#include "cal_ics.h"
#include "cal_tz.h"
#include "cal_url.h"
#include "tb_test.h"

TB_TEST(civil_days_round_trip)
{
    TB_EQ_INT(cal_days_from_civil(1970, 1, 1), 0);
    TB_EQ_INT(cal_days_from_civil(2026, 10, 4), 20730);
    int y, m, d;
    cal_civil_from_days(20730, &y, &m, &d);
    TB_EQ_INT(y, 2026);
    TB_EQ_INT(m, 10);
    TB_EQ_INT(d, 4);
    TB_EQ_INT(cal_weekday(2026, 10, 4), 0);     /* a Sunday */
    TB_EQ_INT(cal_days_in_month(2028, 2), 29);
    TB_EQ_INT(cal_days_in_month(2100, 2), 28);
}

TB_TEST(instance_ids_differ_and_are_never_zero)
{
    uint32_t a = cal_instance_id("abc@google.com", 1791148920);
    uint32_t b = cal_instance_id("abc@google.com", 1791148920 + 604800);
    TB_TRUE(a != 0 && b != 0);
    TB_TRUE(a != b);
}

TB_TEST(url_error_codes)
{
    TB_EQ_STR(cal_url_err_code(CAL_URL_PUBLIC), "public_address");
    TB_EQ_STR(cal_url_err_code(CAL_URL_EMPTY), "bad_request");
    cal_url_info_t info;
    TB_EQ_INT(cal_url_check("", &info), CAL_URL_EMPTY);
}
