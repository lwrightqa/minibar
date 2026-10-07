/*
 * cal_status.h: the calendar's status as GET /api/v1/calendar reports it (api.md 11.1), and the codes and wording of
 * a check's or a sync's result (api.md 11.2). Pure C, so net's router can use it on the host too. Filled by
 * cal_sync_get_status() on the device.
 *
 * Owner: calendar builder.
 */
#pragma once

#include "cal_list.h"
#include "cal_url.h"
#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CAL_CHECK_NONE = 0, CAL_CHECK_CHECKING, CAL_CHECK_SAVED, CAL_CHECK_FAILED } cal_check_state_t;

/* What GET /api/v1/calendar reports (everything except "today", which net builds from the app's meetings): the
 * calendars as one. The address is never in it, not even masked (decisions.md, Multiple calendars). */
typedef struct {
    bool saved;                     /* at least one calendar */
    tb_epoch_t last_sync;           /* the latest good sync of any calendar, 0 = never */
    bool syncing;
    const char *error;              /* the first failing calendar's last sync code ("calendar_unreachable"...) or NULL */
    char error_message[128];
    tb_epoch_t error_at;
    cal_check_state_t check;        /* the last PUT or POST, kept 10 minutes */
    int check_id;                   /* the calendar it was for (api id), 0 for a new one */
    const char *check_error;        /* code or NULL */
    char check_message[160];
} cal_status_t;

/* One calendar for GET /api/v1/calendars (api.md 11.5). */
typedef struct {
    bool used;
    int id;                         /* slot + 1: stable for as long as the calendar exists */
    char name[TB_CAL_NAME_BYTES], tag[TB_CAL_TAG_BYTES];
    tb_epoch_t last_sync;           /* 0 = never */
    bool syncing;
    bool failing;                   /* its meetings are left out of the bar */
    const char *error;
    char error_message[128];
    tb_epoch_t error_at;
    int left_today;                 /* its own meetings still to come or in progress today */
} cal_item_t;

typedef struct {
    int n;                          /* used entries */
    cal_item_t c[TB_CALS_MAX];      /* by slot */
} cal_items_t;

/* What a calendar change answers: 0 and 0 when it started. */
typedef struct {
    int url_err;                    /* cal_url_err_t */
    int list_err;                   /* cal_list_err_t */
} cal_res_t;

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
