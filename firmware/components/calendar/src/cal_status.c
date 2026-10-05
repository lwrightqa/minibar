/*
 * cal_status.c: check and sync results as api.md codes and the Remote's wording. Owner: calendar builder.
 * The sentences match web/remote.html's (net), so the Remote, the API and the Mac app say the same thing.
 */
#include "cal_status.h"

const char *cal_sync_err_code(cal_sync_err_t e)
{
    switch (e) {
    case CAL_SYNC_OK: return NULL;
    case CAL_SYNC_REJECTED: return "calendar_rejected";
    case CAL_SYNC_UNREACHABLE: return "calendar_unreachable";
    case CAL_SYNC_NOT_A_CALENDAR: return "not_a_calendar";
    case CAL_SYNC_OFFLINE: return "offline";
    case CAL_SYNC_TOO_LARGE: return "calendar_unreachable";
    }
    return "calendar_unreachable";
}

const char *cal_sync_err_message(cal_sync_err_t e, bool google, bool for_check)
{
    switch (e) {
    case CAL_SYNC_OK: return NULL;
    case CAL_SYNC_REJECTED:
        return google ? "Google didn't recognize that address. It may have been reset in Google Calendar."
                      : "The calendar's server didn't recognize that address. It may have been reset.";
    case CAL_SYNC_UNREACHABLE: return "MiniBar couldn't reach the calendar's server. Try again in a minute.";
    case CAL_SYNC_NOT_A_CALENDAR:
        return "That address didn't send back a calendar. Copy the Secret address in iCal format.";
    case CAL_SYNC_OFFLINE:
        return for_check ? "MiniBar isn't online, so it can't check the address." : "MiniBar isn't online, so it can't sync.";
    case CAL_SYNC_TOO_LARGE: return "That calendar is too large for MiniBar to read.";
    }
    return "MiniBar couldn't reach the calendar's server. Try again in a minute.";
}

cal_sync_err_t cal_sync_err_from_http(int status)
{
    if (status >= 200 && status < 300) return CAL_SYNC_OK;
    switch (status) {
    case 400:
    case 401:
    case 403:
    case 404:
    case 410:
        return CAL_SYNC_REJECTED;
    default:
        return CAL_SYNC_UNREACHABLE;
    }
}
