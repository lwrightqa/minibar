/*
 * fake_port.c: net_port.h for the host tests. Owner: net builder (seeded by the lead).
 * Tests change the fake_* variables to put the "device" into the state they need.
 */
#include <string.h>

#include "fake_port.h"
#include "net_port.h"

tb_clock_t fake_now = {.mono = 1000, .wall = 1791148920, .valid = true};
net_wifi_info_t fake_wifi = {.state = NET_WIFI_CONNECTED, .ssid = "Office-WiFi", .ip = "10.0.4.42", .host = "tinybar.local", .rssi = -61};
net_time_source_t fake_time_source = NET_TIME_NTP;
cal_status_t fake_cal;
static uint8_t s_tokens[4096];
static size_t s_tokens_n;
static uint32_t s_rand = 12345;

void net_port_device_id(char out[13]) { memcpy(out, "f412fa3f2a1c", 13); }
const char *net_port_fw_version(void) { return "1.0.0"; }
tb_clock_t net_port_now(void) { return fake_now; }
net_time_source_t net_port_time_source(void) { return fake_time_source; }
bool net_port_set_time_from_mac(tb_epoch_t t) { (void)t; return false; }
bool net_port_set_time_zone(const char *iana) { (void)iana; return true; }
bool net_port_time_zone_known(const char *iana) { return iana && *iana; }
void net_port_wifi(net_wifi_info_t *out) { *out = fake_wifi; }
int net_port_setup_networks(net_scan_entry_t *out, int max) { (void)out; (void)max; return 0; }
bool net_port_setup_join(const char *ssid, const char *u, const char *p) { (void)ssid; (void)u; (void)p; return true; }
void net_port_setup_status(net_join_status_t *out) { memset(out, 0, sizeof(*out)); }
int net_port_cal_put(const char *url, bool from_setup) { (void)url; (void)from_setup; return 0; }
int net_port_cal_remove(void) { return fake_cal.saved ? 0 : -1; }
int net_port_cal_sync_now(void) { return fake_cal.saved ? 0 : -1; }
void net_port_cal_status(cal_status_t *out) { *out = fake_cal; }

void net_port_random(void *buf, size_t n)
{
    uint8_t *p = buf;
    for (size_t i = 0; i < n; i++) {
        s_rand = s_rand * 1103515245u + 12345u;
        p[i] = (uint8_t)(s_rand >> 16);
    }
}

void net_port_sha256(const void *data, size_t n, uint8_t out[32])
{
    /* Not SHA-256: a stand-in that's deterministic and spreads bits. Tests check behavior, not digests.
     * TODO(net): use a real SHA-256 (a small public-domain one) if a test needs known digests. */
    memset(out, 0, 32);
    const uint8_t *p = data;
    for (size_t i = 0; i < n; i++) out[i % 32] = (uint8_t)(out[i % 32] * 31 + p[i] + 7);
}

bool net_port_tokens_save(const void *blob, size_t n)
{
    if (n > sizeof s_tokens) return false;
    memcpy(s_tokens, blob, n);
    s_tokens_n = n;
    return true;
}

size_t net_port_tokens_load(void *blob, size_t cap)
{
    size_t n = s_tokens_n < cap ? s_tokens_n : cap;
    memcpy(blob, s_tokens, n);
    return n;
}
