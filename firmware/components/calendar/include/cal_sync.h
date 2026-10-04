/*
 * cal_sync.h: the calendar service on the device: keeps the secret address, fetches it over HTTPS with the
 * certificate bundle, streams it through cal_ics, and hands today's meetings to the app task. ESP-IDF only.
 *
 * Owner: calendar builder.
 *
 * Behavior (decisions.md "syncing", api.md section 11):
 *   - Syncs about every 10 minutes while Wi-Fi is up, and on Sync now (quick menu TB_FX_CAL_SYNC, or the API).
 *   - Meetings switch on and off at their exact times from the saved copy (core does that from the list); syncs only
 *     refresh the list. The list is saved in NVS (nvs, namespace "cal") when it changes, so a restart without Wi-Fi
 *     still knows today's meetings.
 *   - PUT: check the format at once (cal_url.h), answer 202, fetch once in the background; only a good fetch replaces
 *     the saved address. A first address turns Calendar meetings on (core, via TB_EV_CAL_EVENT saved).
 *   - The address is write-only: kept in nvs_sec (namespace "calsec"), never logged, never returned; only the masked
 *     form (host, file, ending) is readable.
 *   - Errors: 401/403/404 calendar_rejected; DNS, TLS, time-out or no answer calendar_unreachable; not iCal
 *     not_a_calendar; no Wi-Fi offline. While an error stands, the bar keeps the last good copy.
 *   - One fetch at a time, in its own task (stack in internal RAM; TLS buffers in PSRAM); the HTTP server never waits.
 * Posts: TB_EV_CAL_MEETINGS (a malloc'ed tb_cal_meetings_t), TB_EV_CAL_STATUS, TB_EV_CAL_EVENT.
 */
#pragma once

#include "esp_err.h"
#include "cal_status.h"
#include "cal_url.h"
#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load the saved address and the saved meetings, post them, start the sync task (idle until online). */
esp_err_t cal_sync_init(void);
/* Wi-Fi came up or went down (net calls this). Coming up starts a sync if the last one is over 10 minutes old. */
void cal_sync_set_online(bool online);
/* The device zone changed (main calls this after setting TZ): recompute the window and re-read on the next sync. */
void cal_sync_set_time_zone(const char *posix);
/* PUT /api/v1/calendar and the setup page. Returns ESP_OK when the check started (202); the format error otherwise
 * in *fmt_err. from_setup: a failure shows "Calendar address didn't work · add it on the Remote". */
esp_err_t cal_sync_put(const char *url, bool from_setup, cal_url_err_t *fmt_err);
/* DELETE /api/v1/calendar: forget the address and the saved copy. ESP_ERR_NOT_FOUND if none. */
esp_err_t cal_sync_remove(void);
/* Sync now (202). ESP_ERR_NOT_FOUND if no address, ESP_ERR_INVALID_STATE if offline. */
esp_err_t cal_sync_now(void);
/* A consistent copy of the status (safe from any task). */
void cal_sync_get_status(cal_status_t *out);

#ifdef __cplusplus
}
#endif
