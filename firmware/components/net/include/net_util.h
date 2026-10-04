/*
 * net_util.h: small pure helpers of the network layer, kept apart from ESP-IDF so they're tested on Linux:
 * the per-address rate limits (api.md 2.5), RFC 3339 times, the Mac's hello time rule (6.6), the setup network's DNS
 * catch-all, what a failed Wi-Fi join is called (13.3), and the USB line reader (6.4).
 *
 * Owner: net builder.
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- rate limits (api.md 2.5) ---------- */
#define NET_RATE_SLOTS 16
typedef struct {
    bool used;
    uint32_t ip;
    tb_ms_t last;       /* last refill */
    int32_t all_mt;     /* milli-requests left: 10 a second on average, bursts up to 20 */
    int32_t anon_mt;    /* without a valid token: 5 a second */
} net_rate_slot_t;
typedef struct {
    net_rate_slot_t s[NET_RATE_SLOTS];
} net_rate_t;

void net_rate_init(net_rate_t *r);
/* Count one request from ip (anon: it had no valid token). Returns 0 if it may go on, else the seconds to wait
 * (429 rate_limited, Retry-After). The least recently used address is forgotten when the table is full. */
int net_rate_take(net_rate_t *r, uint32_t ip, bool anon, tb_ms_t now);

/* ---------- time ---------- */
/* "2026-10-04T14:12:00-07:00", "...Z", with optional fractions of a second. Returns false if it isn't one. */
bool net_parse_rfc3339(const char *s, tb_epoch_t *out);
/* api.md 6.6: set the clock from the Mac's hello only if there was no network time in the last 24 hours and the
 * clock is unknown or more than 2 s off. last_ntp_ms: monotonic time of the last SNTP sync, < 0 if never. */
bool net_time_mac_should_set(tb_epoch_t mac_time, const tb_clock_t *now, tb_ms_t last_ntp_ms);

/* ---------- the setup network's DNS catch-all ---------- */
/* Answer a DNS query: every A question gets ip (network byte order) with a short TTL; other types get an empty
 * answer (so phones fall back to IPv4). Returns the answer's length, or 0 to send nothing (not a query, malformed). */
size_t net_dns_answer(const uint8_t *q, size_t qlen, uint32_t ip, uint8_t *out, size_t cap);

/* ---------- why a Wi-Fi join failed (api.md 13.3) ---------- */
typedef enum {
    NET_JOIN_OK = 0,
    NET_JOIN_WRONG_PASSWORD,
    NET_JOIN_LOGIN_FAILED,  /* work login refused */
    NET_JOIN_NOT_FOUND,     /* the network is gone */
    NET_JOIN_NO_SIGNAL,
    NET_JOIN_NO_ADDRESS,    /* joined but got no IP address, often a sign-in-page network */
} net_join_err_t;

/* ESP-IDF's wifi_err_reason_t values this mapping uses (checked against esp_wifi_types.h in esp/net_wifi.c). */
#define NET_REASON_AUTH_EXPIRE              2
#define NET_REASON_ASSOC_EXPIRE             4
#define NET_REASON_MIC_FAILURE              14
#define NET_REASON_4WAY_HANDSHAKE_TIMEOUT   15
#define NET_REASON_GROUP_KEY_UPDATE_TIMEOUT 16
#define NET_REASON_IE_IN_4WAY_DIFFERS       17
#define NET_REASON_802_1X_AUTH_FAILED       23
#define NET_REASON_BEACON_TIMEOUT           200
#define NET_REASON_NO_AP_FOUND              201
#define NET_REASON_AUTH_FAIL                202
#define NET_REASON_ASSOC_FAIL               203
#define NET_REASON_HANDSHAKE_TIMEOUT        204
#define NET_REASON_CONNECTION_FAIL          205
#define NET_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY 210
#define NET_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD 211
#define NET_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD     212

/* A station disconnect reason during a join, for a network that is a work login (enterprise) or not. */
net_join_err_t net_join_err_from_reason(int reason, bool enterprise);
/* Whether another try could help (not found and weak signals are retried; a refused password isn't). */
bool net_join_err_retry(net_join_err_t e);
/* api.md 13.3 code ("wrong_password"), the page's sentence, and the bar's headline on "Couldn't connect to X". */
const char *net_join_err_code(net_join_err_t e);
const char *net_join_err_message(net_join_err_t e);
const char *net_join_err_screen(net_join_err_t e);

/* ---------- USB lines (api.md 6.4) ---------- */
/* Assembles lines from the bytes read off the port. A line ends at LF (a CR before it is dropped). A line over
 * NET_LINE_MAX bytes keeps its first NET_LINE_MAX bytes (enough to see its "@tb " marker), the rest is thrown away up
 * to its LF, and it's reported with too_long = true. */
#define NET_LINE_MAX 2048
typedef void (*net_line_cb)(const char *line, size_t len, bool too_long, void *ctx);
typedef struct {
    char buf[NET_LINE_MAX + 2];     /* the line, a CR before its LF, and the NUL */
    size_t len;
    bool too_long;
} net_lines_t;
void net_lines_init(net_lines_t *l);
void net_lines_feed(net_lines_t *l, const uint8_t *data, size_t n, net_line_cb cb, void *ctx);

/* The text of an IPv4 address in network byte order ("10.0.4.42"). */
char *net_ip_str(uint32_t ip, char out[16]);

#ifdef __cplusplus
}
#endif
