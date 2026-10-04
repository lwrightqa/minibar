/*
 * brd_rtc.c: PCF85063 time registers <-> UTC seconds, and civil-date arithmetic. Owner: board builder.
 * No ESP-IDF headers (see brd_logic.h).
 */
#include "brd_logic.h"

/* Howard Hinnant's days_from_civil / civil_from_days (proleptic Gregorian, 1970-01-01 = day 0). */
int64_t brd_days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

void brd_civil_from_days(int64_t z, int64_t *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int64_t)yoe + era * 400 + (*m <= 2);
}

static uint8_t to_bcd(unsigned v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

/* BCD byte (after masking) to its value, or -1 if a digit is over 9. */
static int from_bcd(uint8_t b)
{
    unsigned hi = b >> 4, lo = b & 0x0f;
    if (hi > 9 || lo > 9) return -1;
    return (int)(hi * 10 + lo);
}

static unsigned days_in_month(int64_t y, unsigned m)
{
    static const uint8_t dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dim[m - 1];
}

void brd_rtc_encode(int64_t utc, uint8_t regs[7])
{
    const int64_t lo = brd_days_from_civil(2000, 1, 1) * 86400;
    const int64_t hi = brd_days_from_civil(2100, 1, 1) * 86400 - 1;
    if (utc < lo) utc = lo;
    if (utc > hi) utc = hi;
    int64_t days = utc / 86400;
    int64_t secs = utc % 86400;
    int64_t y;
    unsigned m, d;
    brd_civil_from_days(days, &y, &m, &d);
    regs[0] = to_bcd((unsigned)(secs % 60));            /* OS (bit 7) clear */
    regs[1] = to_bcd((unsigned)(secs / 60 % 60));
    regs[2] = to_bcd((unsigned)(secs / 3600));
    regs[3] = to_bcd(d);
    regs[4] = (uint8_t)((days + 4) % 7);                /* 1970-01-01 was a Thursday; 0 = Sunday */
    regs[5] = to_bcd(m);
    regs[6] = to_bcd((unsigned)(y - 2000));
}

bool brd_rtc_decode(const uint8_t regs[7], int64_t *utc)
{
    if (regs[0] & 0x80) return false;                   /* oscillator stopped: the time is not trustworthy */
    int sec = from_bcd(regs[0] & 0x7f);
    int min = from_bcd(regs[1] & 0x7f);
    int hour = from_bcd(regs[2] & 0x3f);
    int day = from_bcd(regs[3] & 0x3f);
    int mon = from_bcd(regs[5] & 0x1f);
    int yy = from_bcd(regs[6]);
    if (sec < 0 || sec > 59 || min < 0 || min > 59 || hour < 0 || hour > 23) return false;
    if (mon < 1 || mon > 12 || yy < 0 || day < 1) return false;
    int64_t year = 2000 + yy;
    if (year < BRD_RTC_MIN_YEAR) return false;
    if ((unsigned)day > days_in_month(year, (unsigned)mon)) return false;
    *utc = brd_days_from_civil(year, (unsigned)mon, (unsigned)day) * 86400 + hour * 3600 + min * 60 + sec;
    return true;
}
