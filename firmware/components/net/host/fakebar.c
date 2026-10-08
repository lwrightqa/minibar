/*
 * fakebar.c: a MiniBar on Linux for testing the Remote and setup pages. Owner: net builder.
 *
 * The real router (proto/net_api.c) and the real status engine (core) behind a tiny single-threaded HTTP server, with
 * a simulated device around them: a Wi-Fi scan, joins that take 2.5 s (see below), a calendar check that passes unless the address ends in
 * 0000/basic.ics, and a Mac you can drive with USB lines.
 *
 *   tb_fakebar [--port 8080] [--setup] [--auth-none]
 * Joins work unless the password is "wrong-password" or the network is "Printer-Direct".
 *   GET  /               the Remote (or, with --setup, the setup page: the browser is a phone on MiniBar-Setup)
 *   ANY  /api/...        the router, exactly as on the bar (the Host must be 127.0.0.1:<port>)
 *   POST /_sim/usb       body: one "@tb ..." line; answers the reply line (a Mac on USB)
 *   GET  /_sim/state     the bar's screen state as JSON (toast, own status, pairing code...) for test checks
 *   POST /_sim/meetings  body: minutes from now for each meeting's start, length 30 min, e.g. "-5 45"
 *   POST /_sim/tap       a tap on the bar's screen (cancels a pairing code, finishes the Connected screen...)
 *   POST /_sim/forget    Forget all, confirmed on the bar (hold, Wi-Fi, Devices, Forget all)
 *   POST /_sim/restart   what a restart does to pairing: any code ends, the back-off is cleared (tokens stay)
 *   POST /_sim/connected the bar shows Wi-Fi setup's Connected screen (it's back on the office Wi-Fi; a tap ends it)
 *   POST /_sim/jira      body: how Jira answers from now on: "ok 12", "down", "token", "nofilter", or "age 180" (the last answer
 *                        was that many minutes ago); the saved setup checks again at once (filter 10042 is "Open bugs",
 *                        10043 "Needs review", any other "Filter <id>")
 * Not part of the firmware or the host tests.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "cal_list.h"
#include "cal_url.h"
#include "jira_service.h"
#include "net_api.h"
#include "net_pair.h"
#include "net_port.h"
#include "net_util.h"
#include "tb_app.h"
#include "tb_settings.h"
#include "tb_text.h"

#ifndef TB_WEB_DIR
#define TB_WEB_DIR "."
#endif

static tb_app_t s_app;
static int s_port = 8080;
static bool s_setup_net;     /* --setup: the browser stands for a phone on MiniBar-Setup */
static net_wifi_info_t s_wifi;
static net_time_source_t s_tsrc = NET_TIME_NTP;
static cal_status_t s_cal;
static net_join_status_t s_join;
static char s_join_ssid[33], s_join_pass[129];
static tb_ms_t s_join_at, s_cal_at, s_sync_at;
static bool s_cal_from_setup;
static char s_cal_url[1100];
/* Several calendars: by slot. Each has its own sample meetings, and every one has the "Standup" at +300 min, so the merge
 * shows it once. An add or an address change is checked for 1.2 s first (it fails when the address ends in 0000/basic.ics). */
static cal_items_t s_items;
static struct { char url[1100]; bool failing; int shift; } s_slot[TB_CALS_MAX];
static int s_chk_slot = -1;
static char s_chk_name[100], s_chk_tag[40];
static uint8_t s_tokens[NET_TOKEN_BLOB_MAX];
static size_t s_tokens_n;

static tb_clock_t now_clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    tb_clock_t c = {.mono = (tb_ms_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000, .wall = time(NULL), .valid = true};
    return c;
}

/* ======================================================================================================== */
/* net_port.h, simulated                                                                                    */
/* ======================================================================================================== */

void net_port_device_id(char out[13]) { memcpy(out, "f412fa3f2a1c", 13); }
const char *net_port_fw_version(void) { return "1.0.0-host"; }
tb_clock_t net_port_now(void) { return now_clock(); }
net_time_source_t net_port_time_source(void) { return s_tsrc; }
bool net_port_set_time_from_mac(tb_epoch_t t) { (void)t; return false; }
bool net_port_time_zone_known(const char *iana)
{
    return iana && (!strcmp(iana, "America/Los_Angeles") || !strcmp(iana, "America/New_York") ||
                    !strcmp(iana, "Europe/Berlin") || !strcmp(iana, "UTC") || !strcmp(iana, "Europe/London"));
}
void net_port_wifi(net_wifi_info_t *out) { *out = s_wifi; }

static const net_scan_entry_t SCAN[] = {
    {"Office-WiFi", NET_SEC_PASSWORD, -52}, {"Office-Corp", NET_SEC_WORK_LOGIN, -60},
    {"Office-Guest", NET_SEC_OPEN, -63}, {"Printer-Direct", NET_SEC_PASSWORD, -84},
};

int net_port_setup_networks(net_scan_entry_t *out, int max)
{
    int n = (int)(sizeof SCAN / sizeof SCAN[0]);
    if (n > max) n = max;
    memcpy(out, SCAN, (size_t)n * sizeof *out);
    return n;
}

bool net_port_setup_join(const char *ssid, const char *u, const char *p, const char *cal)
{
    (void)u;
    tb_strlcpy(s_join_ssid, ssid, sizeof s_join_ssid);
    tb_strlcpy(s_join_pass, p ? p : "", sizeof s_join_pass);
    tb_strlcpy(s_cal_url, cal ? cal : "", sizeof s_cal_url);
    memset(&s_join, 0, sizeof s_join);
    s_join.state = NET_JOIN_CONNECTING;
    s_join_at = now_clock().mono + 2500;
    return true;
}

void net_port_setup_status(net_join_status_t *out) { *out = s_join; }

int net_port_cal_check(const char *url)
{
    if (!url || !*url) return CAL_URL_EMPTY;
    if (strlen(url) > CAL_URL_MAX) return CAL_URL_TOO_LONG;
    if (!strncmp(url, "http://", 7)) return CAL_URL_HTTP;
    if (strncmp(url, "https://", 8) && strncmp(url, "webcal://", 9)) return CAL_URL_NOT_A_URL;
    if (strstr(url, "/public/")) return CAL_URL_PUBLIC;
    size_t n = strlen(url);
    if (n < 4 || strcasecmp(url + n - 4, ".ics")) return CAL_URL_NOT_ICS;
    return CAL_URL_OK;
}

/* The fake's list as the pure list logic (cal_list.h) sees it, so names and tags are checked by the real rules. */
static void fake_list(cal_list_t *l)
{
    cal_list_init(l);
    for (int i = 0; i < TB_CALS_MAX; i++) {
        if (!s_items.c[i].used) continue;
        l->c[i].used = true;
        tb_strlcpy(l->c[i].name, s_items.c[i].name, sizeof l->c[i].name);
        tb_strlcpy(l->c[i].tag, s_items.c[i].tag, sizeof l->c[i].tag);
    }
}

/* The summary status (GET /api/v1/calendar) from the items. */
static void refresh_status(void)
{
    cal_check_state_t chk = s_cal.check;
    const char *ce = s_cal.check_error;
    char cm[sizeof s_cal.check_message];
    memcpy(cm, s_cal.check_message, sizeof cm);
    int cid = s_cal.check_id;
    memset(&s_cal, 0, sizeof s_cal);
    s_cal.check = chk;
    s_cal.check_error = ce;
    s_cal.check_id = cid;
    memcpy(s_cal.check_message, cm, sizeof cm);
    for (int i = 0; i < TB_CALS_MAX; i++) {
        const cal_item_t *c = &s_items.c[i];
        if (!c->used) continue;
        s_cal.saved = true;
        if (c->last_sync > s_cal.last_sync) s_cal.last_sync = c->last_sync;
        if (c->failing && !s_cal.error) {
            s_cal.error = c->error;
            tb_strlcpy(s_cal.error_message, c->error_message, sizeof s_cal.error_message);
            s_cal.error_at = c->error_at;
        }
    }
    s_cal.syncing = s_cal.check == CAL_CHECK_CHECKING || s_sync_at != 0;
}

/* Merge the calendars' sample meetings into core, and tell it the list. */
static void rebuild(const tb_clock_t *now, bool announce_removed_name_valid, const char *removed)
{
    static tb_meeting_t mine[TB_CALS_MAX][3], out[CAL_MERGE_CAP];
    cal_source_t src[TB_CALS_MAX];
    tb_cal_info_t info[TB_CALS_MAX];
    int ns = 0;
    memset(info, 0, sizeof info);
    for (int i = 0; i < TB_CALS_MAX; i++) {
        if (!s_items.c[i].used) continue;
        static const char *const titles[] = {"Design review", "1:1 with Sam", "Planning"};
        int starts[3] = {48 + s_slot[i].shift, 108 + s_slot[i].shift, 198};
        for (int k = 0; k < 3; k++) {
            memset(&mine[i][k], 0, sizeof mine[i][k]);
            mine[i][k].id = k == 2 ? 5000 : (uint32_t)(1000 + i * 10 + k);
            mine[i][k].start = now->wall + starts[k] * 60;
            mine[i][k].end = mine[i][k].start + 30 * 60;
            tb_strlcpy(mine[i][k].title, k == 2 ? "Standup" : titles[(i + k) % 3], sizeof mine[i][k].title);
        }
        info[i].used = true;
        info[i].failing = s_slot[i].failing;
        tb_strlcpy(info[i].name, s_items.c[i].name, sizeof info[i].name);
        tb_strlcpy(info[i].tag, s_items.c[i].tag, sizeof info[i].tag);
        src[ns++] = (cal_source_t){i, s_slot[i].failing, mine[i], 3};
    }
    int n = cal_merge(src, ns, out, TB_MEETINGS_MAX, now->wall);
    if (announce_removed_name_valid) {
        tb_app_calendar_removed(&s_app, removed, info, out, n, now);
    } else {
        tb_app_set_cal_list(&s_app, info, now);
        tb_app_set_meetings(&s_app, out, n, now);
    }
}

void net_port_cal_items(cal_items_t *out) { *out = s_items; }

cal_res_t net_port_cal_add(const char *url, const char *name, const char *tag)
{
    cal_res_t res = {net_port_cal_check(url), 0};
    if (res.url_err) return res;
    cal_list_t l;
    fake_list(&l);
    res.list_err = (int)cal_list_add(&l, name, tag, NULL);
    if (res.list_err) return res;
    tb_strlcpy(s_cal_url, url, sizeof s_cal_url);
    s_chk_slot = -1;
    tb_strlcpy(s_chk_name, name ? name : "", sizeof s_chk_name);
    tb_strlcpy(s_chk_tag, tag ? tag : "", sizeof s_chk_tag);
    s_cal.check = CAL_CHECK_CHECKING;
    s_cal.check_error = NULL;
    s_cal.check_message[0] = '\0';
    s_cal.check_id = 0;
    s_cal_at = now_clock().mono + 1200;
    refresh_status();
    tb_clock_t now = now_clock();
    tb_app_set_calendar(&s_app, s_cal.saved, true, s_cal.last_sync, &now);
    return res;
}

cal_res_t net_port_cal_edit(int id, const char *url, const char *name, const char *tag)
{
    cal_res_t res = {0, 0};
    int slot = id - 1;
    if (slot < 0 || slot >= TB_CALS_MAX || !s_items.c[slot].used) { res.list_err = CAL_LIST_NO_SUCH; return res; }
    if (url && url[0] && (res.url_err = net_port_cal_check(url)) != 0) return res;
    cal_list_t l;
    fake_list(&l);
    res.list_err = (int)cal_list_edit(&l, slot, name, tag);
    if (res.list_err) return res;
    tb_clock_t now = now_clock();
    if (url && url[0]) {
        tb_strlcpy(s_cal_url, url, sizeof s_cal_url);
        s_chk_slot = slot;
        tb_strlcpy(s_chk_name, name ? name : "", sizeof s_chk_name);
        tb_strlcpy(s_chk_tag, tag ? tag : "", sizeof s_chk_tag);
        s_cal.check = CAL_CHECK_CHECKING;
        s_cal.check_error = NULL;
        s_cal.check_message[0] = '\0';
        s_cal.check_id = id;
        s_cal_at = now.mono + 1200;
        refresh_status();
    } else {
        tb_strlcpy(s_items.c[slot].name, l.c[slot].name, sizeof s_items.c[slot].name);
        tb_strlcpy(s_items.c[slot].tag, l.c[slot].tag, sizeof s_items.c[slot].tag);
        rebuild(&now, false, NULL);
    }
    return res;
}

int net_port_cal_remove_id(int id)
{
    int slot = id - 1;
    if (slot < 0 || slot >= TB_CALS_MAX || !s_items.c[slot].used) return -1;
    tb_clock_t now = now_clock();
    char name[TB_CAL_NAME_BYTES];
    tb_strlcpy(name, s_items.c[slot].name, sizeof name);
    memset(&s_items.c[slot], 0, sizeof s_items.c[slot]);
    memset(&s_slot[slot], 0, sizeof s_slot[slot]);
    s_items.n--;
    refresh_status();
    rebuild(&now, true, name);
    tb_app_set_calendar(&s_app, s_cal.saved, false, s_cal.last_sync, &now);
    return 0;
}

/* The single-address calls: the setup page always adds; PUT adds the first or replaces the only one. */
int net_port_cal_put(const char *url, bool from_setup)
{
    int only = -1;
    for (int i = 0; i < TB_CALS_MAX; i++)
        if (s_items.c[i].used) only = i;
    s_cal_from_setup = from_setup;
    cal_res_t r = from_setup || only < 0 ? net_port_cal_add(url, NULL, NULL) : net_port_cal_edit(only + 1, url, NULL, NULL);
    return r.url_err;
}

int net_port_cal_remove(void)
{
    for (int i = 0; i < TB_CALS_MAX; i++)
        if (s_items.c[i].used) return net_port_cal_remove_id(i + 1);
    return -1;
}

int net_port_cal_sync_now(void)
{
    if (!s_cal.saved) return -1;
    if (!s_wifi.sta_up) return -2;
    s_sync_at = now_clock().mono + 1000;
    refresh_status();
    return 0;
}

void net_port_cal_status(cal_status_t *out) { *out = s_cal; }

void net_port_random(void *buf, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(buf, 1, n, f) != n) memset(buf, 0x5A, n);
    if (f) fclose(f);
}

void net_port_sha256(const void *data, size_t n, uint8_t out[32])
{
    const uint8_t *p = data;     /* a stand-in (FNV lanes); the bar uses mbedTLS */
    for (int lane = 0; lane < 8; lane++) {
        uint32_t h = 2166136261u ^ (uint32_t)lane * 0x9E3779B9u;
        for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
        memcpy(out + lane * 4, &h, 4);
    }
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

/* ======================================================================================================== */
/* The simulated device                                                                                     */
/* ======================================================================================================== */

static void sample_meetings(const int *starts_min, int n, const tb_clock_t *now)
{
    tb_meeting_t m[TB_MEETINGS_MAX];
    memset(m, 0, sizeof m);
    static const char *titles[] = {"Design review", "1:1 with Sam", "Planning", "Standup"};
    for (int i = 0; i < n && i < TB_MEETINGS_MAX; i++) {
        m[i].id = (uint32_t)(1000 + i);
        m[i].start = now->wall + starts_min[i] * 60;
        m[i].end = m[i].start + 30 * 60;
        tb_strlcpy(m[i].title, titles[i % 4], sizeof m[i].title);
        tb_strlcpy(m[i].location, i == 0 ? "Room 4" : "", sizeof m[i].location);
    }
    tb_app_set_meetings(&s_app, m, n, now);
}

/* ======================================================================================================== */
/* The Jira service, simulated (jira_service.h): the real checks (jira_resolve), answers from /_sim/jira               */
/* ======================================================================================================== */

static struct {
    bool saved;
    jira_cfg_t cfg;
    tb_jira_t j;
    char mode[16];
    int count;
    tb_ms_t check_at;           /* the saved setup's check finishes then (0: none) */
    jira_test_state_t test_state;
    char test_name[TB_JIRA_LABEL_BYTES];
    int test_count;
    const char *test_error;
    tb_ms_t test_at;
    jira_cfg_t test_cfg;
    char filter_name[TB_JIRA_LABEL_BYTES];
} s_jira = {.mode = "ok", .count = 12};

static const char *jira_name_of(const char *id, char *buf, size_t cap)
{
    snprintf(buf, cap, "%s", !strcmp(id, "10042") ? "Open bugs" : !strcmp(id, "10043") ? "Needs review" : "");
    if (!buf[0]) snprintf(buf, cap, "Filter %s", id);
    return buf;
}

static void jira_push(const tb_clock_t *now)
{
    tb_app_set_jira(&s_app, &s_jira.j, now);
}

void jira_get_info(jira_info_t *out)
{
    memset(out, 0, sizeof *out);
    out->alert_above = -1;
    out->count = -1;
    out->test.state = s_jira.test_state;
    snprintf(out->test.name, sizeof out->test.name, "%s", s_jira.test_name);
    out->test.count = s_jira.test_count;
    out->test.error = s_jira.test_error;
    if (!s_jira.saved) return;
    const jira_cfg_t *c = &s_jira.cfg;
    out->configured = true;
    snprintf(out->site, sizeof out->site, "%s", c->site);
    jira_email_hint(c->email, out->email_hint, sizeof out->email_hint);
    out->token_saved = c->token[0] != '\0';
    snprintf(out->filter_id, sizeof out->filter_id, "%s", c->filter);
    snprintf(out->filter_name, sizeof out->filter_name, "%s", s_jira.filter_name);
    snprintf(out->label, sizeof out->label, "%s", c->label);
    out->alert_above = c->alert_above;
    out->state = s_jira.j.state;
    out->count = s_jira.j.count;
    out->updated_at = s_jira.j.ok_at;
}

jira_err_t jira_save(const jira_input_t *in)
{
    jira_cfg_t c;
    jira_err_t e = jira_resolve(s_jira.saved ? &s_jira.cfg : NULL, in, &c);
    if (e != JIRA_OK) return e;
    tb_clock_t now = now_clock();
    char nm[TB_JIRA_LABEL_BYTES];
    jira_name_of(c.filter, nm, sizeof nm);
    if (c.label_auto) snprintf(c.label, sizeof c.label, "%s", nm);       /* the name the first check learns */
    snprintf(s_jira.filter_name, sizeof s_jira.filter_name, "%s", nm);
    s_jira.saved = true;
    s_jira.cfg = c;
    tb_jira_configure(&s_jira.j, c.label, c.alert_above, c.goal_type, c.goal_value);
    s_jira.check_at = now.mono + 600;
    jira_push(&now);
    return JIRA_OK;
}

jira_err_t jira_test(const jira_input_t *in)
{
    jira_cfg_t c;
    jira_err_t e = jira_resolve(s_jira.saved ? &s_jira.cfg : NULL, in, &c);
    if (e != JIRA_OK) return e;
    s_jira.test_cfg = c;
    s_jira.test_state = JIRA_TEST_ASKING;
    s_jira.test_at = now_clock().mono + 800;
    return JIRA_OK;
}

bool jira_remove(void)
{
    bool was = s_jira.saved;
    tb_clock_t now = now_clock();
    s_jira.saved = false;
    memset(&s_jira.cfg, 0, sizeof s_jira.cfg);
    tb_jira_clear(&s_jira.j);
    s_jira.check_at = 0;
    s_jira.test_state = JIRA_TEST_IDLE;
    jira_push(&now);
    return was;
}

/* What Jira would answer for a check of this filter now. */
static tb_jira_result_t jira_answer(const char *filter, int *count, const char **err)
{
    (void)filter;
    *err = NULL;
    if (!s_wifi.sta_up) { *err = JIRA_ERR_OFFLINE; return TB_JIRA_RES_UNREACHABLE; }
    if (!strcmp(s_jira.mode, "down")) { *err = JIRA_ERR_UNREACHABLE; return TB_JIRA_RES_UNREACHABLE; }
    if (!strcmp(s_jira.mode, "token")) { *err = JIRA_ERR_TOKEN; return TB_JIRA_RES_TOKEN; }
    if (!strcmp(s_jira.mode, "nofilter") || !strcmp(filter, "10000")) { *err = JIRA_ERR_FILTER; return TB_JIRA_RES_NOFILTER; }
    *count = s_jira.count;
    return TB_JIRA_RES_OK;
}

static void jira_tick(const tb_clock_t *now)
{
    if (s_jira.saved && s_jira.check_at && now->mono >= s_jira.check_at) {
        int c = 0;
        const char *err;
        tb_jira_result_t r = jira_answer(s_jira.cfg.filter, &c, &err);
        tb_jira_apply(&s_jira.j, r, c, now->wall);
        s_jira.check_at = 0;
        jira_push(now);
    }
    if (s_jira.test_state == JIRA_TEST_ASKING && now->mono >= s_jira.test_at) {
        int c = 0;
        const char *err;
        tb_jira_result_t r = jira_answer(s_jira.test_cfg.filter, &c, &err);
        if (r == TB_JIRA_RES_OK) {
            s_jira.test_state = JIRA_TEST_OK;
            jira_name_of(s_jira.test_cfg.filter, s_jira.test_name, sizeof s_jira.test_name);
            s_jira.test_count = c;
            s_jira.test_error = NULL;
        } else {
            s_jira.test_state = JIRA_TEST_ERROR;
            s_jira.test_error = err;
        }
    }
}

static void sim_tick(const tb_clock_t *now)
{
    jira_tick(now);
    if (s_join.state == NET_JOIN_CONNECTING && now->mono >= s_join_at) {
        if (!strcmp(s_join_ssid, "Printer-Direct")) {
            s_join.state = NET_JOIN_FAILED;
            s_join.error = "no_signal";
            s_join.message = "The signal is too weak, or the network didn't answer.";
            tb_app_wifi_failed(&s_app, s_join_ssid, "No signal", now);
        } else if (!strcmp(s_join_pass, "wrong-password")) {
            /* the bar refuses passwords under 8 characters at once, so here this one stands for a wrong password */
            s_join.state = NET_JOIN_FAILED;
            s_join.error = "wrong_password";
            s_join.message = "The password didn't work.";
            tb_app_wifi_failed(&s_app, s_join_ssid, "Wrong password", now);
        } else {
            s_join.state = NET_JOIN_CONNECTED;
            snprintf(s_join.host, sizeof s_join.host, "minibar.local");
            snprintf(s_join.ip, sizeof s_join.ip, "127.0.0.1");
            s_wifi.sta_up = true;
            tb_strlcpy(s_wifi.ssid, s_join_ssid, sizeof s_wifi.ssid);
            tb_app_wifi_connected(&s_app, s_join_ssid, "127.0.0.1", "minibar.local", now);
            if (s_cal_url[0]) net_port_cal_put(s_cal_url, true);
        }
    }
    if (s_cal.check == CAL_CHECK_CHECKING && now->mono >= s_cal_at) {
        size_t n = strlen(s_cal_url);
        bool bad = n >= 14 && !strcmp(s_cal_url + n - 14, "0000/basic.ics");
        if (bad) {
            s_cal.check = CAL_CHECK_FAILED;
            s_cal.check_error = "calendar_rejected";
            snprintf(s_cal.check_message, sizeof s_cal.check_message,
                     "Google didn't recognize that address. It may have been reset in Google Calendar.");
            refresh_status();
            tb_app_set_calendar(&s_app, s_cal.saved, false, s_cal.last_sync, now);
            if (s_cal_from_setup) tb_app_calendar_event(&s_app, TB_CALEV_SETUP_FAILED, now);
        } else {
            cal_list_t l;
            fake_list(&l);
            int slot = s_chk_slot;
            if (slot >= 0) cal_list_edit(&l, slot, s_chk_name, s_chk_tag);
            else cal_list_add(&l, s_chk_name, s_chk_tag, &slot);
            s_items.c[slot].used = true;
            s_items.c[slot].id = slot + 1;
            tb_strlcpy(s_items.c[slot].name, l.c[slot].name, sizeof s_items.c[slot].name);
            tb_strlcpy(s_items.c[slot].tag, l.c[slot].tag, sizeof s_items.c[slot].tag);
            s_items.c[slot].last_sync = now->wall;
            s_items.c[slot].failing = false;
            s_items.c[slot].error = NULL;
            s_items.c[slot].left_today = 3;
            s_items.n = 0;
            for (int i = 0; i < TB_CALS_MAX; i++) s_items.n += s_items.c[i].used;
            tb_strlcpy(s_slot[slot].url, s_cal_url, sizeof s_slot[slot].url);
            s_slot[slot].failing = false;
            s_slot[slot].shift = slot * 30;
            s_cal.check = CAL_CHECK_SAVED;
            s_cal.check_id = slot + 1;
            refresh_status();
            rebuild(now, false, NULL);
            tb_app_calendar_event(&s_app, TB_CALEV_SAVED, now);
            tb_app_set_calendar(&s_app, true, false, s_cal.last_sync, now);
        }
        s_cal.syncing = s_sync_at != 0;
    }
    if (s_sync_at && now->mono >= s_sync_at) {
        s_sync_at = 0;
        bool any_bad = false;
        for (int i = 0; i < TB_CALS_MAX; i++) {
            if (!s_items.c[i].used) continue;
            if (s_slot[i].failing) any_bad = true;
            else s_items.c[i].last_sync = now->wall;
        }
        refresh_status();
        tb_app_calendar_event(&s_app, any_bad ? TB_CALEV_SYNC_FAILED : TB_CALEV_SYNCED, now);
        tb_app_set_calendar(&s_app, true, false, s_cal.last_sync, now);
    }
}

static void run_effects(const tb_clock_t *now)
{
    tb_effect_t fx[TB_EFFECTS_MAX];
    int n = tb_app_take_effects(&s_app, fx, TB_EFFECTS_MAX);
    for (int i = 0; i < n; i++) {
        switch (fx[i].kind) {
        case TB_FX_WIFI_SETUP: s_wifi.state = NET_WIFI_SETUP; memset(&s_join, 0, sizeof s_join); break;
        case TB_FX_WIFI_SKIP: s_wifi.state = NET_WIFI_OFFLINE; s_wifi.sta_up = false; break;
        case TB_FX_WIFI_DONE: s_wifi.state = NET_WIFI_CONNECTED; break;
        case TB_FX_PAIRING_CANCELED: net_api_pairing_canceled(now); break;
        case TB_FX_PAIRING_RESET: net_api_pairing_reset(now); break;
        case TB_FX_FORGET_DEVICES: net_api_forget_devices(now); break;
        case TB_FX_CAL_SYNC: net_port_cal_sync_now(); break;
        default: break;
        }
    }
}

/* ======================================================================================================== */
/* HTTP                                                                                                     */
/* ======================================================================================================== */

typedef struct {
    char method[8], path[600];
    char host[256], origin[256], auth[256], cookie[512], ctype[128], inm[128];
    long clen;
    char *body;
    size_t body_len;
    bool too_large;
} hreq_t;

static const char *reason(int st)
{
    switch (st) {
    case 200: return "OK"; case 202: return "Accepted"; case 204: return "No Content"; case 304: return "Not Modified";
    case 400: return "Bad Request"; case 401: return "Unauthorized"; case 403: return "Forbidden"; case 404: return "Not Found";
    case 405: return "Method Not Allowed"; case 409: return "Conflict"; case 413: return "Content Too Large";
    case 415: return "Unsupported Media Type"; case 421: return "Misdirected Request"; case 429: return "Too Many Requests";
    case 503: return "Service Unavailable"; default: return "Internal Server Error";
    }
}

static void send_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w <= 0) return;
        p += w;
        n -= (size_t)w;
    }
}

static void respond(int fd, int st, const char *ctype, const char *extra, const char *body, size_t len)
{
    char h[2048];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n%s\r\n",
                     st, reason(st), ctype, len, extra ? extra : "");
    send_all(fd, h, (size_t)n);
    if (body && len) send_all(fd, body, len);
}

static char *read_file(const char *name, size_t *len)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", TB_WEB_DIR, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    if (b) { b[n] = '\0'; *len = (size_t)n; }
    return b;
}

static bool read_request(int fd, hreq_t *q)
{
    static char buf[16384];
    size_t got = 0;
    char *end = NULL;
    while (!end && got < sizeof buf - 1) {
        ssize_t n = read(fd, buf + got, sizeof buf - 1 - got);
        if (n <= 0) return false;
        got += (size_t)n;
        buf[got] = '\0';
        end = strstr(buf, "\r\n\r\n");
    }
    if (!end) return false;
    *end = '\0';
    char *line = strtok(buf, "\r\n");
    if (!line || sscanf(line, "%7s %599s", q->method, q->path) != 2) return false;
    for (char *h = strtok(NULL, "\r\n"); h; h = strtok(NULL, "\r\n")) {
        char *c = strchr(h, ':');
        if (!c) continue;
        *c = '\0';
        char *v = c + 1;
        while (*v == ' ') v++;
        if (!strcasecmp(h, "Host")) tb_strlcpy(q->host, v, sizeof q->host);
        else if (!strcasecmp(h, "Origin")) tb_strlcpy(q->origin, v, sizeof q->origin);
        else if (!strcasecmp(h, "Authorization")) tb_strlcpy(q->auth, v, sizeof q->auth);
        else if (!strcasecmp(h, "Cookie")) tb_strlcpy(q->cookie, v, sizeof q->cookie);
        else if (!strcasecmp(h, "Content-Type")) tb_strlcpy(q->ctype, v, sizeof q->ctype);
        else if (!strcasecmp(h, "If-None-Match")) tb_strlcpy(q->inm, v, sizeof q->inm);
        else if (!strcasecmp(h, "Content-Length")) q->clen = atol(v);
    }
    size_t have = got - (size_t)(end + 4 - buf);
    if (q->clen > NET_BODY_MAX) q->too_large = true;
    if (q->clen > 0) {
        q->body = malloc((size_t)q->clen + 1);
        size_t take = have < (size_t)q->clen ? have : (size_t)q->clen;
        memcpy(q->body, end + 4, take);
        while (take < (size_t)q->clen) {
            ssize_t n = read(fd, q->body + take, (size_t)q->clen - take);
            if (n <= 0) break;
            take += (size_t)n;
        }
        q->body[take] = '\0';
        q->body_len = take;
    }
    return true;
}

static void cookie_token(const char *cookies, char *out, size_t cap)
{
    out[0] = '\0';
    const char *p = strstr(cookies, "tb_token=");
    if (!p) return;
    p += 9;
    size_t n = strcspn(p, "; ");
    if (n >= cap) return;
    memcpy(out, p, n);
    out[n] = '\0';
}

static void handle(int fd, bool *quit)
{
    (void)quit;
    hreq_t q;
    memset(&q, 0, sizeof q);
    if (!read_request(fd, &q)) return;
    tb_clock_t now = now_clock();
    if (!strncmp(q.path, "/api/", 5)) {
        char ctok[128];
        cookie_token(q.cookie, ctok, sizeof ctok);
        const char *ct = q.ctype;
        bool json = !strncasecmp(ct, "application/json", 16) && (!ct[16] || ct[16] == ';' || ct[16] == ' ');
        net_req_t r = {
            .via = s_setup_net ? NET_VIA_SETUP : NET_VIA_HTTP,
            .method = q.method, .path = q.path, .body = q.body, .body_len = q.body_len, .body_too_large = q.too_large,
            .content_type_json = json,
            /* the phone reaches the setup network by its address; here the page comes from 127.0.0.1 */
            .host = s_setup_net ? NET_SETUP_IP : q.host[0] ? q.host : NULL, .origin = q.origin[0] ? q.origin : NULL,
            .bearer = !strncasecmp(q.auth, "Bearer ", 7) ? q.auth + 7 : NULL, .cookie_token = ctok[0] ? ctok : NULL,
            .if_none_match = q.inm[0] ? q.inm : NULL, .peer_ip = 0x0100007f,
        };
        net_resp_t resp;
        net_api_handle(&r, &resp);
        char extra[512] = "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
        size_t e = strlen(extra);
        if (resp.etag[0]) e += (size_t)snprintf(extra + e, sizeof extra - e, "ETag: %s\r\n", resp.etag);
        if (resp.set_cookie[0]) e += (size_t)snprintf(extra + e, sizeof extra - e, "Set-Cookie: %s\r\n", resp.set_cookie);
        if (resp.allow[0]) e += (size_t)snprintf(extra + e, sizeof extra - e, "Allow: %s\r\n", resp.allow);
        if (resp.www_authenticate) e += (size_t)snprintf(extra + e, sizeof extra - e, "WWW-Authenticate: Bearer realm=\"MiniBar\"\r\n");
        if (resp.retry_after_s > 0) snprintf(extra + e, sizeof extra - e, "Retry-After: %d\r\n", resp.retry_after_s);
        respond(fd, resp.status, "application/json; charset=utf-8", extra, resp.status == 304 ? NULL : resp.body,
                resp.status == 304 ? 0 : resp.len);
        free(resp.body);
    } else if (!strcmp(q.path, "/_sim/usb") && q.body) {
        static char out[NET_REPLY_MAX + 16];
        size_t n = strcspn(q.body, "\r\n");
        if (!net_api_usb_line(q.body, n, n > NET_LINE_MAX, out, sizeof out)) out[0] = '\0';
        respond(fd, 200, "text/plain; charset=utf-8", NULL, out, strlen(out));
    } else if (!strcmp(q.path, "/_sim/meetings") && q.body) {
        int starts[TB_MEETINGS_MAX], n = 0;
        for (char *t = strtok(q.body, " ,"); t && n < TB_MEETINGS_MAX; t = strtok(NULL, " ,")) starts[n++] = atoi(t);
        s_cal.saved = true;
        sample_meetings(starts, n, &now);
        tb_app_set_calendar(&s_app, true, false, now.wall, &now);
        respond(fd, 200, "text/plain", NULL, "ok", 2);
    } else if (!strcmp(q.path, "/_sim/calfail") && q.body) {
        /* body: "<id> <0|1>": that calendar can't sync (1) or works again (0): left out of the bar, its row says why */
        int id = 0, on = 0;
        sscanf(q.body, "%d %d", &id, &on);
        if (id >= 1 && id <= TB_CALS_MAX && s_items.c[id - 1].used) {
            cal_item_t *c = &s_items.c[id - 1];
            s_slot[id - 1].failing = on;
            c->failing = on;
            c->error = on ? "calendar_unreachable" : NULL;
            tb_strlcpy(c->error_message, on ? "MiniBar couldn't reach the calendar's server. Try again in a minute." : "", sizeof c->error_message);
            c->error_at = on ? now.wall : 0;
            refresh_status();
            rebuild(&now, false, NULL);
        }
        respond(fd, 200, "text/plain", NULL, "ok", 2);
    } else if (!strcmp(q.path, "/_sim/jira") && q.body) {
        char mode[16] = "";
        int n = 0;
        sscanf(q.body, "%15s %d", mode, &n);
        if (!strcmp(mode, "age")) {
            if (s_jira.j.ok_at) s_jira.j.ok_at = now.wall - (tb_epoch_t)n * 60;
            s_jira.j.state = TB_JIRA_UNREACHABLE;
            snprintf(s_jira.mode, sizeof s_jira.mode, "down");
        } else {
            snprintf(s_jira.mode, sizeof s_jira.mode, "%s", mode);
            if (!strcmp(mode, "ok")) s_jira.count = n;
        }
        if (s_jira.saved) s_jira.check_at = now.mono + 300;
        jira_push(&now);
        respond(fd, 200, "text/plain", NULL, "ok", 2);
    } else if (!strcmp(q.path, "/_sim/tap")) {
        tb_app_pointer(&s_app, true, 300, 80, TB_TILE_NONE, &now);
        tb_app_pointer(&s_app, false, 300, 80, TB_TILE_NONE, &now);
        respond(fd, 200, "text/plain", NULL, "ok", 2);
    } else if (!strcmp(q.path, "/_sim/forget")) {
        /* In the firmware core toasts "Forgot 3 devices" first and net's effect runs after it, so a Wi-Fi call that
         * ends with the tokens replaces the toast with "Forgot 3 devices · back to Busy"; the same order here. */
        char t[TB_TOAST_BYTES];
        snprintf(t, sizeof t, "Forgot %u device%s", (unsigned)s_app.paired_count, s_app.paired_count == 1 ? "" : "s");
        tb_app_notify(&s_app, t, &now);
        net_api_forget_devices(&now);
        respond(fd, 200, "text/plain", NULL, "ok", 2);
    } else if (!strcmp(q.path, "/_sim/restart")) {
        if (s_app.pairing.active) tb_app_pairing_end(&s_app, TB_PAIR_END_CANCELED, NULL, &now);
        net_api_pairing_reset(&now);
        respond(fd, 200, "text/plain", NULL, "ok", 2);
    } else if (!strcmp(q.path, "/_sim/connected")) {
        s_wifi.state = NET_WIFI_SETUP;     /* the setup network stays up until the Connected screen is over */
        tb_app_wifi_connected(&s_app, "Office-WiFi", "127.0.0.1", s_wifi.host, &now);
        respond(fd, 200, "text/plain", NULL, "ok", 2);
    } else if (!strcmp(q.path, "/_sim/state")) {
        char b[1024];
        int n = snprintf(b, sizeof b, "{\"toast\":\"%s\",\"pending\":\"%s\",\"idx\":%d,\"wifi_mode\":%d,\"pairing\":%s,\"code\":\"%s\",\"who\":\"%s\",\"paired\":%d,\"off\":%s}",
                         s_app.toast, s_app.pending_toast, (int)s_app.idx, (int)s_app.wifi_mode,
                         s_app.pairing.active ? "true" : "false", s_app.pairing.code, s_app.pairing.who,
                         (int)s_app.paired_count, s_app.off ? "true" : "false");
        respond(fd, 200, "application/json", NULL, b, (size_t)n);
    } else if (!strcmp(q.method, "GET") && (!strcmp(q.path, "/") || !strncmp(q.path, "/?", 2))) {
        size_t len = 0;
        char *page = read_file(s_setup_net ? "setup.html" : "remote.html", &len);
        if (page) respond(fd, 200, "text/html; charset=utf-8", "Cache-Control: no-cache\r\n", page, len);
        else respond(fd, 500, "text/plain", NULL, "page missing", 12);
        free(page);
    } else {
        respond(fd, 404, "text/plain", NULL, "Not found.", 10);
    }
    free(q.body);
}

int main(int argc, char **argv)
{
    bool setup = false, auth_none = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) s_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--setup")) setup = true;
        else if (!strcmp(argv[i], "--auth-none")) auth_none = true;
    }
    if (!getenv("TZ")) setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();
    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    tb_clock_t now = now_clock();
    s_setup_net = setup;
    tb_app_init(&s_app, &s, setup ? TB_WIFI_SETUP : TB_WIFI_OK, &now);
    s_wifi.state = setup ? NET_WIFI_SETUP : NET_WIFI_CONNECTED;
    s_wifi.sta_up = !setup;
    snprintf(s_wifi.ssid, sizeof s_wifi.ssid, "Office-WiFi");
    snprintf(s_wifi.ip, sizeof s_wifi.ip, "127.0.0.1");
    snprintf(s_wifi.host, sizeof s_wifi.host, "127.0.0.1:%d", s_port);
    s_wifi.rssi = -58;
    net_api_bind(&s_app);
    net_api_set_auth(!auth_none);
    net_api_init();

    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)s_port), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, 16)) {
        perror("listen");
        return 1;
    }
    fprintf(stderr, "fakebar: http://127.0.0.1:%d/ (%s, auth %s)\n", s_port, setup ? "setup" : "on Wi-Fi", auth_none ? "none" : "bearer");
    bool quit = false;
    while (!quit) {
        struct pollfd p = {.fd = ls, .events = POLLIN};
        if (poll(&p, 1, 50) > 0) {
            int fd = accept(ls, NULL, NULL);
            if (fd >= 0) {
                struct timeval tv = {.tv_sec = 2};
                setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
                handle(fd, &quit);
                close(fd);
            }
        }
        now = now_clock();
        tb_app_tick(&s_app, &now);
        net_api_tick(&now);
        sim_tick(&now);
        run_effects(&now);
    }
    return 0;
}
