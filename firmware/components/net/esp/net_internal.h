/* net_internal.h: shared between net's device files. Owner: net builder. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "net_port.h"
#include "tb_app.h"

/* ---------- net_main.c ---------- */
const char *net_device_id(void);
tb_app_t *net_app(void);
/* The bar's name as last handed over (settings.device.name), for mDNS. */
const char *net_bar_name(void);

/* ---------- net_wifi.c: Wi-Fi, the setup network, mDNS, SNTP ---------- */
esp_err_t net_wifi_init(void);
/* Station with the saved credentials, or the setup network when there are none. */
void net_wifi_start(void);
bool net_wifi_have_creds(void);
/* How the bar starts (tb_app_init): OFFLINE when Skip was the last choice, OK with a saved network, else SETUP. */
tb_wifi_mode_t net_wifi_start_mode(void);
void net_wifi_setup_begin(void);
void net_wifi_setup_skip(void);
void net_wifi_setup_done(void);
/* net.h net_setup_follow(): core's Wi-Fi mode, and the app task's monotonic clock in ms. */
void net_wifi_follow(tb_wifi_mode_t core_mode, int64_t now_ms);
/* The radio is on (esp_fill_random gives true random numbers then). */
bool net_wifi_rf_on(void);
/* MiniBar-Setup's access point is up (between WIFI_EVENT_AP_START and AP_STOP), for net_http.c's fallback when a
 * socket's own address can't tell which network a request came in on. */
bool net_wifi_setup_net_up(void);
/* net_port.h's Wi-Fi functions are implemented there too. */
void net_mdns_set_name(const char *name);

/* ---------- time (net_wifi.c) ---------- */
net_time_source_t net_time_source(void);
/* The Mac's hello (api.md 6.6). */
bool net_time_set_from_mac(tb_epoch_t t);
/* The system clock is valid (RTC at boot, SNTP, or the Mac). */
bool net_time_valid(void);

/* ---------- net_dns.c: the setup network's DNS catch-all ---------- */
/* Answer queries from the subnet ip/mask (network byte order) with ip, until net_dns_stop(). net_wifi.c calls it on
 * WIFI_EVENT_AP_START. */
void net_dns_start(uint32_t ip, uint32_t mask);
void net_dns_stop(void);

/* ---------- net_http.c ---------- */
esp_err_t net_http_start(void);

/* ---------- net_usb.c ---------- */
/* Start the reader task and send the "ready" line. */
void net_usb_start(void);
