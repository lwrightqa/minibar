/* test_url.c: the secret address checks, a port of the mock-up's checkIcal(). Owner: calendar builder. */
#include <string.h>

#include "cal_fixture.h"
#include "cal_url.h"

#define GOOGLE "https://calendar.google.com/calendar/ical/sam.lee%40example.com/private-8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a/basic.ics"

TB_TEST(url_google_secret_address)
{
    cal_url_info_t i;
    TB_EQ_INT(cal_url_check(GOOGLE, &i), CAL_URL_OK);
    TB_EQ_STR(i.url, GOOGLE);
    TB_EQ_STR(i.host, "calendar.google.com");
    TB_EQ_STR(i.file, "basic.ics");
    TB_EQ_STR(i.ending, "3f2a");
    TB_TRUE(i.google);
    TB_EQ_STR(i.self_email, "sam.lee@example.com");
    /* webcal://, spaces around, a #fragment, uppercase scheme and host */
    TB_EQ_INT(cal_url_check("  webcal://Calendar.Google.com/calendar/ical/sam.lee%40example.com/private-8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a/basic.ics#x \n", &i), CAL_URL_OK);
    TB_EQ_STR(i.url, GOOGLE);
    TB_EQ_INT(cal_url_check("HTTPS://calendar.google.com/calendar/ical/sam.lee%40example.com/private-8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a/basic.ics", &i), CAL_URL_OK);
    TB_EQ_STR(i.url, GOOGLE);
    /* a group calendar's id */
    TB_EQ_INT(cal_url_check("https://calendar.google.com/calendar/ical/c_9f2b%40group.calendar.google.com/private-0a1b2c3d/basic.ics", &i), CAL_URL_OK);
    TB_EQ_STR(i.self_email, "c_9f2b@group.calendar.google.com");
    TB_EQ_STR(i.ending, "2c3d");
}

TB_TEST(url_errors_in_the_mock_up_order)
{
    cal_url_info_t i;
    TB_EQ_INT(cal_url_check("", &i), CAL_URL_EMPTY);
    TB_EQ_INT(cal_url_check("   \t ", &i), CAL_URL_EMPTY);
    TB_EQ_INT(cal_url_check(NULL, &i), CAL_URL_EMPTY);
    TB_EQ_INT(cal_url_check("my calendar", &i), CAL_URL_NOT_A_URL);
    TB_EQ_INT(cal_url_check("calendar.google.com/calendar/ical/x/basic.ics", &i), CAL_URL_NOT_A_URL);
    TB_EQ_INT(cal_url_check("ftp://calendar.google.com/x.ics", &i), CAL_URL_NOT_A_URL);
    TB_EQ_INT(cal_url_check("https://localhost/basic.ics", &i), CAL_URL_NOT_A_URL);     /* no dot in the host */
    TB_EQ_INT(cal_url_check("https:///basic.ics", &i), CAL_URL_NOT_A_URL);
    TB_EQ_INT(cal_url_check("https://cal endar.com/basic.ics", &i), CAL_URL_NOT_A_URL);
    TB_EQ_INT(cal_url_check("https://calendar.google.com:99999/x.ics", &i), CAL_URL_NOT_A_URL);
    TB_EQ_INT(cal_url_check("http://calendar.google.com/calendar/ical/a/private-b/basic.ics", &i), CAL_URL_HTTP);
    TB_EQ_INT(cal_url_check("https://calendar.google.com/calendar/ical/sam.lee%40example.com/public/basic.ics", &i),
              CAL_URL_PUBLIC);
    TB_EQ_INT(cal_url_check("https://calendar.google.com/calendar/embed?src=sam.lee%40example.com", &i), CAL_URL_NOT_ICS);
    TB_EQ_INT(cal_url_check("https://example.com/calendar.ics.html", &i), CAL_URL_NOT_ICS);
    TB_EQ_INT(cal_url_check("https://example.com/calendar?file=basic.ics", &i), CAL_URL_NOT_ICS);  /* the query isn't the path */
    /* http:// is reported before /public/ and .ics, as in the mock-up */
    TB_EQ_INT(cal_url_check("http://calendar.google.com/calendar/ical/x/public/basic.html", &i), CAL_URL_HTTP);
    TB_EQ_STR(i.url, "");   /* cleared on an error */
    char big[CAL_URL_MAX + 10];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    memcpy(big, "https://", 8);
    TB_EQ_INT(cal_url_check(big, &i), CAL_URL_TOO_LONG);
}

TB_TEST(url_other_providers)
{
    cal_url_info_t i;
    TB_EQ_INT(cal_url_check("https://outlook.office365.com/owa/calendar/9f8e7d6c@example.com/0a1b2c3d4e5f/calendar.ics", &i), CAL_URL_OK);
    TB_FALSE(i.google);
    TB_EQ_STR(i.host, "outlook.office365.com");
    TB_EQ_STR(i.file, "calendar.ics");
    TB_EQ_STR(i.ending, "4e5f");    /* the second-to-last path segment */
    TB_EQ_STR(i.self_email, "");
    /* iCloud's webcal address, with a port and a query */
    TB_EQ_INT(cal_url_check("webcal://p52-caldav.icloud.com:443/published/2/MTIzNDU2Nzg5MDEyMzQ1Nj.ics?x=1", &i), CAL_URL_OK);
    TB_EQ_STR(i.url, "https://p52-caldav.icloud.com:443/published/2/MTIzNDU2Nzg5MDEyMzQ1Nj.ics?x=1");
    TB_EQ_STR(i.ending, "2");
    /* https's default port is left out, user:password@ is dropped, spaces are encoded */
    TB_EQ_INT(cal_url_check("https://me:pw@cal.example.org:443/my cal/basic.ICS", &i), CAL_URL_OK);
    TB_EQ_STR(i.url, "https://cal.example.org/my%20cal/basic.ICS");
    TB_EQ_STR(i.ending, "0cal");     /* the last four characters of "my%20cal" */
    /* a token too short for four characters, and none at all */
    TB_EQ_INT(cal_url_check("https://example.com/ab/basic.ics", &i), CAL_URL_OK);
    TB_EQ_STR(i.ending, "ab");
    TB_EQ_INT(cal_url_check("https://example.com/basic.ics", &i), CAL_URL_OK);
    TB_EQ_STR(i.ending, "\xC2\xB7\xC2\xB7\xC2\xB7\xC2\xB7");
    /* google.com subdomains count, look-alikes don't */
    TB_EQ_INT(cal_url_check("https://notgoogle.com/a/basic.ics", &i), CAL_URL_OK);
    TB_FALSE(i.google);
    TB_EQ_INT(cal_url_check("https://google.com.evil.example/a/basic.ics", &i), CAL_URL_OK);
    TB_FALSE(i.google);
}

TB_TEST(url_messages)
{
    TB_EQ_STR(cal_url_err_code(CAL_URL_NOT_A_URL), "not_a_url");
    TB_EQ_STR(cal_url_err_code(CAL_URL_HTTP), "http_not_allowed");
    TB_EQ_STR(cal_url_err_code(CAL_URL_NOT_ICS), "not_ics");
    TB_EQ_STR(cal_url_err_code(CAL_URL_TOO_LONG), "bad_value");
    TB_TRUE(cal_url_err_code(CAL_URL_OK) == NULL);
    TB_EQ_STR(cal_url_err_message(CAL_URL_EMPTY), "Paste your secret address first.");
    TB_TRUE(strstr(cal_url_err_message(CAL_URL_PUBLIC), "Secret address in iCal format") != NULL);
}
