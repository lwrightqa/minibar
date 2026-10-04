/*
 * net_port_esp.c: net_port.h on the device. Owner: net builder.
 * Skeleton: identity, time and randomness are real; Wi-Fi and calendar answers are placeholders.
 */
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"

#include "cal_sync.h"
#include "net_internal.h"
#include "net_port.h"

void net_port_device_id(char out[13])
{
    memcpy(out, net_device_id(), 13);
}

const char *net_port_fw_version(void)
{
    return esp_app_get_description()->version;
}

tb_clock_t net_port_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tb_clock_t c = {.mono = esp_timer_get_time() / 1000, .wall = tv.tv_sec, .valid = tv.tv_sec > 1700000000};
    return c;
}

net_time_source_t net_port_time_source(void)
{
    return NET_TIME_NONE;   /* TODO(net): track SNTP, RTC (main tells net at boot), Mac */
}

bool net_port_set_time_from_mac(tb_epoch_t t)
{
    (void)t;
    return false;   /* TODO(net): api.md 6.6 rule; then post TB_EV_TIME_SET {source mac} */
}

bool net_port_set_time_zone(const char *iana)
{
    (void)iana;
    return false;   /* TODO(net): through the settings patch; main applies TZ on TB_FX_SAVE_SETTINGS */
}

bool net_port_time_zone_known(const char *iana)
{
    (void)iana;
    return true;    /* TODO(net): cal_tz_posix_for(iana) != NULL */
}

void net_port_wifi(net_wifi_info_t *out)
{
    memset(out, 0, sizeof(*out));
    out->state = NET_WIFI_OFFLINE;
    strcpy(out->host, "tinybar.local");
}

int net_port_setup_networks(net_scan_entry_t *out, int max)
{
    (void)out; (void)max;
    return 0;
}

bool net_port_setup_join(const char *ssid, const char *username, const char *password)
{
    (void)ssid; (void)username; (void)password;
    return false;
}

void net_port_setup_status(net_join_status_t *out)
{
    memset(out, 0, sizeof(*out));
}

int net_port_cal_put(const char *url, bool from_setup)
{
    cal_url_err_t e = CAL_URL_OK;
    return cal_sync_put(url, from_setup, &e) == ESP_OK ? 0 : (int)e;
}

int net_port_cal_remove(void)
{
    return cal_sync_remove() == ESP_OK ? 0 : -1;
}

int net_port_cal_sync_now(void)
{
    esp_err_t e = cal_sync_now();
    return e == ESP_OK ? 0 : e == ESP_ERR_NOT_FOUND ? -1 : -2;
}

void net_port_cal_status(cal_status_t *out)
{
    cal_sync_get_status(out);
}

void net_port_random(void *buf, size_t n)
{
    esp_fill_random(buf, n);    /* TODO(net): RF must be on, or bootloader_random_enable() around it (api.md 15) */
}

void net_port_sha256(const void *data, size_t n, uint8_t out[32])
{
    mbedtls_sha256(data, n, out, 0);
}

bool net_port_tokens_save(const void *blob, size_t n)
{
    (void)blob; (void)n;
    return false;   /* TODO(net): nvs_sec, namespace "tokens" */
}

size_t net_port_tokens_load(void *blob, size_t cap)
{
    (void)blob; (void)cap;
    return 0;
}
