/*
 * fake_port.c: net_port.h for the host tests. Owner: net builder (seeded by the lead).
 * Tests change the fake_* variables to put the "device" into the state they need; fake_reset() restores defaults.
 */
#include <string.h>

#include "cal_url.h"
#include "fake_port.h"
#include "net_port.h"
#include <stdio.h>

#include "tb_text.h"

/* 2026-10-04 14:12:00 -07:00 */
#define FAKE_WALL 1791148320

tb_clock_t fake_now;
net_wifi_info_t fake_wifi;
net_time_source_t fake_time_source;
cal_status_t fake_cal;
net_scan_entry_t fake_networks[8];
int fake_n_networks;
net_join_status_t fake_join;
bool fake_join_ok;
int fake_join_calls;
char fake_join_ssid[33], fake_join_user[129], fake_join_pass[129], fake_join_cal[1100];
int fake_cal_put_calls;
bool fake_cal_put_from_setup;
int fake_cal_sync_result;
cal_items_t fake_items;
int fake_cal_add_calls, fake_cal_edit_calls, fake_cal_remove_calls;
char fake_cal_last_url[1100], fake_cal_last_name[100], fake_cal_last_tag[40];
int fake_cal_last_id;
int fake_mac_time_calls;
fake_jira_t fake_jira;
tb_epoch_t fake_mac_time;
bool fake_mac_time_accept;
int fake_tokens_saves;

static uint8_t s_tokens[4096];
static size_t s_tokens_n;
static uint32_t s_rand = 12345;

void fake_reset(void)
{
    fake_now = (tb_clock_t){.mono = 1000, .wall = FAKE_WALL, .valid = true};
    fake_wifi = (net_wifi_info_t){.state = NET_WIFI_CONNECTED, .sta_up = true, .ssid = "Office-WiFi", .ip = "10.0.4.42",
                                  .host = "minibar.local", .rssi = -61};
    fake_time_source = NET_TIME_NTP;
    memset(&fake_cal, 0, sizeof fake_cal);
    memset(fake_networks, 0, sizeof fake_networks);
    fake_networks[0] = (net_scan_entry_t){"Office-WiFi", NET_SEC_PASSWORD, -52};
    fake_networks[1] = (net_scan_entry_t){"Office-Corp", NET_SEC_WORK_LOGIN, -60};
    fake_networks[2] = (net_scan_entry_t){"Office-Guest", NET_SEC_OPEN, -63};
    fake_networks[3] = (net_scan_entry_t){"Printer-Direct", NET_SEC_PASSWORD, -84};
    fake_n_networks = 4;
    memset(&fake_join, 0, sizeof fake_join);
    fake_join_ok = true;
    fake_join_calls = 0;
    fake_join_ssid[0] = fake_join_user[0] = fake_join_pass[0] = fake_join_cal[0] = '\0';
    fake_cal_put_calls = 0;
    fake_cal_put_from_setup = false;
    fake_cal_sync_result = 0;
    memset(&fake_items, 0, sizeof fake_items);
    fake_cal_add_calls = fake_cal_edit_calls = fake_cal_remove_calls = 0;
    fake_cal_last_url[0] = fake_cal_last_name[0] = fake_cal_last_tag[0] = '\0';
    fake_cal_last_id = 0;
    fake_mac_time_calls = 0;
    fake_mac_time = 0;
    fake_mac_time_accept = true;
    fake_tokens_saves = 0;
    memset(&fake_jira, 0, sizeof fake_jira);
}

void fake_tokens_clear(void)
{
    s_tokens_n = 0;
}

void net_port_device_id(char out[13]) { memcpy(out, "f412fa3f2a1c", 13); }
const char *net_port_fw_version(void) { return "1.0.0"; }
tb_clock_t net_port_now(void) { return fake_now; }
net_time_source_t net_port_time_source(void) { return fake_time_source; }

bool net_port_set_time_from_mac(tb_epoch_t t)
{
    fake_mac_time_calls++;
    fake_mac_time = t;
    if (!fake_mac_time_accept) return false;
    fake_now.wall = t;
    fake_now.valid = true;
    fake_time_source = NET_TIME_MAC;
    return true;
}

bool net_port_time_zone_known(const char *iana)
{
    static const char *const known[] = {"America/Los_Angeles", "America/New_York", "Europe/Berlin", "UTC"};
    for (size_t i = 0; iana && i < sizeof known / sizeof known[0]; i++)
        if (!strcmp(iana, known[i])) return true;
    return false;
}

void net_port_wifi(net_wifi_info_t *out) { *out = fake_wifi; }

int net_port_setup_networks(net_scan_entry_t *out, int max)
{
    int n = fake_n_networks < max ? fake_n_networks : max;
    memcpy(out, fake_networks, (size_t)n * sizeof *out);
    return n;
}

bool net_port_setup_join(const char *ssid, const char *u, const char *p, const char *cal)
{
    fake_join_calls++;
    tb_strlcpy(fake_join_ssid, ssid, sizeof fake_join_ssid);
    tb_strlcpy(fake_join_user, u ? u : "", sizeof fake_join_user);
    tb_strlcpy(fake_join_pass, p ? p : "", sizeof fake_join_pass);
    tb_strlcpy(fake_join_cal, cal ? cal : "", sizeof fake_join_cal);
    if (fake_join_ok) fake_join.state = NET_JOIN_CONNECTING;
    return fake_join_ok;
}

void net_port_setup_status(net_join_status_t *out) { *out = fake_join; }

/* A small stand-in for cal_url_check(), so the router's tests don't depend on the calendar builder's work. */
int net_port_cal_check(const char *url)
{
    if (!url || !*url) return CAL_URL_EMPTY;
    if (strlen(url) > CAL_URL_MAX) return CAL_URL_TOO_LONG;
    if (!strncmp(url, "http://", 7)) return CAL_URL_HTTP;
    if (strncmp(url, "https://", 8) && strncmp(url, "webcal://", 9)) return CAL_URL_NOT_A_URL;
    if (strstr(url, "/public/")) return CAL_URL_PUBLIC;
    size_t n = strlen(url);
    if (n < 4 || strcmp(url + n - 4, ".ics")) return CAL_URL_NOT_ICS;
    return CAL_URL_OK;
}

int net_port_cal_put(const char *url, bool from_setup)
{
    int e = net_port_cal_check(url);
    if (e) return e;
    fake_cal_put_calls++;
    fake_cal_put_from_setup = from_setup;
    fake_cal.check = CAL_CHECK_CHECKING;
    fake_cal.syncing = true;
    return 0;
}

int net_port_cal_remove(void)
{
    if (!fake_cal.saved) return -1;
    memset(&fake_cal, 0, sizeof fake_cal);
    return 0;
}

int net_port_cal_sync_now(void)
{
    if (!fake_cal.saved) return -1;
    if (fake_cal_sync_result) return fake_cal_sync_result;
    fake_cal.syncing = true;
    return 0;
}

void net_port_cal_status(cal_status_t *out) { *out = fake_cal; }

void net_port_cal_items(cal_items_t *out) { *out = fake_items; }

/* The fake's list as the pure list logic (cal_list.h) sees it, so names and tags are checked by the real rules. */
static void fake_list(cal_list_t *l)
{
    cal_list_init(l);
    for (int i = 0; i < TB_CALS_MAX; i++) {
        if (!fake_items.c[i].used) continue;
        l->c[i].used = true;
        snprintf(l->c[i].name, sizeof l->c[i].name, "%s", fake_items.c[i].name);
        snprintf(l->c[i].tag, sizeof l->c[i].tag, "%s", fake_items.c[i].tag);
    }
}

cal_res_t net_port_cal_add(const char *url, const char *name, const char *tag)
{
    cal_res_t res = {net_port_cal_check(url), 0};
    if (res.url_err) return res;
    cal_list_t l;
    fake_list(&l);
    res.list_err = (int)cal_list_add(&l, name, tag, NULL);
    if (res.list_err) return res;
    fake_cal_add_calls++;
    snprintf(fake_cal_last_url, sizeof fake_cal_last_url, "%s", url);
    snprintf(fake_cal_last_name, sizeof fake_cal_last_name, "%s", name ? name : "");
    snprintf(fake_cal_last_tag, sizeof fake_cal_last_tag, "%s", tag ? tag : "");
    fake_cal.check = CAL_CHECK_CHECKING;
    fake_cal.check_id = 0;
    return res;
}

cal_res_t net_port_cal_edit(int id, const char *url, const char *name, const char *tag)
{
    cal_res_t res = {0, 0};
    int slot = id - 1;
    if (slot < 0 || slot >= TB_CALS_MAX || !fake_items.c[slot].used) { res.list_err = CAL_LIST_NO_SUCH; return res; }
    if (url && url[0] && (res.url_err = net_port_cal_check(url)) != 0) return res;
    cal_list_t l;
    fake_list(&l);
    res.list_err = (int)cal_list_edit(&l, slot, name, tag);
    if (res.list_err) return res;
    fake_cal_edit_calls++;
    fake_cal_last_id = id;
    snprintf(fake_cal_last_url, sizeof fake_cal_last_url, "%s", url ? url : "");
    if (url && url[0]) {
        fake_cal.check = CAL_CHECK_CHECKING;
        fake_cal.check_id = id;
    } else {
        snprintf(fake_items.c[slot].name, sizeof fake_items.c[slot].name, "%s", l.c[slot].name);
        snprintf(fake_items.c[slot].tag, sizeof fake_items.c[slot].tag, "%s", l.c[slot].tag);
    }
    return res;
}

int net_port_cal_remove_id(int id)
{
    int slot = id - 1;
    if (slot < 0 || slot >= TB_CALS_MAX || !fake_items.c[slot].used) return -1;
    memset(&fake_items.c[slot], 0, sizeof fake_items.c[slot]);
    fake_items.n--;
    fake_cal_remove_calls++;
    return 0;
}

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
    /* Not SHA-256: a deterministic stand-in that spreads bits (FNV-1a in 8 lanes). Tests check behavior, not digests;
     * the device uses mbedTLS. */
    const uint8_t *p = data;
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
    fake_tokens_saves++;
    return true;
}

size_t net_port_tokens_load(void *blob, size_t cap)
{
    size_t n = s_tokens_n < cap ? s_tokens_n : cap;
    memcpy(blob, s_tokens, n);
    return n;
}

/* ---------- jira_service.h, faked: the real rules (jira_resolve) over a state the tests control ---------- */

void jira_get_info(jira_info_t *out)
{
    memset(out, 0, sizeof *out);
    out->alert_above = -1;
    out->count = -1;
    out->test.state = fake_jira.test_state;
    snprintf(out->test.name, sizeof out->test.name, "%s", fake_jira.test_name);
    out->test.count = fake_jira.test_count;
    out->test.error = fake_jira.test_error;
    if (!fake_jira.saved) return;
    const jira_cfg_t *c = &fake_jira.cfg;
    out->configured = true;
    snprintf(out->site, sizeof out->site, "%s", c->site);
    jira_email_hint(c->email, out->email_hint, sizeof out->email_hint);
    out->token_saved = c->token[0] != '\0';
    snprintf(out->filter_id, sizeof out->filter_id, "%s", c->filter);
    snprintf(out->filter_name, sizeof out->filter_name, "%s", fake_jira.filter_name);
    snprintf(out->label, sizeof out->label, "%s", c->label);
    out->alert_above = c->alert_above;
    out->state = fake_jira.state;
    out->count = fake_jira.count;
    out->updated_at = fake_jira.updated_at;
}

jira_err_t jira_save(const jira_input_t *in)
{
    jira_cfg_t c;
    jira_err_t e = jira_resolve(fake_jira.saved ? &fake_jira.cfg : NULL, in, &c);
    if (e != JIRA_OK) return e;
    fake_jira.save_calls++;
    fake_jira.saved = true;
    fake_jira.cfg = c;
    fake_jira.state = TB_JIRA_LOADING;
    fake_jira.count = -1;
    fake_jira.updated_at = 0;
    return JIRA_OK;
}

jira_err_t jira_test(const jira_input_t *in)
{
    jira_cfg_t c;
    jira_err_t e = jira_resolve(fake_jira.saved ? &fake_jira.cfg : NULL, in, &c);
    if (e != JIRA_OK) return e;
    fake_jira.test_calls++;
    fake_jira.last_test_cfg = c;
    fake_jira.test_state = JIRA_TEST_ASKING;
    return JIRA_OK;
}

bool jira_remove(void)
{
    bool was = fake_jira.saved;
    int saves = fake_jira.save_calls, tests = fake_jira.test_calls, removes = fake_jira.remove_calls + 1;
    memset(&fake_jira, 0, sizeof fake_jira);         /* the token and every setting go */
    fake_jira.save_calls = saves;
    fake_jira.test_calls = tests;
    fake_jira.remove_calls = removes;
    return was;
}
