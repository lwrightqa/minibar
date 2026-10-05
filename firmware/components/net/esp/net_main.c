/*
 * net_main.c: net's life cycle (net.h). Owner: net builder.
 *
 * net_init()  (main task, after the app task started): netif and event loop, cJSON in PSRAM, the Wi-Fi driver and
 *             the saved credentials, the token table, and the router bound to the app model.
 * net_start() the HTTP server, then the station (or the setup network when nothing is saved), then the USB reader
 *             and its "ready" line. mDNS and SNTP start once the station has an address (net_wifi.c).
 */
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "sdkconfig.h"

#include "net.h"
#include "net_api.h"
#include "net_internal.h"
#include "tb_bus.h"
#include "tb_text.h"

static const char *TAG = "net";
static char s_device_id[13];
static char s_name[TB_DEVICE_NAME_BYTES] = "MiniBar";
static tb_app_t *s_app;
static bool s_started;

const char *net_device_id(void) { return s_device_id; }
tb_app_t *net_app(void) { return s_app; }
const char *net_bar_name(void) { return s_name; }

/* cJSON trees for replies (up to 8 KB of JSON) go to PSRAM, so they never press on internal RAM. */
static void *json_malloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

static void api_init_job(void *ctx)
{
    (void)ctx;
    net_api_init();
}

esp_err_t net_init(tb_app_t *app, const char *device_id)
{
    s_app = app;
    tb_strlcpy(s_device_id, device_id, sizeof s_device_id);
    /* The name is only changed by the app task afterwards, through net_name_changed(). */
    if (app && app->set.device.name[0]) tb_strlcpy(s_name, app->set.device.name, sizeof s_name);
    cJSON_Hooks hooks = {.malloc_fn = json_malloc, .free_fn = free};
    cJSON_InitHooks(&hooks);

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    net_api_bind(app);
#if CONFIG_TINYBAR_API_AUTH_NONE
    net_api_set_auth(false);
#else
    net_api_set_auth(true);
#endif
    err = net_wifi_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(err));
        return err;
    }
    /* The token table and the Mac table belong to the app task (they tell core the paired count), so they're set up
     * there, and only there: running it here would touch g_app from a second task. tb_bus_exec() returns false only
     * when the job was dropped before it ran, so trying again can't run it twice. */
    for (int tries = 1; !tb_bus_exec(api_init_job, NULL, 5000); tries++)
        ESP_LOGE(TAG, "the app task hasn't run the router's set-up after %d s; still waiting", tries * 5);
    return ESP_OK;
}

esp_err_t net_start(void)
{
    if (s_started) return ESP_OK;
    s_started = true;
    /* The server first: it listens on every address before any network is up, so a phone that joins the setup
     * network finds port 80 answering, and the log reads in the order things happen. */
    esp_err_t err = net_http_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "HTTP server failed: %s", esp_err_to_name(err));
    net_wifi_start();
    net_usb_start();
    return ESP_OK;
}


void net_setup_begin(void) { net_wifi_setup_begin(); }
void net_setup_skip(void) { net_wifi_setup_skip(); }
void net_setup_done(void) { net_wifi_setup_done(); }

void net_name_changed(const char *name)
{
    if (!name || !name[0] || !strcmp(name, s_name)) return;
    tb_strlcpy(s_name, name, sizeof s_name);
    net_mdns_set_name(s_name);
}
