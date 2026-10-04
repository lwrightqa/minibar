/*
 * net_main.c: net's life cycle and Wi-Fi. Owner: net builder.
 * Skeleton: initializes netif and the event loop and binds the router; Wi-Fi, setup AP, DNS, mDNS, SNTP are TODO(net).
 */
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"

#include "net.h"
#include "net_api.h"
#include "net_internal.h"

static const char *TAG = "net";
static char s_device_id[13];
static tb_app_t *s_app;

const char *net_device_id(void) { return s_device_id; }
tb_app_t *net_app(void) { return s_app; }

esp_err_t net_init(tb_app_t *app, const char *device_id)
{
    s_app = app;
    strncpy(s_device_id, device_id, sizeof(s_device_id) - 1);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    net_api_bind(app);
    net_api_init();
    /* TODO(net): esp_wifi_init, credentials from NVS ("wifi" namespace), WPA2-Enterprise via esp_eap_client */
    return ESP_OK;
}

esp_err_t net_start(void)
{
    ESP_LOGW(TAG, "net: skeleton, no Wi-Fi, HTTP or mDNS yet");
    /* TODO(net): station or setup, net_http_start(), mDNS (tinybar, _tinybar._tcp, _http._tcp), SNTP, USB reader */
    return ESP_OK;
}

bool net_wifi_configured(void)
{
    return false;   /* TODO(net) */
}

void net_setup_begin(void) { ESP_LOGI(TAG, "setup begin (TODO)"); }
void net_setup_skip(void) { ESP_LOGI(TAG, "setup skipped (TODO)"); }
void net_setup_done(void) { ESP_LOGI(TAG, "setup done (TODO)"); }
void net_name_changed(const char *name) { (void)name; }
