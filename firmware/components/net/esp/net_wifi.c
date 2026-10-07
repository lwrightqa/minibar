/*
 * net_wifi.c: Wi-Fi for MiniBar. Owner: net builder.
 *
 * The station joins the office Wi-Fi with the saved credentials (WPA2/WPA3 Personal, open, or WPA2-Enterprise
 * "work login" with PEAP/TTLS through esp_eap_client) and reconnects with a back-off when it drops. The setup
 * network is APSTA: the open "MiniBar-Setup" access point at NET_SETUP_IP with a DNS catch-all (net_dns.c) and the
 * setup page (net_http.c); the station side tries the chosen network while the access point stays up, so the phone
 * can read the result (api.md 13.3). Once the bar has an address: mDNS (api.md 3) and SNTP.
 *
 * Opening the setup network (setup_open(), on the worker), in the order of ESP-IDF's captive_portal example:
 *   1. one scan for the page's list, with the access point still closed (an APSTA scan hops channels for a second or
 *      two, and a phone on the setup network loses packets, its captive-portal check among them);
 *   2. the access point's DHCP server, changed while it's stopped: the setup address, the bar as the DNS server, and
 *      no captive-portal option 114 (RFC 8908 wants an HTTPS API address there; an http one only invites oddities);
 *   3. mode, then the access point's settings, then esp_wifi_start(), so it never comes up under the driver's
 *      default name (during "Set up again" that restart drops the office link, and the calendar is told);
 *   4. the DNS catch-all once the access point is up (WIFI_EVENT_AP_START).
 * If the setup address can't be set, the network stays closed (it would answer every request 421); Set up again
 * tries again. While a phone is on the setup network nothing scans, unless the page has no networks to show.
 * The log says when the setup network opens (channel and address), and each phone that joins, gets an address and
 * leaves (INFO, at most 40 lines a minute).
 *
 * Closing it once setup is over (decisions.md, Wi-Fi: once the bar is set up it never broadcasts MiniBar-Setup unless
 * a person starts setup again). The Connected screen moving on (TB_FX_WIFI_DONE) arms the linger (AP_LINGER_US, so
 * the page can read the result); its J_STOP_AP sets the radio to station mode and counts the network closed only once
 * esp_wifi_get_mode() has no access point. A close that fails is tried again (the mode change after 1 and 2 s, then a
 * restart of the radio in station mode; net_ap_close_retry_ms), a J_STOP_AP the full queue dropped is queued again
 * after 1 s, and net_wifi_follow() (the app task, every loop) finishes setup if core left its setup screens without
 * net hearing it, and once a second closes a setup network that is up outside setup with no close on its way. A save
 * of the network that worked is tried again too (net_creds_save_retry_ms), since without it the next start would open
 * the setup network again. The rules are in net_util.c, where the host tests reach them.
 *
 * Threads: the Wi-Fi and IP event handlers run in the default event loop's task; net_port.h's functions run on the
 * app task; timers run in the esp_timer task. Shared state is behind s_lock, and anything slow or that writes flash
 * (saving credentials, starting mDNS and SNTP, handing a calendar address over, scans) runs on a small worker task.
 *
 * Credentials live in NVS namespace "wifi" (ssid, user, pass, sec), saved only once a join worked. NVS encryption is
 * off until the user agrees (ARCHITECTURE.md "Secrets"), so the Wi-Fi password is stored in plain NVS, as ESP-IDF's
 * own Wi-Fi storage would.
 *
 * Skip is remembered (key "skipped" in the same namespace): decisions.md says Skip uses the bar offline, so the next
 * start stays offline with the radio off, instead of opening the open MiniBar-Setup network again. Set up (the QR
 * code) and a join that works clear it.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cal_sync.h"
#include "dhcpserver/dhcpserver.h"
#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "net_api.h"
#include "net_internal.h"
#include "net_nets.h"
#include "net_util.h"
#include "tb_bus.h"
#include "tb_text.h"

static const char *TAG = "net.wifi";

#define SETUP_SSID        "MiniBar-Setup"
#define SCAN_MAX          20
#define JOIN_TIMEOUT_US   (30 * 1000000LL)  /* a join that hasn't worked in 30 s has failed */
#define NO_ADDRESS_US     (15 * 1000000LL)  /* joined but no address in 15 s: often a sign-in-page network */
#define AP_LINGER_US      (15 * 1000000LL)  /* the setup network stays up a little after Connected, for the page */
#define AP_GUARD_MS       1000              /* net_wifi_follow() looks for a setup network up outside setup this often */
#define REQUEUE_US        (1 * 1000000LL)   /* a close or a save the full job queue dropped is queued again after 1 s */
#define SCAN_STALE_US     (15 * 1000000LL)  /* with no phone on the setup network, an older list is scanned again */
#define SCAN_EMPTY_US     (30 * 1000000LL)  /* with a phone on it, only an empty list, at most this often */
#define SCAN_WAIT_MS      5000              /* the first scan takes 1 to 2 s */
#define AP_LOG_PER_MIN    40
#define JOIN_TRIES        3
#define RSSI_EVERY_US     (5 * 1000000LL)

/* The ESP-IDF reason codes net_util.c maps (it can't include esp_wifi_types.h on the host). */
_Static_assert(NET_REASON_4WAY_HANDSHAKE_TIMEOUT == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT, "reason");
_Static_assert(NET_REASON_802_1X_AUTH_FAILED == WIFI_REASON_802_1X_AUTH_FAILED, "reason");
_Static_assert(NET_REASON_NO_AP_FOUND == WIFI_REASON_NO_AP_FOUND, "reason");
_Static_assert(NET_REASON_AUTH_FAIL == WIFI_REASON_AUTH_FAIL, "reason");
_Static_assert(NET_REASON_HANDSHAKE_TIMEOUT == WIFI_REASON_HANDSHAKE_TIMEOUT, "reason");
_Static_assert(NET_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD, "reason");
_Static_assert(NET_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY, "reason");
_Static_assert(NET_REASON_BEACON_TIMEOUT == WIFI_REASON_BEACON_TIMEOUT, "reason");

typedef enum { M_OFF = 0, M_STA, M_SETUP } wmode_t;

typedef struct {
    char ssid[33];
    char user[129];
    char pass[129];
    uint8_t sec;                /* net_security_t */
} creds_t;

typedef enum { J_SAVE_CREDS = 1, J_ONLINE, J_SCAN, J_STOP_AP, J_CONNECT, J_SAVE_SKIP, J_CLEAR_SKIP, J_SETUP_OPEN } job_t;

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_radio;       /* esp_wifi_start/stop/set_mode come one sequence at a time (worker, callers) */
static SemaphoreHandle_t s_scan_done;   /* given by WIFI_EVENT_SCAN_DONE, for setup_open()'s first scan */
static QueueHandle_t s_jobs;
static esp_netif_t *s_sta, *s_ap;
static bool s_inited, s_running, s_creds_loaded, s_have_creds;
static bool s_ap_want;          /* the setup network should be up (the setup screens, and the linger after Connected);
                                 * J_STOP_AP clears it once the driver has no access point */
static bool s_ap_open;          /* its access point is up: WIFI_EVENT_AP_START, until AP_STOP (or a close that checked) */
static bool s_close_queued;     /* J_STOP_AP is queued or running (at most one at a time) */
static int s_close_failures;    /* closes that failed in a row (net_ap_close_retry_ms) */
static int s_save_failures;     /* saves of the network that failed in a row (net_creds_save_retry_ms) */
static int s_ap_clients;        /* phones on it */
static net_log_quota_t s_ap_quota;      /* the event task's setup network lines */
static bool s_skipped;          /* Skip was the last Wi-Fi choice (saved as "skipped") */
static creds_t s_creds;         /* the network the station is set to: the last one tried or joined */
static net_nets_t s_nets;       /* the saved networks (up to 5); s_have_creds is s_nets.count > 0 */
static int s_try_pos;           /* where in the order of last use the next attempt starts (net_nets_try_next) */
static bool s_nets_unsaved;     /* the list is only in RAM: a migration whose write failed (saved after the first start) */
static wmode_t s_mode;
static bool s_sta_up;
static char s_ip[TB_IP_BYTES], s_ssid[TB_SSID_BYTES];
static int8_t s_rssi;
static int64_t s_rssi_at;
static int s_retry;
static volatile bool s_we_left;     /* on_addr_timer disconnected the station itself */
static esp_timer_handle_t s_retry_timer, s_join_timer, s_addr_timer, s_linger_timer, s_save_timer;
static int64_t s_next_guard_ms;  /* net_wifi_follow()'s next look for a stray setup network (the app task's) */

/* the join started by the setup page */
static bool s_joining;
static creds_t s_join;
static int s_join_tries;
static net_join_status_t s_js;
static char *s_pending_cal;     /* the setup page's calendar address, handed over once online */

/* the scan the setup page shows */
static net_scan_entry_t s_scan[SCAN_MAX];
static int s_scan_n;
static int64_t s_scan_at;
static bool s_scanning;

/* mDNS and time */
static bool s_mdns_on, s_sntp_on;
static char s_host[64] = "minibar.local";
static net_time_source_t s_time_src = NET_TIME_NONE;
static int64_t s_last_ntp_ms = -1;

#define LOCK() xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)
/* Taken before s_lock when both are, never the other way round. */
#define RADIO_LOCK() xSemaphoreTake(s_radio, portMAX_DELAY)
#define RADIO_UNLOCK() xSemaphoreGive(s_radio)

/* One more line about the setup network's phones (the event task's): at most AP_LOG_PER_MIN a minute. */
static bool ap_log_ok(void)
{
    int dropped;
    bool ok = net_log_quota_take(&s_ap_quota, esp_timer_get_time() / 1000, AP_LOG_PER_MIN, &dropped);
    if (dropped) ESP_LOGI(TAG, "(%d more setup network events in the last minute weren't logged)", dropped);
    return ok;
}

/* The setup page's calendar address is a secret: zeroed before it's freed. The caller holds s_lock. */
static void drop_pending_cal_locked(void)
{
    if (!s_pending_cal) return;
    memset(s_pending_cal, 0, strlen(s_pending_cal));
    free(s_pending_cal);
    s_pending_cal = NULL;
}

static bool job(job_t j)
{
    if (s_jobs && xQueueSend(s_jobs, &j, 0) == pdTRUE) return true;
    ESP_LOGE(TAG, "the Wi-Fi worker's queue is full: job %d dropped", (int)j);
    return false;
}

static void post_wifi(tb_wifi_ev_t what, const char *ssid, const char *ip, const char *host, const char *error)
{
    tb_event_t ev = {.kind = TB_EV_WIFI};
    ev.u.wifi.ev = what;
    tb_strlcpy(ev.u.wifi.ssid, ssid ? ssid : "", sizeof ev.u.wifi.ssid);
    tb_strlcpy(ev.u.wifi.ip, ip ? ip : "", sizeof ev.u.wifi.ip);
    tb_strlcpy(ev.u.wifi.host, host ? host : "", sizeof ev.u.wifi.host);
    tb_strlcpy(ev.u.wifi.error, error ? error : "", sizeof ev.u.wifi.error);
    if (!tb_bus_post(&ev)) ESP_LOGW(TAG, "bus full: Wi-Fi event %d dropped", (int)what);
}

/* J_SAVE_CREDS in us (on_save_timer), for a save that failed or a job the full queue dropped. */
static void save_later(int64_t us)
{
    esp_timer_stop(s_save_timer);
    esp_timer_start_once(s_save_timer, (uint64_t)us);
}

/* ======================================================================================================== */
/* Credentials                                                                                              */
/* ======================================================================================================== */

/* The list as bytes for NVS. Used by the worker and, before it exists, by creds_load(): never on a task stack (1.5 KB
 * at most), and zeroed after every use: it holds passwords. */
static uint8_t s_blob[NET_NETS_BLOB_MAX];

static void creds_from_saved(creds_t *c, const net_saved_t *n)
{
    memset(c, 0, sizeof *c);
    tb_strlcpy(c->ssid, n->ssid, sizeof c->ssid);
    tb_strlcpy(c->user, n->user, sizeof c->user);
    tb_strlcpy(c->pass, n->pass, sizeof c->pass);
    c->sec = n->sec;
}

/* The single network of firmware before 1.0.5 (keys ssid, user, pass, sec in "wifi"). False if there isn't one. */
static bool legacy_read(nvs_handle_t h, net_nets_t *out)
{
    char ssid[33], user[129] = "", pass[129] = "";
    size_t n = sizeof ssid;
    if (nvs_get_str(h, "ssid", ssid, &n) != ESP_OK || !ssid[0]) return false;
    n = sizeof user;
    if (nvs_get_str(h, "user", user, &n) != ESP_OK) user[0] = '\0';
    n = sizeof pass;
    if (nvs_get_str(h, "pass", pass, &n) != ESP_OK) pass[0] = '\0';
    uint8_t sec;
    bool ok = net_nets_from_legacy(out, ssid, user, pass, nvs_get_u8(h, "sec", &sec) == ESP_OK ? sec : -1);
    memset(pass, 0, sizeof pass);
    return ok;
}

static esp_err_t legacy_erase(nvs_handle_t h)
{
    static const char *const keys[] = {"ssid", "user", "pass", "sec"};
    esp_err_t err = ESP_OK;
    for (int i = 0; i < 4; i++) {
        esp_err_t e = nvs_erase_key(h, keys[i]);
        if (e != ESP_OK && e != ESP_ERR_NVS_NOT_FOUND) err = e;
    }
    return err == ESP_OK ? nvs_commit(h) : err;
}

/* Reads the saved networks, and moves the single network of firmware before 1.0.5 into the list: write the list, read
 * it back, and only then erase the old keys, so a power cut at any step just repeats the move at the next start
 * (the list wins once it's there). */
static void creds_load(void)
{
    if (s_creds_loaded) return;
    s_creds_loaded = true;
    net_nets_init(&s_nets);
    nvs_handle_t h;
    bool rw = true;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) {
        rw = false;
        if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) return;
    }
    uint8_t skipped = 0;
    s_skipped = nvs_get_u8(h, "skipped", &skipped) == ESP_OK && skipped;
    size_t n = sizeof s_blob;
    esp_err_t e = nvs_get_blob(h, "nets", s_blob, &n);
    if (e == ESP_OK && net_nets_decode(&s_nets, s_blob, n)) {
        if (rw) legacy_erase(h);       /* a cut between the list's write and the old keys' erase */
    } else if (e != ESP_ERR_NVS_NOT_FOUND) {
        /* unreadable list: not erased, not overwritten; an old single network still works from RAM */
        ESP_LOGE(TAG, "the saved Wi-Fi list isn't readable (%s): ignored, not erased", esp_err_to_name(e));
        net_nets_init(&s_nets);
        legacy_read(h, &s_nets);
    } else if (legacy_read(h, &s_nets)) {
        n = net_nets_encode(&s_nets, s_blob, sizeof s_blob);
        static uint8_t back[NET_NETS_BLOB_MAX];
        size_t bn = sizeof back;
        bool ok = rw && n && nvs_set_blob(h, "nets", s_blob, n) == ESP_OK && nvs_commit(h) == ESP_OK &&
                  nvs_get_blob(h, "nets", back, &bn) == ESP_OK && bn == n && !memcmp(back, s_blob, n);
        memset(back, 0, sizeof back);
        if (ok && legacy_erase(h) == ESP_OK) {
            ESP_LOGI(TAG, "moved the saved Wi-Fi network \"%s\" into the list of %d", s_nets.n[0].ssid, NET_NETS_MAX);
        } else if (!ok) {
            ESP_LOGE(TAG, "moving the saved Wi-Fi network into the list failed: using it from RAM, trying again later");
            s_nets_unsaved = true;      /* the old keys stay: nothing is lost */
        }
    }
    memset(s_blob, 0, sizeof s_blob);
    nvs_close(h);
    int order[NET_NETS_MAX];
    if (net_nets_order(&s_nets, order)) {
        creds_from_saved(&s_creds, &s_nets.n[order[0]]);
        s_have_creds = true;
    }
}

/* Writes the list in s_blob (the worker's). clear_skip: a join that worked ends "offline", unless Skip came after it
 * (a later J_SAVE_SKIP has the last word). NVS skips a write whose bytes haven't changed. */
static esp_err_t creds_save(size_t len, bool clear_skip)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "nets", s_blob, len);
    if (err == ESP_OK && clear_skip) {
        esp_err_t e2 = nvs_erase_key(h, "skipped");
        if (e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) err = e2;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* Remember Skip (true), or forget it (false). Runs on the worker: it writes flash. */
static void skip_save(bool skipped)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = skipped ? nvs_set_u8(h, "skipped", 1) : nvs_erase_key(h, "skipped");
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "saving the Wi-Fi skip failed: %s", esp_err_to_name(err));
}

bool net_wifi_have_creds(void)
{
    creds_load();
    return s_have_creds;
}

tb_wifi_mode_t net_wifi_start_mode(void)
{
    creds_load();
    return s_skipped ? TB_WIFI_OFFLINE : s_have_creds ? TB_WIFI_OK : TB_WIFI_SETUP;
}

/* Point the station at a network (personal, open, or work login). */
static void sta_apply(const creds_t *c)
{
    wifi_config_t wc;
    memset(&wc, 0, sizeof wc);
    memcpy(wc.sta.ssid, c->ssid, strnlen(c->ssid, sizeof wc.sta.ssid));
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    if (c->sec == NET_SEC_PASSWORD) {
        memcpy(wc.sta.password, c->pass, strnlen(c->pass, sizeof wc.sta.password));
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;     /* WPA2 or WPA3 Personal */
    } else if (c->sec == NET_SEC_WORK_LOGIN) {
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_ENTERPRISE;
    } else {
        wc.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (c->sec == NET_SEC_WORK_LOGIN) {
        /* PEAP or TTLS with MSCHAPv2: the supplicant negotiates which. The server's certificate isn't checked:
         * office RADIUS servers mostly use a private CA the bar can't know (see the README's open questions). */
        esp_eap_client_set_identity((const unsigned char *)c->user, (int)strlen(c->user));
        esp_eap_client_set_username((const unsigned char *)c->user, (int)strlen(c->user));
        esp_eap_client_set_password((const unsigned char *)c->pass, (int)strlen(c->pass));
        esp_eap_client_set_ttls_phase2_method(ESP_EAP_TTLS_PHASE2_MSCHAPV2);
        esp_wifi_sta_enterprise_enable();
    } else {
        esp_wifi_sta_enterprise_disable();
        esp_eap_client_clear_identity();
        esp_eap_client_clear_username();
        esp_eap_client_clear_password();
    }
}

/* ======================================================================================================== */
/* Events                                                                                                   */
/* ======================================================================================================== */

static void join_failed(net_join_err_t e)
{
    char ssid[TB_SSID_BYTES];
    LOCK();
    if (!s_joining) {
        UNLOCK();
        return;
    }
    s_joining = false;
    s_js.state = NET_JOIN_FAILED;
    s_js.error = net_join_err_code(e);
    s_js.message = net_join_err_message(e);
    tb_strlcpy(ssid, s_join.ssid, sizeof ssid);
    memset(s_join.pass, 0, sizeof s_join.pass);
    drop_pending_cal_locked();
    UNLOCK();
    esp_timer_stop(s_join_timer);
    esp_timer_stop(s_addr_timer);
    esp_wifi_disconnect();
    ESP_LOGW(TAG, "couldn't join \"%s\": %s", ssid, net_join_err_code(e));
    post_wifi(TB_WIFI_EV_FAILED, ssid, NULL, NULL, net_join_err_screen(e));
    /* back to the saved network's settings (a failed "Set up again" keeps the old one for the next start) */
    if (s_have_creds) sta_apply(&s_creds);
}

static void reconnect_later(int min_s)
{
    static const int delays_s[] = {1, 2, 5, 10, 30};
    int d = delays_s[s_retry < 4 ? s_retry : 4];
    if (d < min_s) d = min_s;
    if (s_retry < 100) s_retry++;
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)d * 1000000ULL);
}

/* An attempt at a saved network failed (no signal, refused, no address): the next saved network at once, and the
 * backoff only after a whole round. With a phone on the setup network that's lingering, the station stays on its
 * network, as the setup network's channel follows it. */
static void try_next_network(void)
{
    LOCK();
    bool hold = s_ap_open && s_ap_clients > 0;
    bool round_done = hold || net_nets_try_next(s_nets.count, &s_try_pos);
    UNLOCK();
    /* a round over 3 or more networks is ~2 s of scanning each: wait longer so the radio isn't busy a quarter of the time */
    if (round_done) reconnect_later(!hold && s_nets.count >= 3 ? 60 : 0);
    else job(J_CONNECT);
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    switch (id) {
    case WIFI_EVENT_STA_START: {
        LOCK();
        bool go = s_mode == M_STA && s_have_creds && !s_joining;   /* in setup the station waits for the page */
        UNLOCK();
        if (go) esp_wifi_connect();
        break;
    }
    case WIFI_EVENT_STA_CONNECTED: {
        LOCK();
        bool watch = s_joining || s_mode == M_STA;     /* a saved network gets its address in time too */
        UNLOCK();
        if (watch) esp_timer_start_once(s_addr_timer, NO_ADDRESS_US);
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *d = data;
        LOCK();
        bool was_up = s_sta_up;
        s_sta_up = false;
        s_ip[0] = '\0';
        bool joining = s_joining;
        bool enterprise = s_join.sec == NET_SEC_WORK_LOGIN;
        wmode_t mode = s_mode;
        UNLOCK();
        esp_timer_stop(s_addr_timer);
        if (joining) {
            if (d->reason == WIFI_REASON_ASSOC_LEAVE) break;    /* we left the old network to try the new one */
            net_join_err_t e = net_join_err_from_reason(d->reason, enterprise);
            ESP_LOGI(TAG, "join attempt %d: reason %d", s_join_tries + 1, d->reason);
            if (net_join_err_retry(e) && ++s_join_tries < JOIN_TRIES) job(J_CONNECT);
            else join_failed(e);
            break;
        }
        if (was_up) {
            ESP_LOGW(TAG, "link down (reason %d)", d->reason);
            /* On the setup screens core isn't told (they show setup; net_wifi_setup_done() catches it up), but the
             * calendar is, so it doesn't sync through a link that's gone. */
            if (mode != M_SETUP) post_wifi(TB_WIFI_EV_LINK_DOWN, NULL, NULL, NULL, NULL);
            else cal_sync_set_online(false);
        } else if (s_we_left) {
            s_we_left = false;
            break;      /* we left a network that gave no address ourselves (on_addr_timer) */
        }
        /* On the setup screens the station waits for the page's choice: reconnecting to the old network could move
         * the setup network to another channel under the phone. */
        if (mode == M_STA && s_have_creds) {
            if (was_up) {       /* a link loss: the most recently used network first, after the usual 1 s */
                LOCK();
                s_try_pos = 0;
                UNLOCK();
                reconnect_later(0);
            } else {
                try_next_network();
            }
        }
        break;
    }
    case WIFI_EVENT_SCAN_DONE: {
        uint16_t n = 0;
        esp_wifi_scan_get_ap_num(&n);
        if (n > 40) n = 40;
        wifi_ap_record_t *recs = n ? calloc(n, sizeof *recs) : NULL;
        if (recs && esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) n = 0;
        net_scan_entry_t list[SCAN_MAX];
        int k = 0;
        for (int i = 0; recs && i < n; i++) {
            const char *ssid = (const char *)recs[i].ssid;
            if (!ssid[0]) continue;     /* hidden */
            net_security_t sec;
            switch (recs[i].authmode) {
            case WIFI_AUTH_OPEN:
            case WIFI_AUTH_OWE: sec = NET_SEC_OPEN; break;
            case WIFI_AUTH_WPA2_ENTERPRISE:
            case WIFI_AUTH_WPA3_ENTERPRISE:
            case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
            case WIFI_AUTH_WPA3_ENT_192: sec = NET_SEC_WORK_LOGIN; break;
            default: sec = NET_SEC_PASSWORD; break;
            }
            int j = 0;
            while (j < k && strcmp(list[j].ssid, ssid)) j++;
            if (j < k) {    /* the same network on several access points: keep the strongest */
                if (recs[i].rssi > list[j].rssi) list[j].rssi = recs[i].rssi;
                continue;
            }
            if (k == SCAN_MAX) continue;
            tb_strlcpy(list[k].ssid, ssid, sizeof list[k].ssid);
            list[k].security = sec;
            list[k].rssi = recs[i].rssi;
            k++;
        }
        free(recs);
        for (int i = 1; i < k; i++) {   /* strongest first */
            net_scan_entry_t x = list[i];
            int j = i - 1;
            while (j >= 0 && list[j].rssi < x.rssi) {
                list[j + 1] = list[j];
                j--;
            }
            list[j + 1] = x;
        }
        LOCK();
        memcpy(s_scan, list, sizeof list);
        s_scan_n = k;
        s_scan_at = esp_timer_get_time();
        s_scanning = false;
        UNLOCK();
        ESP_LOGI(TAG, "scan: %d networks", k);
        xSemaphoreGive(s_scan_done);
        break;
    }
    /* The setup network. This handler is registered for every Wi-Fi event, so it runs before esp_netif's own
     * AP_START handler (esp_event runs a base's "any id" handlers before its id handlers): the DNS socket listens on
     * every address, so it needn't wait for the interface. */
    case WIFI_EVENT_AP_START: {
        esp_netif_ip_info_t info = {0};
        esp_netif_get_ip_info(s_ap, &info);
        uint8_t ch = 0;
        wifi_second_chan_t second;
        if (esp_wifi_get_channel(&ch, &second) != ESP_OK) ch = 0;
        LOCK();
        s_ap_open = true;
        s_ap_clients = 0;
        bool want = s_ap_want;
        UNLOCK();
        char ip[16], mask[16];
        ESP_LOGI(TAG, "setup network up: \"" SETUP_SSID "\", open, channel %u, address %s mask %s", ch,
                 net_ip_str(info.ip.addr, ip), net_ip_str(info.netmask.addr, mask));
        if (want) net_dns_start(info.ip.addr, info.netmask.addr);
        break;
    }
    case WIFI_EVENT_AP_STOP:
        LOCK();
        s_ap_open = false;
        s_ap_clients = 0;
        UNLOCK();
        net_dns_stop();
        ESP_LOGI(TAG, "setup network down");
        break;
    case WIFI_EVENT_AP_STACONNECTED: {
        const wifi_event_ap_staconnected_t *e = data;
        LOCK();
        int n = ++s_ap_clients;
        UNLOCK();
        if (ap_log_ok()) ESP_LOGI(TAG, "a phone joined the setup network: " MACSTR " (%d on it)", MAC2STR(e->mac), n);
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        const wifi_event_ap_stadisconnected_t *e = data;
        LOCK();
        if (s_ap_clients > 0) s_ap_clients--;
        int n = s_ap_clients;
        UNLOCK();
        if (ap_log_ok())
            ESP_LOGI(TAG, "a phone left the setup network: " MACSTR ", reason %u (%d on it)", MAC2STR(e->mac),
                     (unsigned)e->reason, n);
        break;
    }
    default:
        break;
    }
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        char ip[TB_IP_BYTES], host[64], ssid[TB_SSID_BYTES];
        esp_ip4addr_ntoa(&e->ip_info.ip, ip, sizeof ip);
        wifi_ap_record_t ap;
        bool have_ap = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
        LOCK();
        s_sta_up = true;
        s_retry = 0;
        tb_strlcpy(s_ip, ip, sizeof s_ip);
        if (have_ap) {
            tb_strlcpy(s_ssid, (const char *)ap.ssid, sizeof s_ssid);
            s_rssi = ap.rssi;
            s_rssi_at = esp_timer_get_time();
        }
        bool joined = s_joining, touched = false;
        char replaced[33] = "";
        if (joined) {
            s_joining = false;
            s_creds = s_join;
            /* Added only now that it worked: the newest, and with 5 saved the least recently used goes. */
            net_nets_add(&s_nets, s_join.ssid, s_join.user, s_join.pass, s_join.sec, NULL, replaced);
            s_have_creds = true;
            s_try_pos = 0;
            s_save_failures = 0;        /* a new network to save: its own tries */
            s_skipped = false;          /* creds_save() erases the saved skip too */
            s_js.state = NET_JOIN_CONNECTED;
            s_js.error = s_js.message = NULL;
            tb_strlcpy(s_js.ip, ip, sizeof s_js.ip);
            tb_strlcpy(s_js.host, s_host, sizeof s_js.host);
            tb_strlcpy(s_ssid, s_join.ssid, sizeof s_ssid);
        } else if (s_have_creds) {
            /* A saved network joined again: the newest from now on (written only if that changes the order). */
            touched = net_nets_touch(&s_nets, net_nets_find(&s_nets, s_creds.ssid));
            s_try_pos = 0;
            if (touched) s_save_failures = 0;
        }
        tb_strlcpy(host, s_host, sizeof host);
        tb_strlcpy(ssid, s_ssid, sizeof ssid);
        wmode_t mode = s_mode;
        UNLOCK();
        esp_timer_stop(s_join_timer);
        esp_timer_stop(s_addr_timer);
        ESP_LOGI(TAG, "joined \"%s\", address %s", ssid, ip);
        if (replaced[0]) ESP_LOGI(TAG, "%d networks were saved: \"%s\" (used longest ago) is gone", NET_NETS_MAX, replaced);
        if (touched && !job(J_SAVE_CREDS)) save_later(REQUEUE_US);
        if (joined) {
            if (!job(J_SAVE_CREDS)) save_later(REQUEUE_US);     /* not saved, the next start would open setup */
            post_wifi(TB_WIFI_EV_CONNECTED, ssid, ip, host, NULL);
        } else if (mode != M_SETUP) {
            post_wifi(TB_WIFI_EV_LINK_UP, ssid, ip, host, NULL);
        }
        job(J_ONLINE);
    } else if (id == IP_EVENT_STA_LOST_IP) {
        LOCK();
        bool was = s_sta_up;
        s_sta_up = false;
        s_ip[0] = '\0';
        wmode_t mode = s_mode;
        UNLOCK();
        if (was && mode != M_SETUP) post_wifi(TB_WIFI_EV_LINK_DOWN, NULL, NULL, NULL, NULL);
        else if (was) cal_sync_set_online(false);      /* setup screens: as in WIFI_EVENT_STA_DISCONNECTED */
    } else if (id == IP_EVENT_AP_STAIPASSIGNED) {
        const ip_event_ap_staipassigned_t *e = data;
        char ip[16];
        if (ap_log_ok())
            ESP_LOGI(TAG, "the setup network gave " MACSTR " the address %s", MAC2STR(e->mac),
                     esp_ip4addr_ntoa(&e->ip, ip, sizeof ip));
    }
}

/* ---------- timers ---------- */

static void on_retry_timer(void *arg)
{
    (void)arg;
    job(J_CONNECT);
}

static void on_join_timer(void *arg)
{
    (void)arg;
    join_failed(NET_JOIN_NO_SIGNAL);
}

static void on_addr_timer(void *arg)
{
    (void)arg;
    LOCK();
    bool joining = s_joining, saved = s_mode == M_STA && s_have_creds && !s_sta_up;
    UNLOCK();
    if (joining) {
        join_failed(NET_JOIN_NO_ADDRESS);
    } else if (saved) {
        ESP_LOGW(TAG, "no address from \"%s\" in %d s: trying the next saved network", s_creds.ssid,
                 (int)(NO_ADDRESS_US / 1000000));
        s_we_left = true;           /* its DISCONNECTED is ignored: try_next_network moves on */
        esp_wifi_disconnect();
        try_next_network();
    }
}

/* Queue J_STOP_AP once: a request while one is queued or running adds nothing. False if the queue was full. */
static bool queue_close(void)
{
    LOCK();
    bool queued = s_close_queued;
    s_close_queued = true;
    UNLOCK();
    if (queued || job(J_STOP_AP)) return true;
    LOCK();
    s_close_queued = false;
    UNLOCK();
    return false;
}

/* The linger ran out (or a failed close's wait did): close the setup network. A full queue only delays it. */
static void on_linger_timer(void *arg)
{
    (void)arg;
    if (!queue_close()) esp_timer_start_once(s_linger_timer, REQUEUE_US);
}

static void on_save_timer(void *arg)
{
    (void)arg;
    if (!job(J_SAVE_CREDS)) esp_timer_start_once(s_save_timer, REQUEUE_US);
}

/* ======================================================================================================== */
/* mDNS (api.md 3) and SNTP                                                                                 */
/* ======================================================================================================== */

static void on_host_changed(const char *hostname, void *arg)
{
    (void)arg;
    LOCK();
    snprintf(s_host, sizeof s_host, "%s.local", hostname);
    UNLOCK();
    ESP_LOGW(TAG, "mDNS name taken; now %s.local", hostname);
}

static void mdns_txt(mdns_txt_item_t txt[5])
{
    txt[0] = (mdns_txt_item_t){"api", NET_API_VERSION};
    txt[1] = (mdns_txt_item_t){"id", net_device_id()};
    txt[2] = (mdns_txt_item_t){"fw", net_port_fw_version()};
    txt[3] = (mdns_txt_item_t){"path", "/api/v1"};
    txt[4] = (mdns_txt_item_t){"auth", net_api_auth_bearer() ? "bearer" : "none"};
}

static void mdns_start(void)
{
    if (s_mdns_on) return;
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS: %s", esp_err_to_name(err));
        return;
    }
    mdns_hostname_set("minibar");
    mdns_instance_name_set(net_bar_name());
    mdns_register_hostname_changed_callback(on_host_changed, NULL);
    mdns_txt_item_t txt[5];
    mdns_txt(txt);
    mdns_service_add(net_bar_name(), "_minibar", "_tcp", 80, txt, 5);
    mdns_txt_item_t page[1] = {{"path", "/"}};
    mdns_service_add(net_bar_name(), "_http", "_tcp", 80, page, 1);
    char got[MDNS_NAME_BUF_LEN];
    if (mdns_hostname_get(got) == ESP_OK) on_host_changed(got, NULL);
    s_mdns_on = true;
}

void net_mdns_set_name(const char *name)
{
    if (!s_mdns_on) return;
    mdns_instance_name_set(name);
    mdns_service_instance_name_set("_minibar", "_tcp", name);
    mdns_service_instance_name_set("_http", "_tcp", name);
}

static void on_ntp(struct timeval *tv)
{
    (void)tv;
    LOCK();
    s_time_src = NET_TIME_NTP;
    s_last_ntp_ms = esp_timer_get_time() / 1000;
    UNLOCK();
    tb_event_t ev = {.kind = TB_EV_TIME_SET};
    ev.u.time.source = 1;
    tb_bus_post(&ev);
}

static void sntp_start(void)
{
    if (s_sntp_on) return;
#if CONFIG_LWIP_SNTP_MAX_SERVERS > 1
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.google.com"));
#else
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
#endif
#if CONFIG_LWIP_DHCP_GET_NTP_SRV
    /* Office networks often block public NTP and hand out their own server. */
    c.server_from_dhcp = true;
    c.renew_servers_after_new_IP = true;
    c.index_of_first_server = 1;
    c.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
#endif
    c.wait_for_sync = false;
    c.sync_cb = on_ntp;
    if (esp_netif_sntp_init(&c) == ESP_OK) s_sntp_on = true;
}

/* ======================================================================================================== */
/* The worker                                                                                               */
/* ======================================================================================================== */

/* Start a scan in the background (WIFI_EVENT_SCAN_DONE fills the list). Returns false if none started. */
static bool scan_start(void)
{
    LOCK();
    bool busy = s_scanning || s_joining || !s_running;
    if (!busy) s_scanning = true;
    UNLOCK();
    if (busy) return false;
    wifi_scan_config_t sc = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE};
    sc.scan_time.active.min = 60;
    sc.scan_time.active.max = 120;
    esp_err_t err = esp_wifi_scan_start(&sc, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan didn't start: %s", esp_err_to_name(err));
        LOCK();
        s_scanning = false;
        UNLOCK();
        return false;
    }
    return true;
}

/* setup_open() goes on. If setup is over before its network opened (a USB join and Connected during the first scan),
 * nothing will open it now, so "wanted" goes too, under the same lock: a Set up again after this opens it afresh
 * (setup_begin() queues J_SETUP_OPEN), and one before it is seen here and the network opens for it. */
static bool setup_wanted(void)
{
    LOCK();
    bool want = s_ap_want && s_mode == M_SETUP;
    if (s_mode != M_SETUP) s_ap_want = false;
    UNLOCK();
    return want;
}

/* Set up again (hold, Wi-Fi) tries again: the caller's setup_wanted() is false from here on. */
static void ap_give_up(void)
{
    LOCK();
    s_ap_want = false;
    UNLOCK();
}

/* Open MiniBar-Setup's access point. The caller holds the radio lock. */
static void ap_open(void)
{
    /* The DHCP server, changed while it's stopped (as ESP-IDF's examples do): the setup address, and the bar as the
     * DNS server, set explicitly rather than through CONFIG_LWIP_DHCPS_ADD_DNS (which IDF v6 removes). No
     * captive-portal option 114. The server itself starts with the access point. */
    esp_netif_ip_info_t info = {0};
    info.ip.addr = info.gw.addr = esp_ip4addr_aton(NET_SETUP_IP);
    info.netmask.addr = esp_ip4addr_aton(NET_SETUP_NETMASK);
    esp_err_t err = esp_netif_dhcps_stop(s_ap);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
        ESP_LOGW(TAG, "setup network: stopping its DHCP server: %s", esp_err_to_name(err));
    err = esp_netif_set_ip_info(s_ap, &info);
    if (err != ESP_OK) {
        /* At any other address every request would get 421 (net_http.c knows the setup network by NET_SETUP_IP), so
         * the network stays closed rather than opening as a dead end. */
        ESP_LOGE(TAG, "setup network: its address " NET_SETUP_IP ": %s; not opening it", esp_err_to_name(err));
        ap_give_up();
        return;
    }
    dhcps_offer_t dns_on = OFFER_DNS;
    err = esp_netif_dhcps_option(s_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dns_on, sizeof dns_on);
    if (err != ESP_OK) ESP_LOGE(TAG, "setup network: offering a DNS server: %s", esp_err_to_name(err));
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4 = info.ip;
    err = esp_netif_set_dns_info(s_ap, ESP_NETIF_DNS_MAIN, &dns);
    if (err != ESP_OK) ESP_LOGE(TAG, "setup network: the DNS server it offers: %s", esp_err_to_name(err));
    err = esp_netif_dhcps_start(s_ap);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
        ESP_LOGE(TAG, "setup network: its DHCP server: %s", esp_err_to_name(err));

    /* The access point: mode, then its settings, then start. The radio restarts for it: the scan's station-only start
     * goes, and so does a link to the office Wi-Fi during "Set up again" (on the setup screens the bar isn't on the
     * office Wi-Fi; decisions.md, Pairing, criterion 19). */
    wifi_config_t ap;
    memset(&ap, 0, sizeof ap);
    memcpy(ap.ap.ssid, SETUP_SSID, sizeof SETUP_SSID - 1);
    ap.ap.ssid_len = sizeof SETUP_SSID - 1;
    ap.ap.channel = 1;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 4;
    if (s_running) {
        LOCK();
        bool office = s_sta_up;
        UNLOCK();
        if (office) {
            /* Set up again: the office link goes with the restart. Core isn't told on the setup screens, but the
             * calendar is now, rather than syncing through a link that's gone for the whole of setup. */
            ESP_LOGI(TAG, "leaving the office Wi-Fi for the setup network");
            cal_sync_set_online(false);
        }
        esp_wifi_stop();
        s_running = false;
    }
    /* Logged before the start: WIFI_EVENT_AP_START ("setup network up") often runs before esp_wifi_start() returns. */
    ESP_LOGI(TAG, "opening the setup network");
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) {
        s_running = true;
        return;
    }
    ESP_LOGE(TAG, "the setup network didn't open: %s", esp_err_to_name(err));
    ap_give_up();
}

/* J_SETUP_OPEN: one scan with the access point still closed, then open it. */
static void setup_open(void)
{
    if (!setup_wanted()) return;
    RADIO_LOCK();
    /* Again under the radio lock: a Skip that came in between has already turned the radio off, and mustn't find it
     * back on in station mode while the bar is offline. A Skip after this point stops what this starts. */
    if (!setup_wanted()) {
        RADIO_UNLOCK();
        return;
    }
    if (!s_running) {
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK) err = esp_wifi_start();
        if (err == ESP_OK) s_running = true;
        else ESP_LOGE(TAG, "the radio didn't start for the scan: %s", esp_err_to_name(err));
    }
    RADIO_UNLOCK();
    LOCK();
    s_scanning = false;     /* a scan the radio stopped under never says it's done */
    UNLOCK();
    xSemaphoreTake(s_scan_done, 0);
    if (scan_start() && xSemaphoreTake(s_scan_done, pdMS_TO_TICKS(SCAN_WAIT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "the scan didn't finish in %d s; opening the setup network anyway", SCAN_WAIT_MS / 1000);
        esp_wifi_scan_stop();
        LOCK();
        s_scanning = false;
        UNLOCK();
    }
    RADIO_LOCK();
    if (setup_wanted()) ap_open();
    RADIO_UNLOCK();
}

/* J_SAVE_CREDS: the list of saved networks (a network that just worked, or a changed order), so the next start joins it. Without it the next start would open the
 * setup network again, so a save that fails is tried again (net_creds_save_retry_ms: 6 tries over about 43 minutes;
 * a write that fails before NVS writes anything wears nothing). */
static void save_creds(void)
{
    LOCK();
    size_t len = net_nets_encode(&s_nets, s_blob, sizeof s_blob);
    bool have = s_nets.count > 0, clear_skip = !s_skipped;
    UNLOCK();
    if (!have || !len) return;
    esp_err_t err = creds_save(len, clear_skip);
    memset(s_blob, 0, sizeof s_blob);
    LOCK();
    int before = s_save_failures;
    s_save_failures = err == ESP_OK ? 0 : before + 1;
    int failures = s_save_failures;
    UNLOCK();
    if (err == ESP_OK) {
        s_nets_unsaved = false;
        if (before) ESP_LOGI(TAG, "the Wi-Fi network is saved now (try %d)", before + 1);
        return;
    }
    int32_t ms = net_creds_save_retry_ms(failures);
    if (ms) {
        ESP_LOGE(TAG, "saving the Wi-Fi network failed (%s): trying again in %d s", esp_err_to_name(err), (int)(ms / 1000));
        save_later((int64_t)ms * 1000);
    } else {
        ESP_LOGE(TAG, "saving the Wi-Fi network failed %d times (%s): MiniBar stays on it, but after a restart it "
                      "shows the QR code and opens its setup network again", failures, esp_err_to_name(err));
    }
}

/* The driver has an access point up: started, in AP or APSTA mode. The caller holds the radio lock. */
static bool driver_ap_on(void)
{
    wifi_mode_t m = WIFI_MODE_NULL;
    return s_running && (esp_wifi_get_mode(&m) != ESP_OK || (m & WIFI_MODE_AP));
}

/* J_STOP_AP: close the setup network, unless setup has begun again (that setup's own Connected closes it). It's
 * closed once the driver's own mode has no access point (or the radio is off); until then it's tried again
 * (net_ap_close_retry_ms): changing the mode twice more, then restarting the radio in station mode, which drops the
 * office link for about a second (STA_START joins it again). Before 1.0.4 a failed esp_wifi_set_mode() was ignored and
 * the network stayed up until a restart. */
static void ap_close(void)
{
    RADIO_LOCK();
    LOCK();
    bool in_setup = s_mode == M_SETUP;
    wmode_t mode = s_mode;
    int failures = s_close_failures;
    UNLOCK();
    esp_err_t err = ESP_OK;
    bool closed = false;        /* this call took the access point down */
    if (!in_setup) {
        net_dns_stop();
        if (driver_ap_on()) {
            bool restart = false;
            if (failures) net_ap_close_retry_ms(failures, &restart);
            if (mode == M_OFF) {
                err = esp_wifi_stop();      /* offline: the radio goes off, as Skip does */
                if (err == ESP_OK) s_running = false;
            } else if (restart) {
                ESP_LOGW(TAG, "restarting the radio in station mode to close the setup network");
                err = esp_wifi_stop();
                if (err == ESP_OK) {
                    s_running = false;      /* the access point is gone with it, whatever follows */
                    esp_err_t e2 = esp_wifi_set_mode(WIFI_MODE_STA);
                    if (e2 == ESP_OK) e2 = esp_wifi_start();
                    if (e2 == ESP_OK) s_running = true;
                    else ESP_LOGE(TAG, "the radio didn't start again: %s (offline until MiniBar restarts)", esp_err_to_name(e2));
                }
            } else {
                err = esp_wifi_set_mode(WIFI_MODE_STA);
            }
            /* The driver's word, not set_mode's: a mode change that fails inside the driver puts the old mode back. */
            if (err == ESP_OK && driver_ap_on()) err = ESP_ERR_INVALID_STATE;
            closed = err == ESP_OK;
        }
    }
    LOCK();
    /* Set up again (the app task) may have come in meanwhile: the network is then that setup's, and this close is
     * void. Checked here, under the lock its setup_begin() takes, so neither misses the other. */
    bool began = s_mode == M_SETUP;
    if (!began && err == ESP_OK) {
        s_ap_want = false;
        s_ap_open = false;      /* AP_STOP says so too; this covers one that never comes */
        s_close_failures = 0;
    } else if (!began) {
        failures = ++s_close_failures;
    }
    UNLOCK();
    if (began) {
        if (closed) {           /* it took the new setup's network down: open it again */
            ESP_LOGI(TAG, "setup began again while the setup network was closing: opening it again");
            job(J_SETUP_OPEN);
        }
    } else if (err == ESP_OK) {
        if (closed) ESP_LOGI(TAG, "setup network closed");
    } else {
        bool restart;
        int32_t ms = net_ap_close_retry_ms(failures, &restart);
        ESP_LOGE(TAG, "closing the setup network failed (%s, try %d): trying again in %d s%s", esp_err_to_name(err),
                 failures, (int)(ms / 1000), restart ? " with a restart of the radio" : "");
        esp_timer_stop(s_linger_timer);
        esp_timer_start_once(s_linger_timer, (uint64_t)ms * 1000);
    }
    LOCK();
    s_close_queued = false;     /* only now: until here a close was on its way (the retry's wait is armed by now) */
    UNLOCK();
    RADIO_UNLOCK();
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        job_t j;
        if (xQueueReceive(s_jobs, &j, portMAX_DELAY) != pdTRUE) continue;
        switch (j) {
        case J_SAVE_CREDS: save_creds(); break;
        case J_ONLINE: {
            mdns_start();
            sntp_start();
            char *cal;
            LOCK();
            cal = s_pending_cal;
            s_pending_cal = NULL;
            UNLOCK();
            if (cal) {
                /* The setup page's address is checked once the bar is online (api.md 13.2). */
                cal_url_err_t fe = CAL_URL_OK;
                cal_sync_set_online(true);
                if (cal_sync_put(cal, true, &fe) != ESP_OK) {
                    /* The check never started (bad format or no memory): the bar still owes the setup page's
                     * address an answer, the same one a failed check gives. */
                    ESP_LOGW(TAG, "setup's calendar address refused (%d)", (int)fe);
                    tb_bus_post_kind(TB_EV_CAL_EVENT, TB_CALEV_SETUP_FAILED);
                }
                memset(cal, 0, strlen(cal));
                free(cal);
            }
            break;
        }
        case J_SCAN:
            if (scan_start()) ESP_LOGI(TAG, "scanning again (no phone on the setup network, or nothing to show)");
            break;
        case J_SETUP_OPEN: setup_open(); break;
        case J_SAVE_SKIP: skip_save(true); break;
        case J_CLEAR_SKIP: skip_save(false); break;
        case J_STOP_AP: ap_close(); break;
        case J_CONNECT: {
            static creds_t c;
            LOCK();
            /* On the setup screens only the page's join connects: a reconnect to the saved network would move the
             * setup network to that network's channel under the phone. */
            bool joining = s_joining, go = s_mode != M_OFF && (s_joining || (s_mode == M_STA && s_have_creds));
            if (joining) {
                c = s_join;
            } else if (go) {        /* the saved network at this place in the order of last use */
                int order[NET_NETS_MAX];
                int n = net_nets_order(&s_nets, order);
                if (s_try_pos >= n) s_try_pos = 0;
                if (n) {
                    creds_from_saved(&c, &s_nets.n[order[s_try_pos]]);
                    s_creds = c;
                    if (n > 1) ESP_LOGI(TAG, "trying the saved network \"%s\" (%d of %d)", c.ssid, s_try_pos + 1, n);
                } else {
                    go = false;
                }
            }
            UNLOCK();
            if (go) {
                sta_apply(&c);
                esp_wifi_connect();
            }
            memset(&c, 0, sizeof c);
            break;
        }
        }
    }
}

/* ======================================================================================================== */
/* Life cycle                                                                                               */
/* ======================================================================================================== */

esp_err_t net_wifi_init(void)
{
    if (s_inited) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    s_radio = xSemaphoreCreateMutex();
    s_scan_done = xSemaphoreCreateBinary();
    s_jobs = xQueueCreate(8, sizeof(job_t));
    if (!s_lock || !s_radio || !s_scan_done || !s_jobs) return ESP_ERR_NO_MEM;
    creds_load();
    /* The clock: the RTC set it at boot if it held a time MiniBar wrote (board_rtc_init runs before net_init). */
    if (time(NULL) > 1735689600) s_time_src = NET_TIME_RTC;     /* after 2025-01-01 */

    s_sta = esp_netif_create_default_wifi_sta();
    s_ap = esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(s_sta, "minibar");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);     /* the credentials are ours (NVS "wifi"), not the driver's */
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_ip, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, on_ip, NULL);

    const esp_timer_create_args_t t1 = {.callback = on_retry_timer, .name = "wifi_retry"};
    const esp_timer_create_args_t t2 = {.callback = on_join_timer, .name = "wifi_join"};
    const esp_timer_create_args_t t3 = {.callback = on_addr_timer, .name = "wifi_addr"};
    const esp_timer_create_args_t t4 = {.callback = on_linger_timer, .name = "wifi_linger"};
    const esp_timer_create_args_t t5 = {.callback = on_save_timer, .name = "wifi_save"};
    esp_timer_create(&t1, &s_retry_timer);
    esp_timer_create(&t2, &s_join_timer);
    esp_timer_create(&t3, &s_addr_timer);
    esp_timer_create(&t4, &s_linger_timer);
    esp_timer_create(&t5, &s_save_timer);
    if (s_nets_unsaved) save_later(REQUEUE_US);     /* a move into the list that couldn't be written at start-up */
    /* writes NVS: the stack stays in internal RAM */
    if (xTaskCreatePinnedToCore(worker, "net", 4096, NULL, 4, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    s_inited = true;
    return ESP_OK;
}

static void wifi_run(wifi_mode_t mode)
{
    RADIO_LOCK();
    esp_wifi_set_mode(mode);
    if (!s_running) {
        esp_err_t err = esp_wifi_start();
        if (err == ESP_OK) s_running = true;
        else ESP_LOGE(TAG, "the radio didn't start: %s", esp_err_to_name(err));
    }
    RADIO_UNLOCK();
}

void net_wifi_start(void)
{
    if (s_skipped) {
        /* Skip was the last choice: stay offline with the radio off (hold, Wi-Fi, Set up starts setup again). */
        LOCK();
        s_mode = M_OFF;
        UNLOCK();
        ESP_LOGI(TAG, "Wi-Fi was skipped: staying offline");
        return;
    }
    if (s_have_creds) {
        LOCK();
        s_mode = M_STA;
        UNLOCK();
        sta_apply(&s_creds);
        wifi_run(WIFI_MODE_STA);
        esp_err_t cerr = esp_wifi_connect();
        ESP_LOGI(TAG, "joining the saved network (%d saved, most recent first): %s", s_nets.count, esp_err_to_name(cerr));
    } else {
        net_wifi_setup_begin();
    }
}

/* Runs on whichever task shows the QR code (main at the first start, the app task for Set up): the slow part (the
 * scan, the radio's restart) is the worker's J_SETUP_OPEN. */
void net_wifi_setup_begin(void)
{
    LOCK();
    bool was_skipped = s_skipped;
    s_skipped = false;
    s_mode = M_SETUP;
    s_joining = false;
    memset(&s_js, 0, sizeof s_js);
    drop_pending_cal_locked();
    bool already = s_ap_want;       /* up or opening (or lingering after Connected, or a close that failed) */
    s_ap_want = true;
    s_close_failures = 0;           /* a close later on starts with the mode change again */
    s_try_pos = 0;
    bool open = s_ap_open, idle = s_ap_open && s_ap_clients == 0;
    UNLOCK();
    if (was_skipped) job(J_CLEAR_SKIP);
    esp_timer_stop(s_linger_timer);
    esp_timer_stop(s_join_timer);
    esp_timer_stop(s_retry_timer);      /* no reconnecting to the saved network under the setup network */
    if (!already) {
        ESP_LOGI(TAG, "setup begins: scanning, then opening " SETUP_SSID " at " NET_SETUP_IP);
        if (!job(J_SETUP_OPEN)) {
            LOCK();
            s_ap_want = false;      /* Set up again tries again */
            UNLOCK();
        }
    } else if (open) {
        ESP_LOGI(TAG, "setup begins: " SETUP_SSID " is already up");
        if (idle) job(J_SCAN);      /* no phone on it: a fresh list costs nobody anything */
    } else {
        ESP_LOGI(TAG, "setup begins: " SETUP_SSID " is still opening");
    }
}

/* Once the Connected screen has moved on, core hears about a link that dropped while setup held it back (a drop
 * during the Connected screen), and the station reconnects as it would anywhere else. */
static void catch_up_after_setup(void)
{
    LOCK();
    bool down = s_mode == M_STA && s_have_creds && !s_sta_up && !s_joining;
    UNLOCK();
    if (!down) return;
    ESP_LOGW(TAG, "the office Wi-Fi dropped during setup: reconnecting");
    post_wifi(TB_WIFI_EV_LINK_DOWN, NULL, NULL, NULL, NULL);
    job(J_CONNECT);
}

void net_wifi_setup_skip(void)
{
    LOCK();
    bool save = !s_skipped;
    s_skipped = true;
    s_mode = M_OFF;
    s_joining = false;
    s_sta_up = false;
    s_ap_want = false;
    s_close_failures = 0;
    s_scanning = false;
    drop_pending_cal_locked();
    UNLOCK();
    if (save) job(J_SAVE_SKIP);     /* the next start stays offline too */
    esp_timer_stop(s_linger_timer);
    esp_timer_stop(s_join_timer);
    esp_timer_stop(s_retry_timer);
    esp_timer_stop(s_addr_timer);
    net_dns_stop();
    RADIO_LOCK();
    if (s_running) {
        esp_wifi_stop();    /* offline: the radio goes off */
        s_running = false;
    }
    LOCK();
    s_ap_open = false;      /* gone with the radio (AP_STOP says so too, a moment later) */
    UNLOCK();
    RADIO_UNLOCK();
    ESP_LOGI(TAG, "Wi-Fi skipped");
}

void net_wifi_setup_done(void)
{
    LOCK();
    bool was_setup = s_mode == M_SETUP;
    if (was_setup) s_mode = M_STA;
    s_close_failures = 0;
    UNLOCK();
    esp_timer_stop(s_linger_timer);
    esp_timer_start_once(s_linger_timer, AP_LINGER_US);
    if (was_setup) catch_up_after_setup();
}

/* net.h net_setup_follow(): the app task, every loop once core's effects have run. */
void net_wifi_follow(tb_wifi_mode_t core_mode, int64_t now_ms)
{
    if (!s_inited) return;
    LOCK();
    bool in_setup = s_mode == M_SETUP;
    UNLOCK();
    /* core left its setup screens but net never heard (core's effect queue was full): finish the same way */
    switch (net_setup_catch_up(core_mode, in_setup)) {
    case NET_SETUP_FINISH:
        ESP_LOGW(TAG, "setup ended on the screen but net didn't hear it: closing the setup network after its linger");
        net_wifi_setup_done();
        break;
    case NET_SETUP_SKIP:
        ESP_LOGW(TAG, "Wi-Fi was skipped on the screen but net didn't hear it: radio off");
        cal_sync_set_online(false);     /* as main does for TB_FX_WIFI_SKIP */
        net_wifi_setup_skip();
        break;
    case NET_SETUP_KEEP: break;
    }
    if (now_ms < s_next_guard_ms) return;
    s_next_guard_ms = now_ms + AP_GUARD_MS;
    bool lingering = esp_timer_is_active(s_linger_timer);     /* the linger, or a failed close's wait */
    LOCK();
    bool stray = net_setup_ap_stray(s_mode == M_SETUP, s_ap_want, s_ap_open, s_close_queued || lingering);
    UNLOCK();
    if (!stray) return;
    ESP_LOGW(TAG, "the setup network is up outside setup with no close on its way: closing it");
    queue_close();      /* a full queue: the next look tries again */
}

bool net_wifi_rf_on(void)
{
    return s_running;
}

bool net_wifi_setup_net_up(void)
{
    LOCK();
    bool up = s_ap_open;
    UNLOCK();
    return up;
}

/* ======================================================================================================== */
/* net_port.h: Wi-Fi and time                                                                               */
/* ======================================================================================================== */

void net_port_wifi(net_wifi_info_t *out)
{
    memset(out, 0, sizeof *out);
    int64_t now = esp_timer_get_time();
    LOCK();
    bool refresh = s_sta_up && now - s_rssi_at > RSSI_EVERY_US;
    UNLOCK();
    if (refresh) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            LOCK();
            s_rssi = ap.rssi;
            s_rssi_at = now;
            UNLOCK();
        }
    }
    LOCK();
    out->state = s_mode == M_SETUP ? NET_WIFI_SETUP : s_sta_up ? NET_WIFI_CONNECTED : NET_WIFI_OFFLINE;
    out->sta_up = s_sta_up;
    if (s_sta_up) {
        tb_strlcpy(out->ssid, s_ssid, sizeof out->ssid);
        tb_strlcpy(out->ip, s_ip, sizeof out->ip);
        out->rssi = s_rssi;
    }
    tb_strlcpy(out->host, s_host, sizeof out->host);
    UNLOCK();
}

int net_port_setup_networks(net_scan_entry_t *out, int max)
{
    int64_t now = esp_timer_get_time();
    LOCK();
    int n = s_scan_n < max ? s_scan_n : max;
    memcpy(out, s_scan, (size_t)n * sizeof *out);
    /* The list comes from the scan before the setup network opened. A scan now would take the radio off the setup
     * network's channel under the very phone asking, so with a phone on it only an empty list is looked for again,
     * at most every 30 s; the page polls and shows what arrives. */
    bool again = !s_scanning && !s_joining &&
                 (s_ap_clients == 0 ? now - s_scan_at > SCAN_STALE_US : s_scan_n == 0 && now - s_scan_at > SCAN_EMPTY_US);
    UNLOCK();
    if (again) job(J_SCAN);
    return n;
}

bool net_port_setup_join(const char *ssid, const char *username, const char *password, const char *calendar_url)
{
    creds_t c;
    memset(&c, 0, sizeof c);
    tb_strlcpy(c.ssid, ssid, sizeof c.ssid);
    tb_strlcpy(c.user, username ? username : "", sizeof c.user);
    tb_strlcpy(c.pass, password ? password : "", sizeof c.pass);
    c.sec = username && username[0] ? NET_SEC_WORK_LOGIN : password && password[0] ? NET_SEC_PASSWORD : NET_SEC_OPEN;
    char *cal = calendar_url && calendar_url[0] ? strdup(calendar_url) : NULL;
    LOCK();
    bool was_up = s_sta_up;
    s_join = c;
    s_joining = true;
    s_join_tries = 0;
    s_sta_up = false;
    memset(&s_js, 0, sizeof s_js);
    s_js.state = NET_JOIN_CONNECTING;
    drop_pending_cal_locked();
    s_pending_cal = cal;
    s_scanning = false;
    UNLOCK();
    esp_timer_stop(s_retry_timer);
    esp_timer_stop(s_addr_timer);
    esp_timer_stop(s_join_timer);
    esp_wifi_scan_stop();
    if (was_up) esp_wifi_disconnect();
    sta_apply(&c);
    memset(&c, 0, sizeof c);
    esp_timer_start_once(s_join_timer, JOIN_TIMEOUT_US);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGE(TAG, "connect: %s", esp_err_to_name(err));
        LOCK();
        s_joining = false;
        s_js.state = NET_JOIN_IDLE;
        UNLOCK();
        esp_timer_stop(s_join_timer);
        return false;
    }
    return true;
}

void net_port_setup_status(net_join_status_t *out)
{
    LOCK();
    *out = s_js;
    UNLOCK();
}

net_time_source_t net_time_source(void)
{
    LOCK();
    net_time_source_t t = s_time_src;
    UNLOCK();
    return t;
}

bool net_time_valid(void)
{
    return net_time_source() != NET_TIME_NONE;
}

bool net_time_set_from_mac(tb_epoch_t t)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    LOCK();
    tb_clock_t now = {.mono = esp_timer_get_time() / 1000, .wall = tv.tv_sec, .valid = s_time_src != NET_TIME_NONE};
    bool set = net_time_mac_should_set(t, &now, s_last_ntp_ms);
    if (set) s_time_src = NET_TIME_MAC;
    UNLOCK();
    if (!set) return false;
    struct timeval nt = {.tv_sec = (time_t)t, .tv_usec = 0};
    settimeofday(&nt, NULL);
    tb_event_t ev = {.kind = TB_EV_TIME_SET};
    ev.u.time.source = 3;
    tb_bus_post(&ev);
    ESP_LOGI(TAG, "clock set from the Mac");
    return true;
}
