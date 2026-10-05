/*
 * net_util.h: small pure helpers of the network layer, kept apart from ESP-IDF so they're tested on Linux:
 * the per-address rate limits (api.md 2.5), RFC 3339 times, the Mac's hello time rule (6.6), the setup network's DNS
 * catch-all, what a failed Wi-Fi join is called (13.3), the rules that close the setup network once setup is over,
 * and the USB line reader (6.4).
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
 * answer (NODATA, so phones fall back to IPv4). Returns the answer's length, or 0 to send nothing (not a query,
 * malformed). */
size_t net_dns_answer(const uint8_t *q, size_t qlen, uint32_t ip, uint8_t *out, size_t cap);
/* The first question of a DNS query, for the log: its name as text and its type (1 A, 28 AAAA, 65 HTTPS...). Bytes
 * other than printable ASCII (and a dot inside a label) show as '?'; a name longer than cap - 1 ends in "...".
 * Returns false (name "") if q isn't a standard query with a question net_dns_answer() would read. */
bool net_dns_question(const uint8_t *q, size_t qlen, char *name, size_t cap, uint16_t *qtype);

/* ---------- the setup network's captive portal ---------- */
/* The paths phones and computers fetch to find out whether a network has a sign-in page: Android (/generate_204,
 * /gen_204), iOS and macOS (/hotspot-detect.html, /library/test/success.html), Windows (/connecttest.txt,
 * /ncsi.txt, /redirect), Firefox (/canonical.html, /success.txt). On the setup network each gets a 302 to the setup
 * page whatever its Host, so the sign-in sheet opens. Case doesn't matter; a query or fragment is ignored. */
bool net_setup_probe_path(const char *path);
/* a and b (IPv4, network byte order) are in the same subnet. A zero mask or a zero b never matches. */
bool net_ip_same_subnet(uint32_t a, uint32_t b, uint32_t mask);

/* ---------- rate-limited logging ---------- */
/* At most max lines a minute, so a chatty phone can't flood the port. The minute starts with its first line. */
typedef struct {
    bool started;
    tb_ms_t start;
    int n;          /* lines logged in this minute */
    int dropped;    /* lines held back in this minute */
} net_log_quota_t;
/* Whether one more line may be logged at now. When a new minute starts, *dropped_before (optional) gets how many
 * lines the previous minute held back (0 otherwise), so the log can say so once. */
bool net_log_quota_take(net_log_quota_t *q, tb_ms_t now, int max, int *dropped_before);
/* Text from the network (a Host header, a path) made safe for one log line: printable ASCII kept, every other byte
 * shown as '?', cut to cap - 1 bytes ending in "..." when it's longer. NULL shows as "-". Returns out. */
char *net_log_text(char *out, size_t cap, const char *in);
/* A request's path for the log, as net_log_text() but without its query or fragment: other apps' plain-HTTP requests
 * can carry tokens there. A path that had one ends in "?..." ("/generate_204?x=1" logs as "/generate_204?..."). */
char *net_log_path(char *out, size_t cap, const char *path);

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

/* ---------- the setup network once setup is over (decisions.md, Wi-Fi) ---------- */
/* Once setup is over, MiniBar-Setup closes 15 s after the Connected screen moves on (the page reads the result
 * meanwhile) and never comes back on its own: only Set up again (the QR code) opens it. These are the rules that keep
 * that true when a step goes wrong; net_wifi.c does the radio work. */

/* What net does when it's still in setup although core isn't on a setup screen any more: core's TB_FX_WIFI_DONE or
 * _SKIP never arrived (core's effect queue drops effects when it's full). FINISH is what DONE does (station, and the
 * setup network closes after its linger), SKIP what SKIP does (radio off). KEEP while core shows setup, or when net
 * isn't in setup. */
typedef enum { NET_SETUP_KEEP = 0, NET_SETUP_FINISH, NET_SETUP_SKIP } net_setup_catch_up_t;
net_setup_catch_up_t net_setup_catch_up(tb_wifi_mode_t core_mode, bool net_in_setup);

/* The setup network is up, or still meant to be, although setup is over and no close is on its way (the linger, a
 * retry, or a close already in the worker's queue): close it. in_setup: net is in setup (the QR code and the setup
 * screens), when the network belongs up. */
bool net_setup_ap_stray(bool in_setup, bool ap_want, bool ap_up, bool close_pending);

/* Closing the setup network failed `failures` times in a row (1, 2, ...): how long until the next try, and whether
 * that try restarts the radio in station mode (esp_wifi_stop, then start; the office link drops for a second) rather
 * than only changing its mode. Changing the mode is tried NET_AP_CLOSE_SOFT_TRIES times (after 1 and 2 s); then the
 * radio restarts after 5 and 10 s, then every 30 s until it works. */
#define NET_AP_CLOSE_SOFT_TRIES 3
int32_t net_ap_close_retry_ms(int failures, bool *restart_radio);

/* Saving the network that just worked failed `failures` times (1, 2, ...): how long until the next try (5 s, 30 s,
 * 2 min, 10 min, 30 min), or 0 to give up after NET_CREDS_SAVE_TRIES tries in all. Without the saved network the next
 * start would open the setup network again. */
#define NET_CREDS_SAVE_TRIES 6
int32_t net_creds_save_retry_ms(int failures);

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

/* ---------- JSON nesting ---------- */
/* How deep objects and arrays may nest in a request (an HTTP body or a USB line). The deepest real request is 3
 * levels (a USB request, its body, a settings section). cJSON recurses once per level on the app task's 12 KB stack
 * (about 64 bytes each on the S3), and so does cJSON_Delete, so a hostile body mustn't go deep. The same limit is
 * CJSON_NESTING_LIMIT inside cJSON, set for the whole build (CMakeLists.txt, test/host/CMakeLists.txt). */
#define NET_JSON_DEPTH_MAX 16
/* Whether the JSON text s[0..n) nests objects and arrays at most max deep. Brackets inside strings don't count;
 * nothing else is checked (cJSON does that). The router runs it before cJSON. */
bool net_json_depth_ok(const char *s, size_t n, int max);

/* ---------- the port's log output (api.md 6.4) ---------- */
/* Where the log output is in its current line: at its start, inside the ANSI color codes at its start (which the
 * Mac app strips before it looks for the marker), or past them. */
typedef enum { NET_LOG_LINE_START = 0, NET_LOG_ESC, NET_LOG_CSI, NET_LOG_MID } net_log_state_t;
/* A log line never starts with the "@tb " marker, whatever it prints (an SSID with a line break in it, say), so log
 * output can't pass for a protocol line. Scans buf from *st and returns how many bytes can go out as they are. If
 * that's less than len, buf[ret] is an '@' that would start a line (after any color codes): write a space, set *st
 * to NET_LOG_MID, and go on from buf[ret]. *st is the state after the bytes counted. */
size_t net_log_scan(net_log_state_t *st, const char *buf, size_t len);

#ifdef __cplusplus
}
#endif
