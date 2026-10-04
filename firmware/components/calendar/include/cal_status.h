/*
 * cal_status.h: the calendar's status as GET /api/v1/calendar reports it (api.md 11.1). Pure C, so net's router can
 * use it on the host too. Filled by cal_sync_get_status() on the device.
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
    char host[128], file[64], ending[5];
    tb_epoch_t last_sync;           /* 0 = never */
    bool syncing;
    const char *error;              /* last sync's code ("calendar_unreachable"...) or NULL */
    char error_message[128];
    tb_epoch_t error_at;
    cal_check_state_t check;        /* the last PUT, kept 10 minutes */
    const char *check_error;        /* code or NULL */
    char check_message[160];
} cal_status_t;

#ifdef __cplusplus
}
#endif
