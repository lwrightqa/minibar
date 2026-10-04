/*
 * net_port.h: what net's pure protocol code (proto/) needs from the device. Implemented by esp/net_port_esp.c on the
 * bar and by fakes in test/host/net/ on Linux, so the router, the Mac table and pairing are host-testable.
 *
 * Owner: net builder. Every function here is called from the app task only (the router runs there).
 */
#pragma once

#include "cal_status.h"
#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* api.md 7.1 time_source */
typedef enum { NET_TIME_NONE = 0, NET_TIME_NTP, NET_TIME_RTC, NET_TIME_MAC } net_time_source_t;

/* api.md 7.3 "wifi" and 7.1 "wifi". SETUP while the setup screens show (the TinyBar-Setup network is up). */
typedef enum { NET_WIFI_CONNECTED = 0, NET_WIFI_OFFLINE, NET_WIFI_SETUP } net_wifi_state_t;
typedef struct {
    net_wifi_state_t state;
    bool sta_up;                    /* joined the office Wi-Fi and has an address (also during "Set up again") */
    char ssid[TB_SSID_BYTES];       /* "" when not joined */
    char ip[TB_IP_BYTES];           /* the station's IPv4, "" when not joined */
    char host[64];                  /* "tinybar.local" or the name mDNS ended up with */
    int8_t rssi;                    /* dBm; 0 when not joined */
} net_wifi_info_t;

#define NET_SETUP_IP "192.168.4.1"

/* One network the setup page can offer (api.md 13.1). */
typedef enum { NET_SEC_OPEN = 0, NET_SEC_PASSWORD, NET_SEC_WORK_LOGIN } net_security_t;
typedef struct {
    char ssid[TB_SSID_BYTES];
    net_security_t security;
    int8_t rssi;
} net_scan_entry_t;

/* api.md 13.3 */
typedef enum { NET_JOIN_IDLE = 0, NET_JOIN_CONNECTING, NET_JOIN_CONNECTED, NET_JOIN_FAILED } net_join_state_t;
typedef struct {
    net_join_state_t state;
    const char *error;              /* "wrong_password", "login_failed", "not_found", "no_signal", "no_address" or NULL */
    const char *message;            /* "The password didn't work." or NULL */
    char host[64], ip[TB_IP_BYTES];
} net_join_status_t;

/* ---------- identity and time ---------- */
void net_port_device_id(char out[13]);
const char *net_port_fw_version(void);
tb_clock_t net_port_now(void);
net_time_source_t net_port_time_source(void);
/* The Mac's hello time (api.md 6.6): set the clock if there was no network time in the last 24 h and it's more than
 * 2 s off. Returns true if the clock was set (time_source becomes "mac"). */
bool net_port_set_time_from_mac(tb_epoch_t t);
/* The IANA zone is in the bar's table (calendar/cal_tz.h). */
bool net_port_time_zone_known(const char *iana);

/* ---------- Wi-Fi ---------- */
void net_port_wifi(net_wifi_info_t *out);
/* The setup network's scan, strongest first (cached; refreshed in the background). Returns the count. */
int net_port_setup_networks(net_scan_entry_t *out, int max);
/* Start joining (api.md 13.2); a join already running is replaced. calendar_url ("" or NULL for none) is handed to
 * the calendar service once the bar is online. Credentials are saved only once the join works. Returns false if
 * the Wi-Fi driver refused. */
bool net_port_setup_join(const char *ssid, const char *username, const char *password, const char *calendar_url);
void net_port_setup_status(net_join_status_t *out);

/* ---------- calendar (calendar/cal_sync.h and cal_url.h on the device) ---------- */
/* The address's format (cal_url_err_t: 0 fine, else the error). */
int net_port_cal_check(const char *url);
/* 0 started (202); otherwise the cal_url_err_t format error. */
int net_port_cal_put(const char *url, bool from_setup);
/* 0 removed, -1 none saved. */
int net_port_cal_remove(void);
/* 0 started, -1 none saved, -2 offline. */
int net_port_cal_sync_now(void);
void net_port_cal_status(cal_status_t *out);

/* ---------- randomness, hashing, storage for pairing ---------- */
/* Hardware random bytes (esp_fill_random with the RF on, or bootloader_random_enable() around it; api.md 15). */
void net_port_random(void *buf, size_t n);
void net_port_sha256(const void *data, size_t n, uint8_t out[32]);
/* Persist the token table (an opaque blob from net_pair.c) in nvs_sec. */
bool net_port_tokens_save(const void *blob, size_t n);
size_t net_port_tokens_load(void *blob, size_t cap);

#ifdef __cplusplus
}
#endif
