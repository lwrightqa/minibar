/*
 * cal_sync.c: the calendar service (NVS, HTTPS fetch, schedule). Owner: calendar builder.
 * Skeleton stubs; see cal_sync.h. The includes prove esp_http_client and the certificate bundle resolve.
 */
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "cal_ics.h"
#include "cal_sync.h"
#include "tb_bus.h"

static const char *TAG = "cal";

esp_err_t cal_sync_init(void)
{
    tb_event_t ev = {.kind = TB_EV_CAL_STATUS};
    ev.u.cal.saved = false;
    tb_bus_post(&ev);
    ESP_LOGW(TAG, "calendar: skeleton, no sync yet");
    return ESP_OK;
}

void cal_sync_set_online(bool online) { (void)online; }

void cal_sync_set_time_zone(const char *posix) { (void)posix; }

esp_err_t cal_sync_put(const char *url, bool from_setup, cal_url_err_t *fmt_err)
{
    (void)from_setup;
    cal_url_info_t info;
    cal_url_err_t e = cal_url_check(url, &info);
    if (fmt_err) *fmt_err = e;
    return e == CAL_URL_OK ? ESP_OK : ESP_ERR_INVALID_ARG;  /* TODO(calendar): start the check */
}

esp_err_t cal_sync_remove(void) { return ESP_ERR_NOT_FOUND; }

esp_err_t cal_sync_now(void) { return ESP_ERR_NOT_FOUND; }

void cal_sync_get_status(cal_status_t *out)
{
    memset(out, 0, sizeof(*out));
}
