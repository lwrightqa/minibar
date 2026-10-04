/*
 * net_port_esp.c: net_port.h on the device. Owner: net builder.
 * Identity, time, randomness, hashing, the token table in nvs_sec, and the calendar service. The Wi-Fi functions are
 * in net_wifi.c, next to the state they read. All of these run on the app task (the router).
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "bootloader_random.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

#include "cal_sync.h"
#include "cal_tz.h"
#include "cal_url.h"
#include "net_internal.h"
#include "net_port.h"

static const char *TAG = "net.port";

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
    tb_clock_t c = {.mono = esp_timer_get_time() / 1000, .wall = tv.tv_sec, .valid = net_time_valid()};
    return c;
}

net_time_source_t net_port_time_source(void)
{
    return net_time_source();
}

bool net_port_set_time_from_mac(tb_epoch_t t)
{
    return net_time_set_from_mac(t);
}

bool net_port_time_zone_known(const char *iana)
{
    return iana && iana[0] && cal_tz_posix_for(iana) != NULL;
}

/* ---------- calendar ---------- */

int net_port_cal_check(const char *url)
{
    cal_url_info_t *info = malloc(sizeof *info);    /* 1.4 KB: not on the app task's stack */
    if (!info) return (int)CAL_URL_NOT_A_URL;
    int e = (int)cal_url_check(url, info);
    free(info);
    return e;
}

int net_port_cal_put(const char *url, bool from_setup)
{
    cal_url_err_t e = CAL_URL_OK;
    if (cal_sync_put(url, from_setup, &e) == ESP_OK) return 0;
    return e != CAL_URL_OK ? (int)e : (int)CAL_URL_NOT_A_URL;
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

/* ---------- randomness, hashing, the token table ---------- */

void net_port_random(void *buf, size_t n)
{
    /* esp_fill_random is a true RNG only while the RF runs; otherwise (Wi-Fi skipped) borrow the SAR ADC's noise for
     * a moment (api.md 15). Never both: bootloader_random_enable() must not run while Wi-Fi does. */
    if (net_wifi_rf_on()) {
        esp_fill_random(buf, n);
        return;
    }
    bootloader_random_enable();
    esp_fill_random(buf, n);
    bootloader_random_disable();
}

void net_port_sha256(const void *data, size_t n, uint8_t out[32])
{
    mbedtls_sha256(data, n, out, 0);
}

bool net_port_tokens_save(const void *blob, size_t n)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition("nvs_sec", "tokens", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "table", blob, n);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "saving paired devices failed: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

size_t net_port_tokens_load(void *blob, size_t cap)
{
    nvs_handle_t h;
    if (nvs_open_from_partition("nvs_sec", "tokens", NVS_READONLY, &h) != ESP_OK) return 0;
    size_t n = cap;
    esp_err_t err = nvs_get_blob(h, "table", blob, &n);
    nvs_close(h);
    return err == ESP_OK ? n : 0;
}
