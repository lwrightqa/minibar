/*
 * net_wifi.c: Wi-Fi for TinyBar. Owner: net builder.
 *
 * The station joins the office Wi-Fi with the saved credentials (WPA2/WPA3 Personal, open, or WPA2-Enterprise
 * "work login" with PEAP/TTLS through esp_eap_client) and reconnects with a back-off when it drops. The setup
 * network is APSTA: the open "TinyBar-Setup" access point at 192.168.4.1 with a DNS catch-all (net_dns.c) and the
 * setup page (net_http.c); the station side scans and tries the chosen network while the access point stays up, so
 * the phone can read the result (api.md 13.3). Once the bar has an address: mDNS (api.md 3) and SNTP.
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
 * start stays offline with the radio off, instead of opening the open TinyBar-Setup network again. Set up (the QR
 * code) and a join that works clear it.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cal_sync.h"
#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_log.h"
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
#include "net_util.h"
#include "tb_bus.h"
#include "tb_text.h"

static const char *TAG = "net.wifi";

#define SETUP_SSID        "TinyBar-Setup"
#define SCAN_MAX          20
#define JOIN_TIMEOUT_US   (30 * 1000000LL)  /* a join that hasn't worked in 30 s has failed */
#define NO_ADDRESS_US     (15 * 1000000LL)  /* joined but no address in 15 s: often a sign-in-page network */
#define AP_LINGER_US      (15 * 1000000LL)  /* the setup network stays up a little after Connected, for the page */
#define SCAN_STALE_US     (15 * 1000000LL)
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

typedef enum { J_SAVE_CREDS = 1, J_ONLINE, J_SCAN, J_STOP_AP, J_CONNECT, J_SAVE_SKIP, J_CLEAR_SKIP } job_t;

static SemaphoreHandle_t s_lock;
static QueueHandle_t s_jobs;
static esp_netif_t *s_sta, *s_ap;
static bool s_inited, s_running, s_creds_loaded, s_have_creds, s_ap_up;
static bool s_skipped;          /* Skip was the last Wi-Fi choice (saved as "skipped") */
static creds_t s_creds;
static wmode_t s_mode;
static bool s_sta_up;
static char s_ip[TB_IP_BYTES], s_ssid[TB_SSID_BYTES];
static int8_t s_rssi;
static int64_t s_rssi_at;
static int s_retry;
static esp_timer_handle_t s_retry_timer, s_join_timer, s_addr_timer, s_linger_timer;

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
static char s_host[64] = "tinybar.local";
static net_time_source_t s_time_src = NET_TIME_NONE;
static int64_t s_last_ntp_ms = -1;

#define LOCK() xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

/* The setup page's calendar address is a secret: zeroed before it's freed. The caller holds s_lock. */
static void drop_pending_cal_locked(void)
{
    if (!s_pending_cal) return;
    memset(s_pending_cal, 0, strlen(s_pending_cal));
    free(s_pending_cal);
    s_pending_cal = NULL;
}

static void job(job_t j)
{
    if (s_jobs) xQueueSend(s_jobs, &j, 0);
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

/* ======================================================================================================== */
/* Credentials                                                                                              */
/* ======================================================================================================== */

static void creds_load(void)
{
    if (s_creds_loaded) return;
    s_creds_loaded = true;
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t skipped = 0;
    s_skipped = nvs_get_u8(h, "skipped", &skipped) == ESP_OK && skipped;
    size_t n = sizeof s_creds.ssid;
    if (nvs_get_str(h, "ssid", s_creds.ssid, &n) == ESP_OK && s_creds.ssid[0]) {
        n = sizeof s_creds.user;
        if (nvs_get_str(h, "user", s_creds.user, &n) != ESP_OK) s_creds.user[0] = '\0';
        n = sizeof s_creds.pass;
        if (nvs_get_str(h, "pass", s_creds.pass, &n) != ESP_OK) s_creds.pass[0] = '\0';
        if (nvs_get_u8(h, "sec", &s_creds.sec) != ESP_OK) s_creds.sec = s_creds.pass[0] ? NET_SEC_PASSWORD : NET_SEC_OPEN;
        s_have_creds = true;
    }
    nvs_close(h);
}

static void creds_save(const creds_t *c)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) return;
    esp_err_t err = nvs_set_str(h, "ssid", c->ssid);
    if (err == ESP_OK) err = nvs_set_str(h, "user", c->user);
    if (err == ESP_OK) err = nvs_set_str(h, "pass", c->pass);
    if (err == ESP_OK) err = nvs_set_u8(h, "sec", c->sec);
    if (err == ESP_OK) {
        esp_err_t e2 = nvs_erase_key(h, "skipped");     /* a join that worked ends "offline" */
        if (e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) err = e2;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGE(TAG, "saving the Wi-Fi network failed: %s", esp_err_to_name(err));
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

static void reconnect_later(void)
{
    static const int delays_s[] = {1, 2, 5, 10, 30};
    int d = delays_s[s_retry < 4 ? s_retry : 4];
    if (s_retry < 100) s_retry++;
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)d * 1000000ULL);
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
        bool joining = s_joining;
        UNLOCK();
        if (joining) esp_timer_start_once(s_addr_timer, NO_ADDRESS_US);
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
            if (mode != M_SETUP) post_wifi(TB_WIFI_EV_LINK_DOWN, NULL, NULL, NULL, NULL);
        }
        /* On the setup screens the station waits for the page's choice: reconnecting to the old network could move
         * the setup network to another channel under the phone. */
        if (mode == M_STA && s_have_creds) reconnect_later();
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
        bool joined = s_joining;
        if (joined) {
            s_joining = false;
            s_creds = s_join;
            s_have_creds = true;
            s_skipped = false;          /* creds_save() erases the saved skip too */
            s_js.state = NET_JOIN_CONNECTED;
            s_js.error = s_js.message = NULL;
            tb_strlcpy(s_js.ip, ip, sizeof s_js.ip);
            tb_strlcpy(s_js.host, s_host, sizeof s_js.host);
            tb_strlcpy(s_ssid, s_join.ssid, sizeof s_ssid);
        }
        tb_strlcpy(host, s_host, sizeof host);
        tb_strlcpy(ssid, s_ssid, sizeof ssid);
        wmode_t mode = s_mode;
        UNLOCK();
        esp_timer_stop(s_join_timer);
        esp_timer_stop(s_addr_timer);
        ESP_LOGI(TAG, "joined \"%s\", address %s", ssid, ip);
        if (joined) {
            job(J_SAVE_CREDS);
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
    join_failed(NET_JOIN_NO_ADDRESS);
}

static void on_linger_timer(void *arg)
{
    (void)arg;
    job(J_STOP_AP);
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
    mdns_hostname_set("tinybar");
    mdns_instance_name_set(net_bar_name());
    mdns_register_hostname_changed_callback(on_host_changed, NULL);
    mdns_txt_item_t txt[5];
    mdns_txt(txt);
    mdns_service_add(net_bar_name(), "_tinybar", "_tcp", 80, txt, 5);
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
    mdns_service_instance_name_set("_tinybar", "_tcp", name);
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

static void scan_start(void)
{
    LOCK();
    bool busy = s_scanning || s_joining || !s_running;
    if (!busy) s_scanning = true;
    UNLOCK();
    if (busy) return;
    wifi_scan_config_t sc = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE};
    sc.scan_time.active.min = 60;
    sc.scan_time.active.max = 120;
    if (esp_wifi_scan_start(&sc, false) != ESP_OK) {
        LOCK();
        s_scanning = false;
        UNLOCK();
    }
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        job_t j;
        if (xQueueReceive(s_jobs, &j, portMAX_DELAY) != pdTRUE) continue;
        switch (j) {
        case J_SAVE_CREDS: {
            creds_t c;
            LOCK();
            c = s_creds;
            UNLOCK();
            creds_save(&c);
            memset(&c, 0, sizeof c);
            break;
        }
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
        case J_SCAN: scan_start(); break;
        case J_SAVE_SKIP: skip_save(true); break;
        case J_CLEAR_SKIP: skip_save(false); break;
        case J_STOP_AP: {
            LOCK();
            bool stop = s_mode != M_SETUP && s_ap_up;
            if (stop) s_ap_up = false;
            UNLOCK();
            if (stop) {
                net_dns_stop();
                esp_wifi_set_mode(s_mode == M_OFF ? WIFI_MODE_NULL : WIFI_MODE_STA);
                ESP_LOGI(TAG, "setup network closed");
            }
            break;
        }
        case J_CONNECT: {
            static creds_t c;
            LOCK();
            bool joining = s_joining, go = s_mode != M_OFF && (s_joining || s_have_creds);
            if (joining) c = s_join;
            UNLOCK();
            if (go) {
                if (joining) sta_apply(&c);
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
    s_jobs = xQueueCreate(8, sizeof(job_t));
    if (!s_lock || !s_jobs) return ESP_ERR_NO_MEM;
    creds_load();
    /* The clock: the RTC set it at boot if it held a time TinyBar wrote (board_rtc_init runs before net_init). */
    if (time(NULL) > 1735689600) s_time_src = NET_TIME_RTC;     /* after 2025-01-01 */

    s_sta = esp_netif_create_default_wifi_sta();
    s_ap = esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(s_sta, "tinybar");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);     /* the credentials are ours (NVS "wifi"), not the driver's */
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_ip, NULL);

    const esp_timer_create_args_t t1 = {.callback = on_retry_timer, .name = "wifi_retry"};
    const esp_timer_create_args_t t2 = {.callback = on_join_timer, .name = "wifi_join"};
    const esp_timer_create_args_t t3 = {.callback = on_addr_timer, .name = "wifi_addr"};
    const esp_timer_create_args_t t4 = {.callback = on_linger_timer, .name = "wifi_linger"};
    esp_timer_create(&t1, &s_retry_timer);
    esp_timer_create(&t2, &s_join_timer);
    esp_timer_create(&t3, &s_addr_timer);
    esp_timer_create(&t4, &s_linger_timer);
    /* writes NVS: the stack stays in internal RAM */
    if (xTaskCreatePinnedToCore(worker, "net", 4096, NULL, 4, NULL, 0) != pdPASS) return ESP_ERR_NO_MEM;
    s_inited = true;
    return ESP_OK;
}

static void wifi_run(wifi_mode_t mode)
{
    esp_wifi_set_mode(mode);
    if (!s_running) {
        if (esp_wifi_start() == ESP_OK) s_running = true;
    }
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
        wifi_run(WIFI_MODE_STA);    /* STA_START connects */
        ESP_LOGI(TAG, "joining the saved network");
    } else {
        net_wifi_setup_begin();
    }
}

static const char CAPTIVE_URI[] = "http://" NET_SETUP_IP "/";

void net_wifi_setup_begin(void)
{
    LOCK();
    bool was_skipped = s_skipped;
    s_skipped = false;
    UNLOCK();
    if (was_skipped) job(J_CLEAR_SKIP);
    LOCK();
    s_mode = M_SETUP;
    s_joining = false;
    memset(&s_js, 0, sizeof s_js);
    drop_pending_cal_locked();
    bool ap_up = s_ap_up;
    s_ap_up = true;
    UNLOCK();
    esp_timer_stop(s_linger_timer);
    esp_timer_stop(s_join_timer);
    if (!ap_up) {
        wifi_config_t ap;
        memset(&ap, 0, sizeof ap);
        memcpy(ap.ap.ssid, SETUP_SSID, sizeof SETUP_SSID - 1);
        ap.ap.ssid_len = sizeof SETUP_SSID - 1;
        ap.ap.channel = 1;
        ap.ap.authmode = WIFI_AUTH_OPEN;
        ap.ap.max_connection = 4;
        wifi_run(WIFI_MODE_APSTA);
        esp_wifi_set_config(WIFI_IF_AP, &ap);
        /* DHCP: the bar is the DNS server (LWIP_DHCPS_ADD_DNS), and RFC 8910's captive-portal address */
        esp_netif_dhcps_stop(s_ap);
        esp_netif_dhcps_option(s_ap, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, (void *)CAPTIVE_URI, sizeof CAPTIVE_URI - 1);
        esp_netif_dhcps_start(s_ap);
        esp_netif_ip_info_t info;
        uint32_t ip = 0;
        if (esp_netif_get_ip_info(s_ap, &info) == ESP_OK) ip = info.ip.addr;
        net_dns_start(ip);
        ESP_LOGI(TAG, "setup network up: " SETUP_SSID);
    }
    job(J_SCAN);
}

void net_wifi_setup_skip(void)
{
    LOCK();
    bool save = !s_skipped;
    s_skipped = true;
    UNLOCK();
    if (save) job(J_SAVE_SKIP);     /* the next start stays offline too */
    LOCK();
    s_mode = M_OFF;
    s_joining = false;
    s_sta_up = false;
    s_ap_up = false;
    drop_pending_cal_locked();
    UNLOCK();
    esp_timer_stop(s_linger_timer);
    esp_timer_stop(s_join_timer);
    esp_timer_stop(s_retry_timer);
    esp_timer_stop(s_addr_timer);
    net_dns_stop();
    if (s_running) {
        esp_wifi_stop();    /* offline: the radio goes off */
        s_running = false;
    }
    ESP_LOGI(TAG, "Wi-Fi skipped");
}

void net_wifi_setup_done(void)
{
    LOCK();
    if (s_mode == M_SETUP) s_mode = M_STA;
    UNLOCK();
    esp_timer_stop(s_linger_timer);
    esp_timer_start_once(s_linger_timer, AP_LINGER_US);
}

bool net_wifi_rf_on(void)
{
    return s_running;
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
    bool stale = !s_scanning && now - s_scan_at > SCAN_STALE_US;
    UNLOCK();
    if (stale) job(J_SCAN);     /* refreshed in the background; the page polls */
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
