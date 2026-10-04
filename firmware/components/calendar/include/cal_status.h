/*
 * cal_status.h: the calendar's status as GET /api/v1/calendar reports it (api.md 11.1), and the codes and wording of
 * a check's or a sync's result (api.md 11.2). Pure C, so net's router can use it on the host too. Filled by
 * cal_sync_get_status() on the device.
 *
 * Owner: calendar builder.
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CAL_CHECK_NONE = 0, CAL_CHECK_CHECKING, CAL_CHECK_SAVED, CAL_CHECK_FAILED } cal_check_state_t;

/* What GET /api/v1/calendar reports (everything except "today", which net builds from the app's meetings). */
typedef struct {
    bool saved;
    char host[128], file[64], ending[9];    /* the masked address (cal_url_info_t); ending may be "····" */
    tb_epoch_t last_sync;           /* 0 = never */
    bool syncing;
    const char *error;              /* last sync's code ("calendar_unreachable"...) or NULL */
    char error_message[128];
    tb_epoch_t error_at;
    cal_check_state_t check;        /* the last PUT, kept 10 minutes */
    const char *check_error;        /* code or NULL */
    char check_message[160];
} cal_status_t;

/* Why a check or a sync failed. */
typedef enum {
    CAL_SYNC_OK = 0,
    CAL_SYNC_REJECTED,          /* calendar_rejected: the server answered 400, 401, 403, 404 or 410 */
    CAL_SYNC_UNREACHABLE,       /* calendar_unreachable: no answer, DNS, TLS, time-out, a 5xx, a cut-off transfer */
    CAL_SYNC_NOT_A_CALENDAR,    /* not_a_calendar: the answer isn't iCal */
    CAL_SYNC_OFFLINE,           /* offline: no Wi-Fi */
    CAL_SYNC_TOO_LARGE,         /* calendar_unreachable too (api.md has no code of its own): over the size or time limit */
} cal_sync_err_t;

/* The api.md code, NULL for CAL_SYNC_OK. */
const char *cal_sync_err_code(cal_sync_err_t e);
/* The sentence for check.message (for_check) or error.message, matching the Remote's wording. google: the address is
 * Google's (the rejected wording names Google). */
const char *cal_sync_err_message(cal_sync_err_t e, bool google, bool for_check);
/* An HTTP status as a result: 2xx is OK (a redirect is followed before this is asked), see the enum for the rest. */
cal_sync_err_t cal_sync_err_from_http(int status);

#ifdef __cplusplus
}
#endif
