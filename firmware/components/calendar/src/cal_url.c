/*
 * cal_url.c: the secret address's format checks and masked form. Owner: calendar builder.
 * Skeleton: the codes and messages are real (api.md 11.2, the mock-up's wording); the check is a stub.
 */
#include <string.h>

#include "cal_url.h"

cal_url_err_t cal_url_check(const char *raw, cal_url_info_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!raw || !*raw) return CAL_URL_EMPTY;
    if (strlen(raw) > CAL_URL_MAX) return CAL_URL_TOO_LONG;
    return CAL_URL_NOT_A_URL;   /* TODO(calendar): port checkIcal() */
}

const char *cal_url_err_code(cal_url_err_t e)
{
    switch (e) {
    case CAL_URL_OK: return NULL;
    case CAL_URL_EMPTY: return "bad_request";
    case CAL_URL_NOT_A_URL: return "not_a_url";
    case CAL_URL_HTTP: return "http_not_allowed";
    case CAL_URL_PUBLIC: return "public_address";
    case CAL_URL_NOT_ICS: return "not_ics";
    case CAL_URL_TOO_LONG: return "bad_value";
    }
    return "bad_value";
}

const char *cal_url_err_message(cal_url_err_t e)
{
    switch (e) {
    case CAL_URL_OK: return NULL;
    case CAL_URL_EMPTY: return "Paste your secret address first.";
    case CAL_URL_NOT_A_URL: return "That isn't a web address. Copy the whole address, starting with https://.";
    case CAL_URL_HTTP:
        return "That address starts with http://, so it isn't encrypted. Use the https:// address Google gives you.";
    case CAL_URL_PUBLIC:
        return "That's your calendar's public address, which only works if the calendar is public. Copy the Secret "
               "address in iCal format instead; it's further down the same page.";
    case CAL_URL_NOT_ICS:
        return "That isn't a calendar address. The secret address ends in .ics (Google's ends in basic.ics).";
    case CAL_URL_TOO_LONG: return "That address is too long.";
    }
    return "That address can't be used.";
}
