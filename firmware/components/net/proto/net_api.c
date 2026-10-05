/*
 * net_api.c: the router (docs/api.md). Owner: net builder.
 *
 * One router for HTTP and USB: net_api_handle() takes a request from the HTTP server, net_api_usb_line() takes a
 * "@tb " line; both end in route(), which finds the endpoint, checks the token and scope, and runs its handler.
 * Handlers fill r->o, the reply object, which over USB already starts with "id" (and "http_status" for request).
 * Everything here runs on the app task (tb_bus_exec), so it may read and change the core model directly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "cal_url.h"
#include "net_api.h"
#include "net_macs.h"
#include "net_pair.h"
#include "net_port.h"
#include "net_util.h"
#include "tb_fmt.h"
#include "tb_text.h"

/* cJSON's own nesting limit (1000 by default) must be the build's (CMakeLists.txt sets it for every component, the
 * json one included; test/host/CMakeLists.txt for the host build): a 1000-level parse or cJSON_Delete would run the
 * app task's 12 KB stack out. net_json_depth_ok() checks the same limit before cJSON sees a request. */
_Static_assert(CJSON_NESTING_LIMIT == NET_JSON_DEPTH_MAX,
               "CJSON_NESTING_LIMIT must be NET_JSON_DEPTH_MAX for the whole build (CMakeLists.txt)");

static tb_app_t *s_app;
static net_macs_t s_macs;
static net_pair_t s_pair;
static net_rate_t s_rate;
static bool s_bearer = true;
static tb_call_t s_pushed;              /* the call last handed to core */
static uint32_t s_rev_base, s_rev_n, s_rev_sig;
static bool s_rev_sig_set;

/* ======================================================================================================== */
/* Set-up                                                                                                   */
/* ======================================================================================================== */

void net_api_bind(tb_app_t *app)
{
    s_app = app;
}

static const char *kind_label(net_kind_t k);

/* Tell core how many devices are paired and their names, most recently used first ("iPhone, Desk script, Mac"), for
 * the Wi-Fi menu's Devices tile and Forget all (mock-up showForgetMenu: by last use, then by when paired). */
static void paired_changed(void)
{
    if (!s_app) return;
    const net_token_t *order[NET_TOKENS_MAX];
    int n = 0;
    for (int i = 0; i < NET_TOKENS_MAX; i++)
        if (s_pair.tokens[i].used) order[n++] = &s_pair.tokens[i];
    for (int i = 1; i < n; i++) {     /* insertion sort: at most 10 */
        const net_token_t *t = order[i];
        int k = i;
        while (k > 0 && (order[k - 1]->last_used < t->last_used ||
                         (order[k - 1]->last_used == t->last_used && order[k - 1]->paired_at < t->paired_at))) {
            order[k] = order[k - 1];
            k--;
        }
        order[k] = t;
    }
    char names[TB_PAIRED_NAMES_BYTES] = "";
    size_t len = 0;
    for (int i = 0; i < n; i++) {
        const char *name = order[i]->name[0] ? order[i]->name : kind_label(order[i]->kind);
        size_t need = strlen(name) + (i ? 2 : 0);
        if (len + need >= sizeof names) break;      /* the tile shows two lines at most; the rest wouldn't show */
        if (i) memcpy(names + len, ", ", 2), len += 2;
        memcpy(names + len, name, strlen(name) + 1);
        len += strlen(name);
    }
    tb_app_set_paired(s_app, (uint8_t)n, names);
}

void net_api_init(void)
{
    net_macs_init(&s_macs);
    net_pair_init(&s_pair);
    net_rate_init(&s_rate);
    memset(&s_pushed, 0, sizeof s_pushed);
    uint32_t r = 0;
    net_port_random(&r, sizeof r);
    s_rev_base = 1000 + r % 900000;     /* a different start each boot, so an old ETag doesn't match by chance */
    s_rev_n = 0;
    s_rev_sig_set = false;
    paired_changed();
}

void net_api_set_auth(bool bearer)
{
    s_bearer = bearer;
}

bool net_api_auth_bearer(void)
{
    return s_bearer;
}

/* ======================================================================================================== */
/* The request in progress                                                                                  */
/* ======================================================================================================== */

typedef struct {
    const net_req_t *req;
    net_resp_t *resp;
    net_via_t via;
    tb_clock_t now;
    cJSON *o;                   /* the reply object */
    int prefix;                 /* how many of o's first members are the USB prefix (id, http_status) */
    int status;
    const net_token_t *tok;     /* the valid token the request came with, or NULL */
    bool from_cookie;
    char param[24];             /* clients/{token_id} (copied: the path buffer is gone by the handler) */
} rt_t;

/* ---------- JSON output ---------- */

static void add_str(cJSON *o, const char *k, const char *v)
{
    if (v) cJSON_AddStringToObject(o, k, v);
    else cJSON_AddNullToObject(o, k);
}

/* A string that's "" when absent becomes null. */
static void add_str0(cJSON *o, const char *k, const char *v)
{
    add_str(o, k, v && v[0] ? v : NULL);
}

static void add_time(cJSON *o, const char *k, tb_epoch_t t)
{
    char b[40];
    if (t > 0) cJSON_AddStringToObject(o, k, tb_fmt_rfc3339(b, sizeof b, t));
    else cJSON_AddNullToObject(o, k);
}

static void add_now(rt_t *r, cJSON *o, const char *k)
{
    add_time(o, k, r->now.valid ? r->now.wall : 0);
}

/* How many bytes j takes as the reply prints it (0 if out of memory, which the reply's own print then reports). */
static size_t printed_len(const cJSON *j)
{
    char *s = cJSON_PrintUnformatted(j);
    size_t n = s ? strlen(s) : 0;
    cJSON_free(s);
    return n;
}

/* Remove what a handler added after the prefix (before an error replaces it). */
static void reset_body(rt_t *r)
{
    cJSON *c = r->o->child;
    for (int i = 0; c && i < r->prefix; i++) c = c->next;
    while (c) {
        cJSON *next = c->next;
        cJSON_Delete(cJSON_DetachItemViaPointer(r->o, c));
        c = next;
    }
}

static void fail(rt_t *r, int status, const char *code, const char *msg, const char *field)
{
    reset_body(r);
    r->status = status;
    cJSON_AddBoolToObject(r->o, "ok", false);
    cJSON_AddStringToObject(r->o, "error", code);
    cJSON_AddStringToObject(r->o, "message", msg);
    add_str(r->o, "field", field);
}

static void fail_retry(rt_t *r, int status, const char *code, const char *msg, const char *field, int retry_s)
{
    fail(r, status, code, msg, field);
    if (retry_s < 1) retry_s = 1;
    cJSON_AddNumberToObject(r->o, "retry_after_s", retry_s);
    r->resp->retry_after_s = retry_s;
}

static void bad_request(rt_t *r, const char *field, const char *msg) { fail(r, 400, "bad_request", msg, field); }
static void bad_value(rt_t *r, const char *field, const char *msg) { fail(r, 400, "bad_value", msg, field); }

static void ok(rt_t *r, int status)
{
    r->status = status;
    cJSON_AddBoolToObject(r->o, "ok", true);
}

/* ---------- JSON input ---------- */

typedef enum { F_ABSENT = 0, F_OK, F_TYPE, F_RANGE } fstat_t;

static const cJSON *item(const cJSON *o, const char *k)
{
    return o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
}

/* An integer in [lo, hi]. null counts as absent. A fraction or a value out of range is F_RANGE. */
static fstat_t get_int(const cJSON *o, const char *k, double lo, double hi, int64_t *out)
{
    const cJSON *j = item(o, k);
    if (!j || cJSON_IsNull(j)) return F_ABSENT;
    if (!cJSON_IsNumber(j)) return F_TYPE;
    double d = j->valuedouble;
    if (!(d >= lo && d <= hi)) return F_RANGE;
    if ((double)(int64_t)d != d) return F_RANGE;
    *out = (int64_t)d;
    return F_OK;
}

static fstat_t get_bool(const cJSON *o, const char *k, bool *out)
{
    const cJSON *j = item(o, k);
    if (!j || cJSON_IsNull(j)) return F_ABSENT;
    if (!cJSON_IsBool(j)) return F_TYPE;
    *out = cJSON_IsTrue(j);
    return F_OK;
}

static fstat_t get_str(const cJSON *o, const char *k, const char **out)
{
    const cJSON *j = item(o, k);
    if (!j || cJSON_IsNull(j)) return F_ABSENT;
    if (!cJSON_IsString(j) || !j->valuestring) return F_TYPE;
    *out = j->valuestring;
    return F_OK;
}

/* set_aside (api.md 8): true when left out or null. */
static bool get_set_aside(rt_t *r, const cJSON *b, bool *out)
{
    *out = true;
    if (get_bool(b, "set_aside", out) == F_TYPE) {
        bad_request(r, "set_aside", "\"set_aside\" must be true or false.");
        return false;
    }
    return true;
}

/* 8 to 64 of A-Z a-z 0-9 - (api.md 5.1 client). */
static bool is_client_id(const char *s)
{
    size_t n = strlen(s);
    if (n < 8 || n > 64) return false;
    for (const char *p = s; *p; p++)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-'))
            return false;
    return true;
}

static bool is_session(const char *s)
{
    size_t n = strlen(s);
    if (n < 1 || n > 16) return false;
    for (const char *p = s; *p; p++)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))) return false;
    return true;
}

static bool get_client(rt_t *r, const cJSON *b, const char **out)
{
    fstat_t f = get_str(b, "client", out);
    if (f == F_ABSENT || f == F_TYPE) {
        bad_request(r, "client", "\"client\" is required: this app install's ID.");
        return false;
    }
    if (!is_client_id(*out)) {
        bad_value(r, "client", "\"client\" must be 8 to 64 letters, digits or dashes.");
        return false;
    }
    return true;
}

/* Text that reaches the bar (api.md 2.3): cleaned, then its length checked. Returns the length in characters. */
static size_t clean(char *dst, size_t cap, const char *src)
{
    return tb_text_clean(dst, cap, src);
}

static void utf8_encode(uint32_t c, char u[5])
{
    memset(u, 0, 5);
    if (c < 0x80) {
        u[0] = (char)c;
    } else if (c < 0x800) {
        u[0] = (char)(0xC0 | c >> 6);
        u[1] = (char)(0x80 | (c & 63));
    } else if (c < 0x10000) {
        u[0] = (char)(0xE0 | c >> 12);
        u[1] = (char)(0x80 | ((c >> 6) & 63));
        u[2] = (char)(0x80 | (c & 63));
    } else {
        u[0] = (char)(0xF0 | c >> 18);
        u[1] = (char)(0x80 | ((c >> 12) & 63));
        u[2] = (char)(0x80 | ((c >> 6) & 63));
        u[3] = (char)(0x80 | (c & 63));
    }
}

/* 400 unsupported_chars with the characters the fonts can't draw. Returns true if it answered. */
static bool refuse_unsupported(rt_t *r, const char *text, const char *field)
{
    uint32_t cps[8];
    int n = tb_text_unsupported(text, cps, 8);
    if (!n) return false;
    fail(r, 400, "unsupported_chars", "MiniBar can't show some of these characters.", field);
    cJSON *a = cJSON_AddArrayToObject(r->o, "chars");
    for (int i = 0; i < n && i < 8; i++) {
        char u[5];
        utf8_encode(cps[i], u);
        cJSON_AddItemToArray(a, cJSON_CreateString(u));
    }
    return true;
}

/* ======================================================================================================== */
/* Host and origin (api.md 2.2)                                                                             */
/* ======================================================================================================== */

static bool host_eq(const char *host, const char *name)
{
    if (!name || !name[0]) return false;
    size_t n = strlen(name);
    for (size_t i = 0; i < n; i++) {
        char a = host[i], b = name[i];
        if (!a) return false;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    const char *rest = host + n;
    if (*rest == '.' && name[n - 1] != '.') rest++;     /* a fully qualified name's final dot */
    return !*rest || !strcmp(rest, ":80");
}

bool net_api_host_ok(const char *host)
{
    if (!host || !host[0]) return false;
    net_wifi_info_t w;
    net_port_wifi(&w);
    if (host_eq(host, w.host)) return true;
    if (w.sta_up && host_eq(host, w.ip)) return true;
    if (w.state == NET_WIFI_SETUP && host_eq(host, NET_SETUP_IP)) return true;
    return false;
}

static bool origin_ok(const char *origin)
{
    if (strncmp(origin, "http://", 7)) return false;
    return net_api_host_ok(origin + 7);
}

/* ======================================================================================================== */
/* The Macs and core                                                                                        */
/* ======================================================================================================== */

static bool same_call(const tb_call_t *a, const tb_call_t *b)
{
    return a->active == b->active && a->id == b->id && a->via == b->via && a->since_ms == b->since_ms &&
           a->since == b->since && !strcmp(a->app, b->app);
}

/* Hand the Mac table's sum to core when it changed: the call (lead names why it ended, or NULL) and the icon.
 * Returns true when core announced a call's end with lead ("Removed Mac · back to Busy"), so the caller doesn't toast
 * the same thing again a moment later. */
static bool push_macs(const char *lead, const tb_clock_t *now)
{
    if (!s_app) return false;
    bool said = false;
    tb_call_t agg;
    tb_link_t link;
    net_macs_aggregate(&s_macs, &agg, &link, NULL);
    if (!same_call(&agg, &s_pushed)) {
        s_pushed = agg;
        said = tb_app_set_call(s_app, agg.active ? &agg : NULL, agg.active ? NULL : lead, now);
    }
    if (link != s_app->mac_link) tb_app_set_mac_link(s_app, link, now);
    return said;
}

static const char *link_name(tb_link_t l)
{
    return l == TB_LINK_USB ? "usb" : l == TB_LINK_WIFI ? "wifi" : NULL;
}

/* ======================================================================================================== */
/* Objects shared by several replies                                                                        */
/* ======================================================================================================== */

static const char *const STATUS_IDS[TB_ST_COUNT] = {"available", "busy", "meeting", "pomodoro", "away", "message", "clock"};
static const char *const PHASE_IDS[] = {"focus", "short", "long"};
static const char *const POMO_STATES[] = {"ready", "running", "paused", "waiting"};

static const char *wifi_word(net_wifi_state_t s)
{
    return s == NET_WIFI_CONNECTED ? "connected" : s == NET_WIFI_SETUP ? "setup" : "offline";
}

static const char *bar_host(const net_wifi_info_t *w)
{
    return w->host[0] ? w->host : "minibar.local";
}

/* GET /api/v1/info and the USB hello reply (api.md 7.1). */
static void add_info(rt_t *r)
{
    char id[13];
    net_port_device_id(id);
    net_wifi_info_t w;
    net_port_wifi(&w);
    static const char *const src[] = {"none", "ntp", "rtc", "mac"};
    net_time_source_t ts = net_port_time_source();
    ok(r, 200);
    cJSON_AddStringToObject(r->o, "device", "MiniBar");
    cJSON_AddStringToObject(r->o, "device_id", id);
    cJSON_AddStringToObject(r->o, "name", s_app ? s_app->set.device.name : "MiniBar");
    cJSON_AddStringToObject(r->o, "fw", net_port_fw_version());
    cJSON_AddStringToObject(r->o, "api", NET_API_VERSION);
    cJSON_AddStringToObject(r->o, "host", bar_host(&w));
    cJSON_AddStringToObject(r->o, "auth", s_bearer ? "bearer" : "none");
    const char *pairing = net_pair_state(&s_pair, &r->now);
    cJSON_AddStringToObject(r->o, "pairing", pairing);
    /* The number of the code on the screen, counted from start-up, null when none (api.md 7.1): the device that asked
     * compares it with the one pair/start gave it, so its prompt can tell its own code from the next device's within
     * one poll, without anyone learning the pairing_id (the mock-up's remotePoll() compares pair.id === phone.pid). */
    if (!strcmp(pairing, "showing")) cJSON_AddNumberToObject(r->o, "pairing_seq", s_pair.seq);
    else cJSON_AddNullToObject(r->o, "pairing_seq");
    /* How many devices are paired (api.md 7.1, firmware alignment): a phone that isn't paired yet reads it to clear
     * "already has 10 paired devices" once a place is free, and to tell Forget all from a removal after a 401. */
    cJSON_AddNumberToObject(r->o, "paired", net_pair_count(&s_pair));
    cJSON_AddNumberToObject(r->o, "heartbeat_s", NET_MAC_HEARTBEAT_S);
    cJSON_AddNumberToObject(r->o, "timeout_s", NET_MAC_TIMEOUT_MS / 1000);
    add_now(r, r->o, "time");
    cJSON_AddStringToObject(r->o, "time_source", r->now.valid ? src[ts <= NET_TIME_MAC ? ts : 0] : "none");
    cJSON_AddStringToObject(r->o, "wifi", wifi_word(w.state));
}

/* status.call and the call reply's call (api.md 7.3): from the Mac table, whether or not Calls from your Mac is on. */
static cJSON *call_obj(void)
{
    tb_call_t agg;
    const net_mac_t *m = NULL;
    net_macs_aggregate(&s_macs, &agg, NULL, &m);
    cJSON *c = cJSON_CreateObject();
    cJSON_AddBoolToObject(c, "active", agg.active);
    add_str0(c, "app", agg.active ? agg.app : NULL);
    if (agg.active && m && m->inputs >= 0) {
        cJSON *in = cJSON_AddArrayToObject(c, "inputs");
        if (m->inputs & NET_INPUT_MIC) cJSON_AddItemToArray(in, cJSON_CreateString("mic"));
        if (m->inputs & NET_INPUT_CAMERA) cJSON_AddItemToArray(in, cJSON_CreateString("camera"));
    } else {
        cJSON_AddNullToObject(c, "inputs");
    }
    add_str(c, "via", agg.active ? link_name(agg.via) : NULL);
    add_time(c, "since", agg.active ? agg.since : 0);
    bool aside = agg.active && s_app && s_app->call.active && s_app->aside_call && s_app->aside_call == s_app->call.id;
    cJSON_AddBoolToObject(c, "aside", aside);
    return c;
}

static cJSON *sources_obj(void)
{
    cJSON *s = cJSON_CreateObject();
    cJSON_AddBoolToObject(s, "calendar", s_app && s_app->set.automatic.calendar);
    cJSON_AddBoolToObject(s, "mac", s_app && s_app->set.automatic.mac);
    return s;
}

/* {start, end, title, location}: title and location only when Show meeting titles is on and it isn't private. */
static cJSON *meeting_obj(const tb_meeting_t *m)
{
    if (!m) return cJSON_CreateNull();
    cJSON *o = cJSON_CreateObject();
    add_time(o, "start", m->start);
    add_time(o, "end", m->end);
    add_str0(o, "title", tb_app_title_of(s_app, m));
    add_str0(o, "location", tb_app_place_of(s_app, m));
    return o;
}

static cJSON *macs_arr(void)
{
    const net_mac_t *ms[NET_MACS_MAX];
    int n = net_macs_sorted(&s_macs, ms, NET_MACS_MAX);
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "client", ms[i]->client);
        cJSON_AddStringToObject(o, "name", ms[i]->name[0] ? ms[i]->name : "Mac");
        add_str(o, "via", link_name(ms[i]->via));
        cJSON_AddBoolToObject(o, "connected", ms[i]->connected);
        cJSON_AddBoolToObject(o, "active", ms[i]->active);
        add_time(o, "last_heard", ms[i]->last_heard);
        cJSON_AddItemToArray(a, o);
    }
    return a;
}

/* ---------- rev: the status object's changes, without its clock and countdowns ---------- */

static bool volatile_key(const char *k)
{
    static const char *const keys[] = {"rev", "time", "remaining_s", "ends_at", "focused_today_s", "last_heard", "rssi"};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
        if (!strcmp(k, keys[i])) return true;
    return false;
}

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static uint32_t hash_json(const cJSON *j, uint32_t h, int skip)
{
    const cJSON *c = j->child;
    for (int i = 0; c && i < skip; i++) c = c->next;     /* the USB prefix (id, http_status) */
    for (; c; c = c->next) {
        if (c->string) {
            if (volatile_key(c->string)) continue;
            h = fnv(h, c->string, strlen(c->string) + 1);
        }
        int type = c->type & 0xFF;
        h = fnv(h, &type, sizeof type);
        if (cJSON_IsString(c) && c->valuestring) h = fnv(h, c->valuestring, strlen(c->valuestring) + 1);
        else if (cJSON_IsNumber(c)) h = fnv(h, &c->valuedouble, sizeof c->valuedouble);
        else if (cJSON_IsArray(c) || cJSON_IsObject(c)) h = hash_json(c, h, 0) ^ 0x5bd1e995u;
    }
    return h;
}

/* GET /api/v1/status, the USB status, and the reply of every change in sections 8 and 9 (api.md 7.3). */
static uint32_t add_status(rt_t *r)
{
    tb_app_t *a = s_app;
    const tb_clock_t *now = &r->now;
    char id[13];
    net_port_device_id(id);
    static const char *const src[] = {"none", "ntp", "rtc", "mac"};
    net_time_source_t ts = net_port_time_source();
    net_wifi_info_t w;
    net_port_wifi(&w);
    cal_status_t cs;
    net_port_cal_status(&cs);

    ok(r, 200);
    cJSON *o = r->o;
    cJSON_AddStringToObject(o, "device_id", id);
    cJSON *rev = cJSON_AddNumberToObject(o, "rev", 0);
    add_now(r, o, "time");
    cJSON_AddStringToObject(o, "time_source", now->valid ? src[ts <= NET_TIME_MAC ? ts : 0] : "none");
    static const char *const showing[] = {"own", "call", "meeting", "setup"};
    cJSON_AddStringToObject(o, "showing", showing[tb_app_showing(a, now)]);
    cJSON_AddStringToObject(o, "screen", a->off ? "dark" : "on");

    cJSON *own = cJSON_AddObjectToObject(o, "own");
    cJSON_AddStringToObject(own, "status", STATUS_IDS[a->idx < TB_ST_COUNT ? a->idx : TB_ST_CLOCK]);
    add_time(own, "since", a->since);
    cJSON_AddStringToObject(own, "previous", STATUS_IDS[a->last_status < TB_ST_COUNT ? a->last_status : TB_ST_AVAILABLE]);

    cJSON *msg = cJSON_AddObjectToObject(o, "message");
    add_str0(msg, "text", a->message);
    add_time(msg, "set_at", a->message[0] ? a->message_at : 0);

    cJSON *away = cJSON_AddObjectToObject(o, "away");
    add_str0(away, "back_at", a->away_back_at);
    add_str0(away, "note", a->away_note);

    cJSON_AddItemToObject(o, "call", call_obj());

    cJSON *mt = cJSON_AddObjectToObject(o, "meeting");
    if (a->cal_saved && a->wifi_mode != TB_WIFI_OFFLINE) {
        const tb_meeting_t *cur = tb_app_current_meeting(a, now);
        cJSON_AddBoolToObject(mt, "active", cur != NULL);
        cJSON_AddBoolToObject(mt, "aside", cur && a->aside_meeting == cur->id);
        cJSON_AddItemToObject(mt, "current", meeting_obj(cur));
        cJSON_AddItemToObject(mt, "next", meeting_obj(tb_app_next_meeting(a, now)));
        cJSON_AddNumberToObject(mt, "left_today", tb_app_meetings_left(a, now));
    } else {
        cJSON_AddBoolToObject(mt, "active", false);
        cJSON_AddBoolToObject(mt, "aside", false);
        cJSON_AddNullToObject(mt, "current");
        cJSON_AddNullToObject(mt, "next");
        cJSON_AddNullToObject(mt, "left_today");
    }

    cJSON *p = cJSON_AddObjectToObject(o, "pomodoro");
    tb_pomo_state_t st = tb_pomo_state(&a->pomo, &a->set);
    cJSON_AddStringToObject(p, "state", POMO_STATES[st]);
    cJSON_AddStringToObject(p, "phase", PHASE_IDS[a->pomo.phase <= TB_PH_LONG ? a->pomo.phase : 0]);
    cJSON_AddNumberToObject(p, "round", a->pomo.round);
    cJSON_AddNumberToObject(p, "rounds", a->set.pomodoro.long_every);
    cJSON_AddNumberToObject(p, "length_s", (double)(tb_pomo_phase_len(&a->set, a->pomo.phase) / 1000));
    int32_t rem = tb_pomo_remaining_s(&a->pomo);
    cJSON_AddNumberToObject(p, "remaining_s", rem);
    add_time(p, "ends_at", st == TB_POMO_ST_RUNNING && now->valid ? now->wall + rem : 0);
    if (st == TB_POMO_ST_PAUSED) {
        tb_auto_t by = tb_app_paused_by(a, now);
        cJSON_AddStringToObject(p, "paused_by", by == TB_AUTO_CALL ? "call" : by == TB_AUTO_MEETING ? "meeting" : "you");
    } else {
        cJSON_AddNullToObject(p, "paused_by");
    }
    cJSON_AddBoolToObject(p, "ringing", a->ringing);
    cJSON_AddNumberToObject(p, "done_today", a->pomo.done_today);
    cJSON_AddNumberToObject(p, "focused_today_s", (double)(a->pomo.focused_ms / 1000));

    cJSON_AddItemToObject(o, "sources", sources_obj());
    cJSON_AddItemToObject(o, "macs", macs_arr());

    cJSON *cal = cJSON_AddObjectToObject(o, "calendar");
    cJSON_AddBoolToObject(cal, "saved", cs.saved);
    add_time(cal, "last_sync", cs.saved ? cs.last_sync : 0);
    cJSON_AddBoolToObject(cal, "syncing", cs.syncing);
    add_str(cal, "error", cs.error);

    cJSON *wo = cJSON_AddObjectToObject(o, "wifi");
    cJSON_AddStringToObject(wo, "state", wifi_word(w.state));
    add_str0(wo, "ssid", w.sta_up ? w.ssid : NULL);
    add_str0(wo, "ip", w.sta_up ? w.ip : NULL);
    add_str0(wo, "host", w.sta_up ? bar_host(&w) : NULL);
    if (w.sta_up && w.rssi) cJSON_AddNumberToObject(wo, "rssi", w.rssi);
    else cJSON_AddNullToObject(wo, "rssi");

    /* rev: the boot's base plus the number of changes seen (the prefix and volatile fields don't count) */
    uint32_t sig = hash_json(o, 2166136261u, r->prefix);
    if (!s_rev_sig_set || sig != s_rev_sig) {
        if (s_rev_sig_set) s_rev_n++;
        s_rev_sig = sig;
        s_rev_sig_set = true;
    }
    uint32_t v = s_rev_base + s_rev_n;
    cJSON_SetNumberValue(rev, v);
    return v;
}

/* The reply of POST /api/v1/call (api.md 5.3). */
static void add_call_reply(rt_t *r, bool stale)
{
    char id[13];
    net_port_device_id(id);
    static const char *const showing[] = {"own", "call", "meeting", "setup"};
    ok(r, 200);
    cJSON_AddStringToObject(r->o, "device_id", id);
    cJSON_AddStringToObject(r->o, "showing", showing[tb_app_showing(s_app, &r->now)]);
    cJSON_AddStringToObject(r->o, "screen", s_app->off ? "dark" : "on");
    cJSON_AddBoolToObject(r->o, "stale", stale);
    cJSON_AddItemToObject(r->o, "call", call_obj());
    cJSON_AddItemToObject(r->o, "sources", sources_obj());
    cJSON_AddNumberToObject(r->o, "heartbeat_s", NET_MAC_HEARTBEAT_S);
    cJSON_AddNumberToObject(r->o, "timeout_s", NET_MAC_TIMEOUT_MS / 1000);
    add_now(r, r->o, "time");
}

static void add_settings(rt_t *r)
{
    const tb_settings_t *s = &s_app->set;
    ok(r, 200);
    cJSON *so = cJSON_AddObjectToObject(r->o, "settings");
    cJSON *p = cJSON_AddObjectToObject(so, "pomodoro");
    cJSON_AddNumberToObject(p, "focus_min", s->pomodoro.focus_min);
    cJSON_AddNumberToObject(p, "short_min", s->pomodoro.short_min);
    cJSON_AddNumberToObject(p, "long_min", s->pomodoro.long_min);
    cJSON_AddNumberToObject(p, "long_every", s->pomodoro.long_every);
    cJSON_AddBoolToObject(p, "auto_start", s->pomodoro.auto_start);
    cJSON_AddBoolToObject(p, "chime", s->pomodoro.chime);
    cJSON_AddBoolToObject(p, "ticking", s->pomodoro.ticking);
    cJSON_AddStringToObject(p, "tick_volume", s->pomodoro.tick_volume == TB_TICK_MEDIUM ? "medium" : "soft");
    cJSON *d = cJSON_AddObjectToObject(so, "display");
    cJSON_AddNumberToObject(d, "brightness", s->display.brightness);
    cJSON *au = cJSON_AddObjectToObject(so, "automatic");
    cJSON_AddBoolToObject(au, "calendar", s->automatic.calendar);
    cJSON_AddBoolToObject(au, "mac", s->automatic.mac);
    cJSON_AddBoolToObject(au, "meeting_titles", s->automatic.meeting_titles);
    cJSON *dev = cJSON_AddObjectToObject(so, "device");
    cJSON_AddStringToObject(dev, "name", s->device.name);
    add_str0(dev, "time_zone", s->device.time_zone);
}

/* GET /api/v1/calendar and the replies of PUT, DELETE and sync (api.md 11.1). today is filled in up to max entries. */
static void add_calendar(rt_t *r, int status, int max_today)
{
    cal_status_t cs;
    net_port_cal_status(&cs);
    ok(r, status);
    cJSON *c = cJSON_AddObjectToObject(r->o, "calendar");
    cJSON_AddBoolToObject(c, "saved", cs.saved);
    if (cs.saved) {
        cJSON *ad = cJSON_AddObjectToObject(c, "address");
        cJSON_AddStringToObject(ad, "host", cs.host);
        cJSON_AddStringToObject(ad, "file", cs.file);
        cJSON_AddStringToObject(ad, "ending", cs.ending);
    } else {
        cJSON_AddNullToObject(c, "address");
    }
    add_time(c, "last_sync", cs.saved ? cs.last_sync : 0);
    cJSON_AddBoolToObject(c, "syncing", cs.syncing);
    if (cs.error) {
        cJSON *e = cJSON_AddObjectToObject(c, "error");
        cJSON_AddStringToObject(e, "error", cs.error);
        add_str0(e, "message", cs.error_message);
        add_time(e, "at", cs.error_at);
    } else {
        cJSON_AddNullToObject(c, "error");
    }
    if (cs.check != CAL_CHECK_NONE) {
        static const char *const st[] = {NULL, "checking", "saved", "failed"};
        cJSON *k = cJSON_AddObjectToObject(c, "check");
        cJSON_AddStringToObject(k, "state", st[cs.check]);
        add_str(k, "error", cs.check_error);
        add_str0(k, "message", cs.check_message);
    } else {
        cJSON_AddNullToObject(c, "check");
    }
    if (cs.saved) {
        /* Today's meetings that count, still to come or in progress, in order (the list is sorted by start). The
         * reply stays within NET_REPLY_MAX (api.md 2.5): with long titles and locations shown, 32 meetings can pass
         * 8 KB, so the list stops at the first one that wouldn't fit. left_today still counts them all, so a shorter
         * today says the list was cut (api.md 11.1). */
        cJSON *t = cJSON_AddArrayToObject(c, "today");
        int n = 0;
        size_t used = printed_len(r->o) + 24;      /* everything so far, plus "left_today" and its number */
        bool full = false;
        if (r->now.valid) {
            tb_epoch_t mid = tb_local_midnight(r->now.wall), next = tb_local_midnight(mid + 26 * 3600);
            for (int i = 0; i < s_app->n_meetings; i++) {
                const tb_meeting_t *m = &s_app->meetings[i];
                if (m->end <= r->now.wall || m->start < mid || m->start >= next) continue;
                if (n < max_today && !full) {
                    cJSON *mo = meeting_obj(m);
                    size_t add = printed_len(mo) + 1;      /* and its comma */
                    full = used + add > NET_REPLY_MAX;
                    if (full) {
                        cJSON_Delete(mo);
                    } else {
                        cJSON_AddItemToArray(t, mo);
                        used += add;
                    }
                }
                n++;
            }
        }
        cJSON_AddNumberToObject(c, "left_today", n);
    } else {
        cJSON_AddNullToObject(c, "today");
        cJSON_AddNullToObject(c, "left_today");
    }
}

/* ======================================================================================================== */
/* core's answers                                                                                           */
/* ======================================================================================================== */

/* A core error as api.md Appendix B. Returns true if it was one. */
static bool core_failed(rt_t *r, tb_err_t e, const char *field)
{
    static const char setup_msg[] = "MiniBar is on its Wi-Fi setup screens. Finish setup or skip it on the bar first.";
    switch (e) {
    case TB_OK: return false;
    case TB_E_BAD_VALUE: bad_value(r, field, "That value isn't allowed."); break;
    case TB_E_IN_SETUP: fail(r, 409, "in_setup", setup_msg, NULL); break;
    case TB_E_NO_MESSAGE:
        fail(r, 409, "no_message", "No message was ever set. Send one with POST /api/v1/message.", "status");
        break;
    case TB_E_NOTHING_TO_SET_ASIDE: fail(r, 409, "nothing_to_set_aside", "No call or meeting is on screen.", "aside"); break;
    case TB_E_NOTHING_SET_ASIDE: fail(r, 409, "nothing_set_aside", "Nothing is set aside.", "aside"); break;
    case TB_E_NOT_RUNNING: fail(r, 409, "not_running", "The timer isn't running.", "action"); break;
    case TB_E_NOTHING_TO_EXTEND:
        fail(r, 409, "nothing_to_extend", "Only a running or paused session or break can be extended.", "action");
        break;
    case TB_E_NO_CALENDAR: fail(r, 409, "no_calendar", "Add a calendar address first.", field); break;
    case TB_E_POWERED_OFF:
        fail_retry(r, 503, "busy", "MiniBar is starting up or powering off. Try again in a second.", NULL, 1);
        break;
    default: fail(r, 500, "internal", "Unexpected result from the status engine.", NULL); break;
    }
    return true;
}

static bool in_setup(rt_t *r)
{
    if (!tb_app_on_wifi_screen(s_app)) return false;
    fail(r, 409, "in_setup", "MiniBar is on its Wi-Fi setup screens. Finish setup or skip it on the bar first.", NULL);
    return true;
}

static bool need_body(rt_t *r, const cJSON *b)
{
    if (b) return true;
    fail(r, 400, "bad_json", "The body isn't a JSON object.", NULL);
    return false;
}

/* ======================================================================================================== */
/* Handlers                                                                                                 */
/* ======================================================================================================== */

static void h_info(rt_t *r, cJSON *b)
{
    (void)b;
    add_info(r);
}

static void h_status_get(rt_t *r, cJSON *b)
{
    (void)b;
    uint32_t rev = add_status(r);
    if (r->via == NET_VIA_USB) return;
    snprintf(r->resp->etag, sizeof r->resp->etag, "\"r%u\"", (unsigned)rev);
    const char *inm = r->req->if_none_match;
    if (inm && (strstr(inm, r->resp->etag) || !strcmp(inm, "*"))) r->status = 304;
}

/* ---------- POST /api/v1/call (section 5) ---------- */

static void h_call(rt_t *r, cJSON *b)
{
    if (!need_body(r, b)) return;
    net_call_msg_t m = {.inputs = -1, .elapsed_s = -1, .via = r->via == NET_VIA_USB ? TB_LINK_USB : TB_LINK_WIFI};
    if (!get_client(r, b, &m.client)) return;
    fstat_t f = get_str(b, "session", &m.session);
    if (f == F_TYPE) { bad_request(r, "session", "\"session\" must be a string."); return; }
    if (f == F_OK && !is_session(m.session)) {
        bad_value(r, "session", "\"session\" must be 1 to 16 letters or digits.");
        return;
    }
    int64_t v;
    f = get_int(b, "seq", -1e300, 1e300, &v);
    if (f == F_TYPE) { bad_request(r, "seq", "\"seq\" must be a number."); return; }
    if (f != F_ABSENT && (f != F_OK || v < 1 || v > 4294967295LL)) {
        bad_value(r, "seq", "\"seq\" must be a whole number from 1 to 4294967295.");
        return;
    }
    if (f == F_OK) { m.seq = (uint32_t)v; m.has_seq = true; }
    if (get_bool(b, "active", &m.active) != F_OK) {
        bad_request(r, "active", "\"active\" is required and must be true or false.");
        return;
    }
    const char *app = NULL;
    char app_clean[TB_APP_NAME_BYTES * 2];
    f = get_str(b, "app", &app);
    if (f == F_TYPE) { bad_request(r, "app", "\"app\" must be a string or null."); return; }
    if (f == F_OK) {
        size_t n = strlen(app);
        if (n < 1 || n > 64) { bad_value(r, "app", "\"app\" must be 1 to 64 bytes."); return; }
        clean(app_clean, sizeof app_clean, app);   /* never longer than what came in */
        if (app_clean[0]) m.app = app_clean;
    }
    const cJSON *in = item(b, "inputs");
    if (in && !cJSON_IsNull(in)) {
        if (!cJSON_IsArray(in)) {
            bad_request(r, "inputs", "\"inputs\" must be a list of \"mic\" and \"camera\".");
            return;
        }
        m.inputs = 0;
        const cJSON *e;
        cJSON_ArrayForEach(e, in) {
            if (cJSON_IsString(e) && !strcmp(e->valuestring, "mic")) m.inputs |= NET_INPUT_MIC;
            else if (cJSON_IsString(e) && !strcmp(e->valuestring, "camera")) m.inputs |= NET_INPUT_CAMERA;
            else { bad_value(r, "inputs", "\"inputs\" can hold \"mic\" and \"camera\"."); return; }
        }
    }
    f = get_int(b, "call_id", -1e300, 1e300, &v);
    if (f == F_TYPE) { bad_request(r, "call_id", "\"call_id\" must be a number."); return; }
    if (f != F_ABSENT && (f != F_OK || v < 1 || v > 4294967295LL)) {
        bad_value(r, "call_id", "\"call_id\" must be a whole number from 1 to 4294967295.");
        return;
    }
    if (f == F_OK) m.call_id = (uint32_t)v;
    f = get_int(b, "elapsed_s", -1e300, 1e300, &v);
    if (f == F_TYPE) { bad_request(r, "elapsed_s", "\"elapsed_s\" must be a number."); return; }
    if (f != F_ABSENT && (f != F_OK || v < 0 || v > 86400)) {
        bad_value(r, "elapsed_s", "\"elapsed_s\" must be a whole number from 0 to 86400.");
        return;
    }
    if (f == F_OK) m.elapsed_s = (int32_t)v;
    f = get_bool(b, "leaving", &m.leaving);
    if (f == F_TYPE) { bad_request(r, "leaving", "\"leaving\" must be true or false."); return; }
    if (m.leaving && m.active) { bad_value(r, "leaving", "\"leaving\" needs \"active\": false."); return; }
    /* Who may report for this client (api.md 5.1): a token paired with a client ID, only for that client; a token
     * paired without one, only for a Mac no other device holds, one Mac at a time. So no token can end or restart
     * another device's call (the Mac on USB's included). USB needs no token: the cable is the proof. */
    bool released = false;
    if (r->tok) {
        const net_token_t *t = r->tok;
        bool mine = t->client[0] ? !strcmp(t->client, m.client)
                                 : !net_pair_find_client(&s_pair, m.client) &&
                                       net_macs_may_report(&s_macs, m.client, t->token_id);
        if (!mine) {
            fail(r, 403, "wrong_client", "This device's token can't report calls for that client.", "client");
            return;
        }
        m.token_id = t->token_id;
        if (!t->client[0]) released = net_macs_release_token(&s_macs, t->token_id, m.client);
        /* A Mac that paired over Wi-Fi is labeled with its pairing's name until a hello names it. */
        if (t->kind == NET_KIND_MAC && t->client[0]) m.name = t->name;
    }

    bool fresh = net_macs_on_call(&s_macs, &m, &r->now);
    if (fresh || released) push_macs(NULL, &r->now);
    add_call_reply(r, !fresh);
}

/* ---------- section 8: own status, message, set aside ---------- */

static void h_status_post(rt_t *r, cJSON *b)
{
    if (!need_body(r, b)) return;
    const char *st;
    fstat_t f = get_str(b, "status", &st);
    if (f != F_OK) { bad_request(r, "status", "\"status\" is required."); return; }
    int idx = -1;
    for (int i = 0; i < TB_ST_COUNT; i++)
        if (!strcmp(st, STATUS_IDS[i])) idx = i;
    if (idx < 0) {
        char msg[120];
        snprintf(msg, sizeof msg, "Unknown status \"%.32s\".%s", st,
                 !strcmp(st, "on_a_call") || !strcmp(st, "call") ? " On a call is set by the Mac app only." : "");
        { bad_value(r, "status", msg); return; }
    }
    const char *back = NULL, *note = NULL;
    f = get_str(b, "back_at", &back);
    if (f == F_TYPE) { bad_request(r, "back_at", "\"back_at\" must be a time like \"13:30\"."); return; }
    char note_clean[TB_AWAY_NOTE_BYTES * 2];
    if (get_str(b, "note", &note) == F_TYPE) { bad_request(r, "note", "\"note\" must be a string."); return; }
    if ((back || note) && idx != TB_ST_AWAY)
        { bad_value(r, back ? "back_at" : "note", "\"back_at\" and \"note\" go only with \"away\"."); return; }
    if (back) {
        bool fine = strlen(back) == 5 && back[2] == ':';
        for (int i = 0; fine && i < 5; i++)
            if (i != 2 && (back[i] < '0' || back[i] > '9')) fine = false;
        if (fine && ((back[0] - '0') * 10 + back[1] - '0' > 23 || (back[3] - '0') * 10 + back[4] - '0' > 59)) fine = false;
        if (!fine) { bad_value(r, "back_at", "\"back_at\" must be a 24-hour time like \"13:30\"."); return; }
    }
    if (note) {
        size_t n = clean(note_clean, sizeof note_clean, note);
        if (n < 1 || n > TB_AWAY_NOTE_CHARS) { bad_value(r, "note", "\"note\" must be 1 to 40 characters."); return; }
        note = note_clean;
    }
    bool aside;
    if (!get_set_aside(r, b, &aside)) return;
    tb_err_t e = tb_app_remote_status(s_app, (tb_status_t)idx, back, note, aside, &r->now);
    if (core_failed(r, e, "status")) return;
    add_status(r);
}

static void h_message(rt_t *r, cJSON *b)
{
    if (!need_body(r, b)) return;
    const char *text;
    if (get_str(b, "text", &text) != F_OK) { bad_request(r, "text", "\"text\" is required."); return; }
    char t[TB_MESSAGE_MAX_CHARS * 4 + 8];
    size_t n = clean(t, sizeof t, text);
    if (n < 1 || n > TB_MESSAGE_MAX_CHARS || strlen(text) > NET_BODY_MAX)
        { bad_value(r, "text", "\"text\" must be 1 to 80 characters."); return; }
    if (refuse_unsupported(r, t, "text")) return;
    bool aside;
    if (!get_set_aside(r, b, &aside)) return;
    tb_err_t e = tb_app_remote_message(s_app, t, aside, &r->now);
    if (core_failed(r, e, "text")) return;
    add_status(r);
}

static void h_aside(rt_t *r, cJSON *b)
{
    if (!need_body(r, b)) return;
    bool aside;
    if (get_bool(b, "aside", &aside) != F_OK) {
        bad_request(r, "aside", "\"aside\" is required and must be true or false.");
        return;
    }
    tb_err_t e = tb_app_remote_aside(s_app, aside, &r->now);
    if (core_failed(r, e, "aside")) return;
    add_status(r);
}

/* ---------- section 9: the Pomodoro ---------- */

static void h_pomodoro(rt_t *r, cJSON *b)
{
    if (!need_body(r, b)) return;
    static const char *const names[] = {"start", "pause", "toggle", "skip", "stop", "extend"};
    const char *act;
    if (get_str(b, "action", &act) != F_OK) { bad_request(r, "action", "\"action\" is required."); return; }
    int a = -1;
    for (int i = 0; i < 6; i++)
        if (!strcmp(act, names[i])) a = i;
    if (a < 0) { bad_value(r, "action", "\"action\" must be start, pause, toggle, skip, stop or extend."); return; }
    int minutes = TB_ADD_MIN;
    if (a == TB_POMO_EXTEND) {
        int64_t v;
        fstat_t f = get_int(b, "minutes", -1e300, 1e300, &v);
        if (f == F_TYPE) { bad_request(r, "minutes", "\"minutes\" must be a number."); return; }
        if (f != F_ABSENT && (f != F_OK || v < 1 || v > 60)) {
            bad_value(r, "minutes", "\"minutes\" must be a whole number from 1 to 60.");
            return;
        }
        if (f == F_OK) minutes = (int)v;
    }
    bool aside;
    if (!get_set_aside(r, b, &aside)) return;
    tb_err_t e = tb_app_remote_pomodoro(s_app, (tb_pomo_action_t)a, minutes, aside, &r->now);
    if (core_failed(r, e, a == TB_POMO_EXTEND && e == TB_E_BAD_VALUE ? "minutes" : "action")) return;
    add_status(r);
}

/* ---------- section 10: settings ---------- */

static void h_settings_get(rt_t *r, cJSON *b)
{
    (void)b;
    add_settings(r);
}

/* One integer setting: sets *has and *dst, or answers bad_value. */
static bool set_int(rt_t *r, const cJSON *sec, const char *k, const char *field, int lo, int hi, bool *has, uint8_t *dst)
{
    const cJSON *j = item(sec, k);
    if (!j) return true;
    int64_t v;
    fstat_t f = get_int(sec, k, lo, hi, &v);
    if (f != F_OK) {
        char msg[96];
        snprintf(msg, sizeof msg, "%s must be a whole number from %d to %d.", field, lo, hi);
        bad_value(r, field, msg);
        return false;
    }
    *has = true;
    *dst = (uint8_t)v;
    return true;
}

static bool set_bool(rt_t *r, const cJSON *sec, const char *k, const char *field, bool *has, bool *dst)
{
    const cJSON *j = item(sec, k);
    if (!j) return true;
    if (!cJSON_IsBool(j)) {
        char msg[96];
        snprintf(msg, sizeof msg, "%s must be true or false.", field);
        bad_value(r, field, msg);
        return false;
    }
    *has = true;
    *dst = cJSON_IsTrue(j);
    return true;
}

/* A section of the PATCH body: absent is fine, anything but an object is a bad value. */
static bool section(rt_t *r, const cJSON *b, const char *k, const cJSON **out)
{
    *out = item(b, k);
    if (!*out) return true;
    if (cJSON_IsObject(*out)) return true;
    char msg[64];
    snprintf(msg, sizeof msg, "\"%s\" must be an object.", k);
    bad_value(r, k, msg);
    return false;
}

static void h_settings_patch(rt_t *r, cJSON *b)
{
    if (!need_body(r, b)) return;
    tb_settings_patch_t p;
    memset(&p, 0, sizeof p);
    p.v = s_app->set;
    const cJSON *po, *di, *au, *de;
    if (!section(r, b, "pomodoro", &po) || !section(r, b, "display", &di) || !section(r, b, "automatic", &au) ||
        !section(r, b, "device", &de))
        return;
    if (!set_int(r, po, "focus_min", "pomodoro.focus_min", 1, 120, &p.has_focus_min, &p.v.pomodoro.focus_min) ||
        !set_int(r, po, "short_min", "pomodoro.short_min", 1, 60, &p.has_short_min, &p.v.pomodoro.short_min) ||
        !set_int(r, po, "long_min", "pomodoro.long_min", 1, 60, &p.has_long_min, &p.v.pomodoro.long_min) ||
        !set_int(r, po, "long_every", "pomodoro.long_every", 2, 8, &p.has_long_every, &p.v.pomodoro.long_every) ||
        !set_bool(r, po, "auto_start", "pomodoro.auto_start", &p.has_auto_start, &p.v.pomodoro.auto_start) ||
        !set_bool(r, po, "chime", "pomodoro.chime", &p.has_chime, &p.v.pomodoro.chime) ||
        !set_bool(r, po, "ticking", "pomodoro.ticking", &p.has_ticking, &p.v.pomodoro.ticking) ||
        !set_int(r, di, "brightness", "display.brightness", 10, 100, &p.has_brightness, &p.v.display.brightness) ||
        !set_bool(r, au, "calendar", "automatic.calendar", &p.has_calendar, &p.v.automatic.calendar) ||
        !set_bool(r, au, "mac", "automatic.mac", &p.has_mac, &p.v.automatic.mac) ||
        !set_bool(r, au, "meeting_titles", "automatic.meeting_titles", &p.has_meeting_titles, &p.v.automatic.meeting_titles))
        return;
    const cJSON *tv = item(po, "tick_volume");
    if (tv) {
        if (cJSON_IsString(tv) && !strcmp(tv->valuestring, "soft")) p.v.pomodoro.tick_volume = TB_TICK_SOFT;
        else if (cJSON_IsString(tv) && !strcmp(tv->valuestring, "medium")) p.v.pomodoro.tick_volume = TB_TICK_MEDIUM;
        else { bad_value(r, "pomodoro.tick_volume", "pomodoro.tick_volume must be \"soft\" or \"medium\"."); return; }
        p.has_tick_volume = true;
    }
    const cJSON *nm = item(de, "name");
    if (nm) {
        if (!cJSON_IsString(nm)) { bad_value(r, "device.name", "device.name must be a string."); return; }
        char t[TB_DEVICE_NAME_BYTES * 2];
        size_t n = clean(t, sizeof t, nm->valuestring);
        if (n < 1 || n > TB_DEVICE_NAME_CHARS || strlen(nm->valuestring) > 256)
            { bad_value(r, "device.name", "device.name must be 1 to 24 characters."); return; }
        if (refuse_unsupported(r, t, "device.name")) return;
        tb_strlcpy(p.v.device.name, t, sizeof p.v.device.name);
        p.has_name = true;
    }
    const cJSON *tz = item(de, "time_zone");
    if (tz) {
        if (!cJSON_IsString(tz) || !tz->valuestring[0] || strlen(tz->valuestring) >= TB_TZ_NAME_BYTES ||
            !net_port_time_zone_known(tz->valuestring))
            {
               bad_value(r, "device.time_zone", "device.time_zone must be a time zone MiniBar knows, like \"America/Los_Angeles\".");
               return;
           }
        tb_strlcpy(p.v.device.time_zone, tz->valuestring, sizeof p.v.device.time_zone);
        p.has_time_zone = true;
    }
    const char *field = NULL;
    tb_err_t e = tb_app_remote_settings(s_app, &p, &field, &r->now);
    if (core_failed(r, e, field)) return;
    add_settings(r);
}

/* ---------- section 11: calendar ---------- */

static void h_cal_get(rt_t *r, cJSON *b)
{
    (void)b;
    add_calendar(r, 200, TB_MEETINGS_MAX);
}

static bool cal_format_failed(rt_t *r, int e, const char *field)
{
    if (!e) return false;
    const char *code = cal_url_err_code((cal_url_err_t)e);
    const char *msg = cal_url_err_message((cal_url_err_t)e);
    fail(r, 400, code ? code : "bad_value", msg ? msg : "That address can't be used.", field);
    return true;
}

static void h_cal_put(rt_t *r, cJSON *b)
{
    if (!need_body(r, b)) return;
    const char *url;
    fstat_t f = get_str(b, "url", &url);
    if (f != F_OK || !url[0]) { bad_request(r, "url", cal_url_err_message(CAL_URL_EMPTY)); return; }
    if (strlen(url) > CAL_URL_MAX) { bad_value(r, "url", "The address can be up to 1024 bytes."); return; }
    if (cal_format_failed(r, net_port_cal_check(url), "url")) return;
    if (cal_format_failed(r, net_port_cal_put(url, false), "url")) return;
    add_calendar(r, 202, TB_MEETINGS_MAX);
}

static void h_cal_delete(rt_t *r, cJSON *b)
{
    (void)b;
    if (net_port_cal_remove() != 0) { fail(r, 409, "no_calendar", "No calendar address is saved.", NULL); return; }
    add_calendar(r, 200, TB_MEETINGS_MAX);
}

static void h_cal_sync(rt_t *r, cJSON *b)
{
    (void)b;
    int e = net_port_cal_sync_now();
    if (e == -1) { fail(r, 409, "no_calendar", "No calendar address is saved.", NULL); return; }
    if (e == -2) { fail(r, 503, "offline", "MiniBar has no Wi-Fi, so it can't sync.", NULL); return; }
    add_calendar(r, 202, TB_MEETINGS_MAX);
}

/* ---------- section 4: pairing ---------- */

static const char *kind_label(net_kind_t k)
{
    return k == NET_KIND_MAC ? "Mac" : k == NET_KIND_REMOTE ? "Phone" : k == NET_KIND_AUTOMATION ? "Script" : "Device";
}

static const char *kind_id(net_kind_t k)
{
    return k == NET_KIND_MAC ? "mac" : k == NET_KIND_REMOTE ? "remote" : k == NET_KIND_AUTOMATION ? "automation" : "device";
}

/* The pairing reply (api.md 4.7, 6.6): token null when it went into the cookie. */
static void add_pair_reply(rt_t *r, const char *token, const net_token_t *t)
{
    char id[13];
    net_port_device_id(id);
    net_wifi_info_t w;
    net_port_wifi(&w);
    ok(r, 200);
    add_str(r->o, "token", token);
    cJSON_AddStringToObject(r->o, "token_id", t->token_id);
    cJSON_AddStringToObject(r->o, "scope", t->scope == NET_SCOPE_FULL ? "full" : "call");
    cJSON_AddStringToObject(r->o, "device_id", id);
    cJSON_AddStringToObject(r->o, "name", s_app->set.device.name);
    cJSON_AddStringToObject(r->o, "host", bar_host(&w));
}

static void token_limit(rt_t *r)
{
    fail(r, 409, "token_limit", "MiniBar already has 10 paired devices. Remove one on the Remote.", NULL);
}

static void h_pair_start(rt_t *r, cJSON *b)
{
    if (!need_body(r, b)) return;
    if (in_setup(r)) return;
    /* "Powering off" is up: the power goes in a moment and takes any code with it, so none is shown (the mock-up's bar
     * doesn't answer then). The splash is different: a code waits for it and shows once the bar has started. */
    if (s_app->powering_off || s_app->powered_off) { core_failed(r, TB_E_POWERED_OFF, NULL); return; }
    const char *kind_s, *scope_s, *name = NULL, *client = NULL;
    if (get_str(b, "kind", &kind_s) != F_OK) {
        bad_request(r, "kind", "\"kind\" is required: mac, remote or automation.");
        return;
    }
    net_kind_t kind = !strcmp(kind_s, "mac") ? NET_KIND_MAC : !strcmp(kind_s, "remote") ? NET_KIND_REMOTE
                    : !strcmp(kind_s, "automation") ? NET_KIND_AUTOMATION : NET_KIND_OTHER;
    if (get_str(b, "scope", &scope_s) != F_OK) {
        bad_request(r, "scope", "\"scope\" is required: call or full.");
        return;
    }
    net_scope_t scope;
    if (!strcmp(scope_s, "call")) scope = NET_SCOPE_CALL;
    else if (!strcmp(scope_s, "full")) scope = NET_SCOPE_FULL;
    else { bad_value(r, "scope", "\"scope\" must be \"call\" or \"full\"."); return; }
    fstat_t f = get_str(b, "client", &client);
    if (f == F_TYPE) { bad_request(r, "client", "\"client\" must be a string."); return; }
    if (f == F_OK && !is_client_id(client)) {
        bad_value(r, "client", "\"client\" must be 8 to 64 letters, digits or dashes.");
        return;
    }
    char label[TB_CLIENT_NAME_BYTES * 2];
    tb_strlcpy(label, kind_label(kind), sizeof label);
    f = get_str(b, "name", &name);
    if (f == F_TYPE) { bad_request(r, "name", "\"name\" must be a string."); return; }
    if (f == F_OK) {
        char t[TB_CLIENT_NAME_BYTES * 2];
        size_t n = clean(t, sizeof t, name);
        /* api.md 4.6: a name that's sent is 1 to 32 characters (after cleaning: "" or only spaces is refused) */
        if (n < 1 || n > 32 || strlen(name) > 256) { bad_value(r, "name", "\"name\" must be 1 to 32 characters."); return; }
        uint32_t bad[1];
        if (!tb_text_unsupported(t, bad, 1)) tb_strlcpy(label, t, sizeof label);   /* else the kind's word */
    }
    char pid[17];
    int retry = 0;
    net_pair_err_t e = net_pair_start(&s_pair, label, kind, scope, client, &r->now, pid, &retry);
    switch (e) {
    case NET_PAIR_OK: break;
    case NET_PAIR_BUSY: {
        char msg[80];
        snprintf(msg, sizeof msg, "Another device is pairing. Try again in %d seconds.", retry);
        { fail_retry(r, 409, "pairing_busy", msg, NULL, retry); return; }
    }
    case NET_PAIR_TOKEN_LIMIT: { token_limit(r); return; }
    default: {
        char msg[96];
        snprintf(msg, sizeof msg, "Too many pairings didn't finish. Try again in %d seconds.", retry);
        { fail_retry(r, 429, "rate_limited", msg, NULL, retry); return; }
    }
    }
    tb_pair_kind_t pk = kind == NET_KIND_MAC ? TB_PAIR_KIND_MAC : kind == NET_KIND_REMOTE ? TB_PAIR_KIND_PHONE
                      : kind == NET_KIND_AUTOMATION ? TB_PAIR_KIND_SCRIPT : TB_PAIR_KIND_OTHER;
    tb_app_pairing_show(s_app, s_pair.code, label, pk, &r->now);
    ok(r, 202);
    cJSON_AddStringToObject(r->o, "pairing_id", pid);
    cJSON_AddNumberToObject(r->o, "pairing_seq", s_pair.seq);     /* info carries it while this code shows (7.1) */
    cJSON_AddNumberToObject(r->o, "expires_in_s", NET_PAIR_CODE_MS / 1000);
    cJSON_AddNumberToObject(r->o, "code_length", NET_PAIR_CODE_LEN);
    cJSON_AddNumberToObject(r->o, "attempts", NET_PAIR_TRIES);
}

/* No in_setup check (api.md 4.7 lists none): starting setup ends any pairing, so during setup this answers
 * not_pairing, as pair/cancel does and as the mock-up's pairTry() does on the Connected screen. */
static void h_pair(rt_t *r, cJSON *b)
{
    if (!need_body(r, b)) return;
    const char *pid, *code;
    if (get_str(b, "pairing_id", &pid) != F_OK) { bad_request(r, "pairing_id", "\"pairing_id\" is required."); return; }
    if (get_str(b, "code", &code) != F_OK) {
        bad_request(r, "code", "\"code\" is required: the 6 digits on the bar.");
        return;
    }
    bool cookie = false;
    if (get_bool(b, "cookie", &cookie) == F_TYPE) {
        bad_request(r, "cookie", "\"cookie\" must be true or false.");
        return;
    }
    char token[NET_TOKEN_LEN + 1];
    const net_token_t *t = NULL;
    int left = 0;
    net_pair_err_t e = net_pair_finish(&s_pair, pid, code, r->req ? r->req->peer_ip : 0, &r->now, token, &t, &left);
    switch (e) {
    case NET_PAIR_OK: break;
    case NET_PAIR_RATE_LIMITED: {
        fail_retry(r, 429, "rate_limited", "One pairing attempt a second, please.", NULL, 1);
        return;
    }
    case NET_PAIR_NOT_PAIRING:
        { fail(r, 409, "not_pairing", "No code is on the bar for this pairing. Start again.", "pairing_id"); return; }
    case NET_PAIR_WRONG_CODE: {
        char msg[80];
        if (left) snprintf(msg, sizeof msg, "That code doesn't match. %d %s left.", left, left == 1 ? "try" : "tries");
        else snprintf(msg, sizeof msg, "That code doesn't match, and that was the last try. Start again.");
        fail(r, 403, "wrong_code", msg, "code");
        cJSON_AddNumberToObject(r->o, "attempts_left", left);
        if (!left) tb_app_pairing_end(s_app, TB_PAIR_END_WRONG_CODE, NULL, &r->now);
        return;
    }
    case NET_PAIR_TOKEN_LIMIT:
        tb_app_pairing_end(s_app, TB_PAIR_END_CANCELED, NULL, &r->now);
        { token_limit(r); return; }
    default: { fail(r, 500, "internal", "Pairing failed.", NULL); return; }
    }
    tb_app_pairing_end(s_app, TB_PAIR_END_PAIRED, t->name, &r->now);
    paired_changed();
    bool set_cookie = cookie && r->via != NET_VIA_USB;
    if (set_cookie)
        snprintf(r->resp->set_cookie, sizeof r->resp->set_cookie,
                 "tb_token=%s; HttpOnly; SameSite=Strict; Path=/api/; Max-Age=31536000", token);
    add_pair_reply(r, set_cookie ? NULL : token, t);
    memset(token, 0, sizeof token);
}

/* POST /api/v1/pair/cancel (api.md 4.7): the device that asked takes its code off the bar (the Remote's Cancel, the
 * Mac app's Back), so it doesn't hold up every other device for 2 minutes. Only its pairing_id works. The bar ends the
 * pairing as a tap would ("Pairing canceled"), and it counts as a failed pairing, so canceling can't buy more guesses.
 * No in_setup check: starting setup ends any pairing, so during setup this answers not_pairing (api.md 13). */
static void h_pair_cancel(rt_t *r, cJSON *b)
{
    if (!need_body(r, b)) return;
    const char *pid;
    if (get_str(b, "pairing_id", &pid) != F_OK) { bad_request(r, "pairing_id", "\"pairing_id\" is required."); return; }
    switch (net_pair_cancel_id(&s_pair, pid, &r->now)) {
    case NET_PAIR_OK: break;
    case NET_PAIR_RATE_LIMITED: { fail_retry(r, 429, "rate_limited", "One pairing request a second, please.", NULL, 1); return; }
    default: { fail(r, 409, "not_pairing", "No code is on the bar for this pairing. Nothing to cancel.", "pairing_id"); return; }
    }
    tb_app_pairing_end(s_app, TB_PAIR_END_CANCELED, NULL, &r->now);
    ok(r, 200);
}

/* ---------- section 12: paired devices ---------- */

static void h_clients(rt_t *r, cJSON *b)
{
    (void)b;
    ok(r, 200);
    cJSON_AddNumberToObject(r->o, "max", NET_TOKENS_MAX);
    cJSON *a = cJSON_AddArrayToObject(r->o, "clients");
    for (int i = 0; i < NET_TOKENS_MAX; i++) {
        const net_token_t *t = &s_pair.tokens[i];
        if (!t->used) continue;
        cJSON *o = cJSON_CreateObject();
        char ip[16];
        cJSON_AddStringToObject(o, "token_id", t->token_id);
        cJSON_AddStringToObject(o, "name", t->name[0] ? t->name : kind_label(t->kind));
        cJSON_AddStringToObject(o, "kind", kind_id(t->kind));
        cJSON_AddStringToObject(o, "scope", t->scope == NET_SCOPE_FULL ? "full" : "call");
        add_time(o, "paired_at", t->paired_at);
        cJSON_AddStringToObject(o, "paired_via", t->paired_via == TB_LINK_USB ? "usb" : "wifi");
        add_time(o, "last_used", t->last_used);
        add_str(o, "last_ip", t->last_ip ? net_ip_str(t->last_ip, ip) : NULL);
        cJSON_AddBoolToObject(o, "self", r->tok == t);
        cJSON_AddItemToArray(a, o);
    }
}

/* Revoke one token: its next request gets 401, and a call it reported over Wi-Fi ends (api.md 12.2), whatever
 * client it named. A call its Mac reports over USB carries on. */
static void revoke(rt_t *r, const net_token_t *t)
{
    char id[9], name[TB_CLIENT_NAME_BYTES], client[65];
    tb_strlcpy(id, t->token_id, sizeof id);
    tb_strlcpy(name, t->name[0] ? t->name : kind_label(t->kind), sizeof name);
    tb_strlcpy(client, t->client, sizeof client);
    net_pair_revoke(&s_pair, id);
    if (r->tok == t) r->tok = NULL;
    paired_changed();
    char toast[TB_TOAST_BYTES];
    snprintf(toast, sizeof toast, "Removed %s", name);
    /* A call it reported over Wi-Fi ends with the removal as the lead ("Removed Mac · back to Busy"), as the mock-up's
     * removeDevice() does (if (!syncAuto(gone)) toast(gone)) and as Forget all does; the removal alone otherwise. */
    bool said = net_macs_forget_token(&s_macs, id, client) && push_macs(toast, &r->now);
    if (!said) tb_app_notify(s_app, toast, &r->now);
    ok(r, 200);
    cJSON_AddStringToObject(r->o, "revoked", id);
}

static void h_client_delete(rt_t *r, cJSON *b)
{
    (void)b;
    const net_token_t *t = net_pair_find(&s_pair, r->param);
    if (!t) { fail(r, 404, "not_found", "No paired device has that token_id.", "token_id"); return; }
    revoke(r, t);
}

static void h_client_self(rt_t *r, cJSON *b)
{
    (void)b;
    if (!r->tok) {
        if (r->via == NET_VIA_USB)
            { bad_request(r, NULL, "USB requests carry no token. Use DELETE /api/v1/clients/{token_id}."); return; }
        { fail(r, 404, "not_found", "This request came with no paired token.", NULL); return; }
    }
    bool cookie = r->from_cookie;
    revoke(r, r->tok);
    if (cookie) snprintf(r->resp->set_cookie, sizeof r->resp->set_cookie, "tb_token=; Max-Age=0; Path=/api/");
}

/* ---------- section 13: the setup network ---------- */

static void h_setup_networks(rt_t *r, cJSON *b)
{
    (void)b;
    net_scan_entry_t n[20];
    int k = net_port_setup_networks(n, 20);
    ok(r, 200);
    cJSON *a = cJSON_AddArrayToObject(r->o, "networks");
    static const char *const sec[] = {"open", "password", "work_login"};
    for (int i = 0; i < k; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", n[i].ssid);
        cJSON_AddStringToObject(o, "security", sec[n[i].security <= NET_SEC_WORK_LOGIN ? n[i].security : 0]);
        cJSON_AddNumberToObject(o, "rssi", n[i].rssi);
        cJSON_AddStringToObject(o, "signal", n[i].rssi >= -78 ? "good" : "weak");
        cJSON_AddItemToArray(a, o);
    }
}

/* C0 controls, DEL, and C1 controls (U+0080 to U+009F as UTF-8). */
static bool has_control_chars(const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (*p < 0x20 || *p == 0x7F || (*p == 0xC2 && p[1] >= 0x80 && p[1] <= 0x9F)) return true;
    return false;
}

static bool wpa_password_ok(const char *p)
{
    size_t n = strlen(p);
    if (n >= 8 && n <= 63) return true;
    if (n != 64) return false;
    for (size_t i = 0; i < n; i++)
        if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f') || (p[i] >= 'A' && p[i] <= 'F')))
            return false;
    return true;
}

static void h_setup_wifi(rt_t *r, cJSON *b)
{
    if (!need_body(r, b)) return;
    net_wifi_info_t w;
    net_port_wifi(&w);
    net_join_status_t js;
    net_port_setup_status(&js);
    /* Connected is the end of setup (decisions.md): from the moment a join works (net's status, which core hears a
     * moment later; then the Connected screen and the 15 s the setup network lingers) nobody on the open network can
     * point the bar elsewhere, or keep the network up by sending again. setup/state still reads the result. */
    if (w.state != NET_WIFI_SETUP || !tb_app_on_wifi_screen(s_app) || s_app->wifi_mode == TB_WIFI_CONNECTED ||
        js.state == NET_JOIN_CONNECTED)
        { fail(r, 404, "not_found", "MiniBar isn't in Wi-Fi setup anymore.", NULL); return; }
    const char *ssid, *pass = NULL, *user = NULL, *cal = NULL, *tz = NULL;
    if (get_str(b, "ssid", &ssid) != F_OK) {
        bad_request(r, "ssid", "\"ssid\" is required: the network's name.");
        return;
    }
    if (!ssid[0] || strlen(ssid) > 32) { bad_value(r, "ssid", "\"ssid\" must be 1 to 32 bytes."); return; }
    /* The bar logs and shows the network's name: a line break or other control character could fake a protocol
     * line on the USB port or garble the screen. */
    if (has_control_chars(ssid)) {
        bad_value(r, "ssid", "MiniBar can't join a network whose name has control characters, such as a line break.");
        return;
    }
    if (get_str(b, "password", &pass) == F_TYPE) {
        bad_request(r, "password", "\"password\" must be a string.");
        return;
    }
    if (get_str(b, "username", &user) == F_TYPE) {
        bad_request(r, "username", "\"username\" must be a string.");
        return;
    }
    if (get_str(b, "calendar_url", &cal) == F_TYPE) {
        bad_request(r, "calendar_url", "\"calendar_url\" must be a string or null.");
        return;
    }
    if (get_str(b, "time_zone", &tz) == F_TYPE) {
        bad_request(r, "time_zone", "\"time_zone\" must be a string.");
        return;
    }
    if (pass && !pass[0]) pass = NULL;
    if (user && !user[0]) user = NULL;
    /* What the network needs: from the scan if it's there, else from what was sent (a hidden network). */
    net_scan_entry_t n[20];
    int k = net_port_setup_networks(n, 20);
    net_security_t sec = user ? NET_SEC_WORK_LOGIN : pass ? NET_SEC_PASSWORD : NET_SEC_OPEN;
    for (int i = 0; i < k; i++)
        if (!strcmp(n[i].ssid, ssid)) sec = n[i].security;
    if (sec == NET_SEC_WORK_LOGIN && !user) {
        bad_request(r, "username", "This network needs your work username.");
        return;
    }
    if (sec != NET_SEC_OPEN && !pass) { bad_request(r, "password", "This network needs a password."); return; }
    /* WPA2 and WPA3 Personal: a passphrase of 8 to 63 characters, or the key itself as 64 hex digits */
    if (sec == NET_SEC_PASSWORD && !wpa_password_ok(pass))
        { bad_value(r, "password", "Wi-Fi passwords are 8 to 63 characters, or 64 hex digits."); return; }
    if (sec == NET_SEC_WORK_LOGIN && (strlen(pass) > 128 || strlen(user) > 128))
        {
           bad_value(r, user && strlen(user) > 128 ? "username" : "password", "That's longer than MiniBar can send.");
           return;
       }
    if (sec == NET_SEC_OPEN) pass = user = NULL;
    if (cal && !cal[0]) cal = NULL;
    if (cal) {
        if (strlen(cal) > CAL_URL_MAX) { bad_value(r, "calendar_url", "The address can be up to 1024 bytes."); return; }
        if (cal_format_failed(r, net_port_cal_check(cal), "calendar_url")) return;
    }
    if (!net_port_setup_join(ssid, sec == NET_SEC_WORK_LOGIN ? user : NULL, pass, cal))
        { fail_retry(r, 503, "busy", "MiniBar couldn't start joining. Try again in a second.", NULL, 1); return; }
    if (tz && tz[0] && strlen(tz) < TB_TZ_NAME_BYTES && net_port_time_zone_known(tz)) {
        /* Lead decision: the setup page's zone (the phone of the person standing at the bar) always applies, so
         * setting the bar up again after a move fixes its clock. Only the Mac's hello is limited to "if none is set
         * yet" (api.md 6.6). Through the settings path, which shows no toast for a zone. */
        tb_settings_patch_t p;
        memset(&p, 0, sizeof p);
        p.has_time_zone = true;
        tb_strlcpy(p.v.device.time_zone, tz, sizeof p.v.device.time_zone);
        const char *field = NULL;
        if (tb_app_remote_settings(s_app, &p, &field, &r->now) != TB_OK) tb_app_set_time_zone(s_app, tz, &r->now);
    }
    tb_app_wifi_connecting(s_app, ssid, &r->now);
    ok(r, 202);
    cJSON_AddStringToObject(r->o, "state", "connecting");
}

static void h_setup_state(rt_t *r, cJSON *b)
{
    (void)b;
    net_join_status_t s;
    net_port_setup_status(&s);
    static const char *const st[] = {"idle", "connecting", "connected", "failed"};
    ok(r, 200);
    cJSON_AddStringToObject(r->o, "state", st[s.state <= NET_JOIN_FAILED ? s.state : 0]);
    add_str(r->o, "error", s.state == NET_JOIN_FAILED ? s.error : NULL);
    add_str(r->o, "message", s.state == NET_JOIN_FAILED ? s.message : NULL);
    add_str0(r->o, "host", s.state == NET_JOIN_CONNECTED ? s.host : NULL);
    add_str0(r->o, "ip", s.state == NET_JOIN_CONNECTED ? s.ip : NULL);
}

/* ======================================================================================================== */
/* Routing                                                                                                  */
/* ======================================================================================================== */

typedef enum { NEED_NONE = 0, NEED_CALL, NEED_FULL, NEED_SETUP } need_t;
typedef void (*handler_t)(rt_t *r, cJSON *body);
typedef struct {
    const char *method;
    const char *path;       /* ending in "/" + "*": one more path segment (clients/{token_id}) */
    need_t need;
    handler_t fn;
} route_t;

static const route_t ROUTES[] = {
    {"GET", "/api/v1/info", NEED_NONE, h_info},
    {"POST", "/api/v1/pair/start", NEED_NONE, h_pair_start},
    {"POST", "/api/v1/pair", NEED_NONE, h_pair},
    {"POST", "/api/v1/pair/cancel", NEED_NONE, h_pair_cancel},
    {"GET", "/api/v1/status", NEED_CALL, h_status_get},
    {"POST", "/api/v1/status", NEED_FULL, h_status_post},
    {"POST", "/api/v1/call", NEED_CALL, h_call},
    {"POST", "/api/v1/message", NEED_FULL, h_message},
    {"POST", "/api/v1/aside", NEED_FULL, h_aside},
    {"POST", "/api/v1/pomodoro", NEED_FULL, h_pomodoro},
    {"GET", "/api/v1/settings", NEED_FULL, h_settings_get},
    {"PATCH", "/api/v1/settings", NEED_FULL, h_settings_patch},
    {"GET", "/api/v1/calendar", NEED_FULL, h_cal_get},
    {"PUT", "/api/v1/calendar", NEED_FULL, h_cal_put},
    {"DELETE", "/api/v1/calendar", NEED_FULL, h_cal_delete},
    {"POST", "/api/v1/calendar/sync", NEED_FULL, h_cal_sync},
    {"GET", "/api/v1/clients", NEED_FULL, h_clients},
    {"DELETE", "/api/v1/clients/self", NEED_CALL, h_client_self},
    {"DELETE", "/api/v1/clients/*", NEED_FULL, h_client_delete},
    {"GET", "/api/v1/setup/networks", NEED_SETUP, h_setup_networks},
    {"POST", "/api/v1/setup/wifi", NEED_SETUP, h_setup_wifi},
    {"GET", "/api/v1/setup/state", NEED_SETUP, h_setup_state},
};
#define N_ROUTES (sizeof ROUTES / sizeof ROUTES[0])

/* Does the route's path match? (path has no query) */
static bool path_match(const route_t *rt, const char *path, const char **param)
{
    size_t n = strlen(rt->path);
    if (rt->path[n - 1] == '*') {
        if (strncmp(path, rt->path, n - 1)) return false;
        const char *rest = path + n - 1;
        if (!*rest || strchr(rest, '/')) return false;
        *param = rest;
        return true;
    }
    return !strcmp(path, rt->path);
}

static bool takes_body(const char *m)
{
    return !strcmp(m, "POST") || !strcmp(m, "PUT") || !strcmp(m, "PATCH");
}

/* Find the endpoint, check where it can be reached from and the token, then hand over to body handling. Returns the
 * route, or NULL when it already answered. */
static const route_t *find_route(rt_t *r, const char *method, const char *raw_path)
{
    char path[160];
    size_t n = strcspn(raw_path, "?#");
    if (n >= sizeof path || strncmp(raw_path, "/api/v1/", 8)) {
        fail(r, 404, "not_found", "There's nothing at that path. Every endpoint is under /api/v1/.", NULL);
        return NULL;
    }
    memcpy(path, raw_path, n);
    path[n] = '\0';
    const route_t *hit = NULL;
    char allow[40] = "";
    bool any = false;
    for (size_t i = 0; i < N_ROUTES; i++) {
        const char *param = NULL;
        if (!path_match(&ROUTES[i], path, &param)) continue;
        /* "self" belongs to its own route, not to clients/{token_id} */
        if (param && !strcmp(param, "self")) continue;
        any = true;
        if (!strcmp(ROUTES[i].method, method)) {
            if (!hit) {
                hit = &ROUTES[i];
                tb_strlcpy(r->param, param ? param : "", sizeof r->param);
            }
        } else if (!strstr(allow, ROUTES[i].method)) {
            if (allow[0]) strncat(allow, ", ", sizeof allow - strlen(allow) - 1);
            strncat(allow, ROUTES[i].method, sizeof allow - strlen(allow) - 1);
        }
    }
    if (!any) {
        fail(r, 404, "not_found", "There's no endpoint at that path.", NULL);
        return NULL;
    }
    if (!hit) {
        /* the Allow header lists every method the path takes */
        tb_strlcpy(r->resp->allow, allow, sizeof r->resp->allow);
        char msg[80];
        snprintf(msg, sizeof msg, "%.8s isn't allowed here. Allowed: %s.", method, allow);
        fail(r, 405, "method_not_allowed", msg, NULL);
        return NULL;
    }
    /* The setup endpoints exist only on the setup network (or over USB while it's up); the setup network serves only
     * them and info (api.md 13). */
    net_wifi_info_t w;
    net_port_wifi(&w);
    bool setup_ok = r->via == NET_VIA_SETUP || (r->via == NET_VIA_USB && w.state == NET_WIFI_SETUP);
    if ((hit->need == NEED_SETUP && !setup_ok) || (r->via == NET_VIA_SETUP && hit->need != NEED_SETUP && hit->fn != h_info)) {
        fail(r, 404, "not_found",
             hit->need == NEED_SETUP ? "The setup endpoints exist only on the MiniBar-Setup network."
                                     : "On the setup network, MiniBar serves only Wi-Fi setup.", NULL);
        return NULL;
    }
    /* Tokens (api.md 4.4, 4.5). USB needs none: the cable is the proof. */
    if (r->via != NET_VIA_USB && s_bearer && (hit->need == NEED_CALL || hit->need == NEED_FULL)) {
        if (!r->tok) {
            fail(r, 401, "unauthorized", "Pair with this MiniBar first.", NULL);
            r->resp->www_authenticate = true;
            return NULL;
        }
        /* An Origin that came but couldn't be read is another origin, never "none" (it would fail open). */
        if (r->from_cookie && (r->req->origin_unreadable || (r->req->origin && !origin_ok(r->req->origin)))) {
            fail(r, 403, "bad_origin", "That page isn't allowed to use this MiniBar.", NULL);
            return NULL;
        }
        if (hit->need == NEED_FULL && r->tok->scope != NET_SCOPE_FULL) {
            fail(r, 403, "wrong_scope", "This device is paired for calls only.", NULL);
            return NULL;
        }
    }
    return hit;
}

/* ======================================================================================================== */
/* Entry points                                                                                             */
/* ======================================================================================================== */

/* The pairing code's 2 minutes, run before any request too, so an expired code never answers. They count from when
 * the code appears on the bar (core's shown_at; decisions.md, Pairing): until then (the splash, the Keep holding
 * screen) the expiry waits. */
static void pair_tick(const tb_clock_t *now)
{
    if (s_pair.showing && s_app && s_app->pairing.active)
        s_pair.expires = s_app->pairing.shown_at ? s_app->pairing.shown_at + NET_PAIR_CODE_MS : now->mono + NET_PAIR_CODE_MS;
    if (net_pair_tick(&s_pair, now) && s_app) tb_app_pairing_end(s_app, TB_PAIR_END_TIMEOUT, NULL, now);
}

static void finish(rt_t *r)
{
    net_resp_t *resp = r->resp;
    resp->status = r->status ? r->status : 500;
    if (resp->status == 304) {
        cJSON_Delete(r->o);
        return;
    }
    char *s = cJSON_PrintUnformatted(r->o);
    if (s && strlen(s) > NET_REPLY_MAX) {
        cJSON_free(s);
        s = NULL;
    }
    if (!s) {
        fail(r, 500, "internal", "The reply didn't fit.", NULL);
        resp->status = 500;
        s = cJSON_PrintUnformatted(r->o);
    }
    cJSON_Delete(r->o);
    resp->body = s;
    resp->len = s ? strlen(s) : 0;
}

void net_api_handle(const net_req_t *req, net_resp_t *resp)
{
    memset(resp, 0, sizeof(*resp));
    rt_t r = {.req = req, .resp = resp, .via = req->via, .now = net_port_now(), .o = cJSON_CreateObject()};
    if (!r.o) {
        resp->status = 503;
        return;
    }
    pair_tick(&r.now);
    if (!s_app) {
        fail_retry(&r, 503, "busy", "MiniBar is starting up. Try again in a second.", NULL, 1);
        return finish(&r);
    }
    /* On the setup network the only name is its address (it stays up a little after setup, for the page). */
    bool host_ok = req->via == NET_VIA_SETUP ? req->host && host_eq(req->host, NET_SETUP_IP) : net_api_host_ok(req->host);
    if (req->via != NET_VIA_USB && !host_ok) {
        fail(&r, 421, "wrong_host", "Use this MiniBar's own address, such as minibar.local.", NULL);
        return finish(&r);
    }
    /* The token, if one came: the header wins over the cookie. */
    const char *tok = req->bearer && req->bearer[0] ? req->bearer : NULL;
    if (!tok && req->cookie_token && req->cookie_token[0]) {
        tok = req->cookie_token;
        r.from_cookie = true;
    }
    if (tok) r.tok = net_pair_check(&s_pair, tok, req->peer_ip, &r.now);
    if (r.tok) paired_changed();    /* last use moved: Forget all lists the names most recently used first */
    if (req->via != NET_VIA_USB) {
        int wait = net_rate_take(&s_rate, req->peer_ip, s_bearer && !r.tok, r.now.mono);
        if (wait) {
            fail_retry(&r, 429, "rate_limited", "Too many requests. Slow down a little.", NULL, wait);
            return finish(&r);
        }
    }
    const route_t *rt = find_route(&r, req->method ? req->method : "", req->path ? req->path : "");
    if (!rt) return finish(&r);
    cJSON *body = NULL;
    if (takes_body(req->method)) {
        if (req->body_too_large || req->body_len > NET_BODY_MAX) {
            fail(&r, 413, "too_large", "The body can be up to 2048 bytes.", NULL);
            return finish(&r);
        }
        if (req->body && req->body_len) {
            if (!req->content_type_json) {
                fail(&r, 415, "unsupported_media_type", "Send the body as application/json.", NULL);
                return finish(&r);
            }
            const char *end = NULL;
            /* cJSON skips a byte-order mark; api.md 2.2 says a body has none. Nothing deeper than
             * NET_JSON_DEPTH_MAX reaches cJSON, which recurses on this task's stack. */
            bool bom = req->body_len >= 3 && !memcmp(req->body, "\xEF\xBB\xBF", 3);
            bool shallow = net_json_depth_ok(req->body, req->body_len, NET_JSON_DEPTH_MAX);
            body = bom || !shallow ? NULL : cJSON_ParseWithLengthOpts(req->body, req->body_len, &end, false);
            bool trailing = false;
            if (body && end)
                for (const char *c = end; c < req->body + req->body_len; c++)
                    if (*c != ' ' && *c != '\t' && *c != '\r' && *c != '\n' && *c != '\0') trailing = true;
            if (!body || !cJSON_IsObject(body) || trailing) {
                cJSON_Delete(body);
                fail(&r, 400, "bad_json", "The body isn't a JSON object.", NULL);
                return finish(&r);
            }
        }
    }
    rt->fn(&r, body);
    cJSON_Delete(body);
    finish(&r);
}

/* ======================================================================================================== */
/* USB (api.md section 6)                                                                                   */
/* ======================================================================================================== */

static void usb_hello(rt_t *r, cJSON *b)
{
    const char *api, *client, *name = NULL, *t = NULL, *tz = NULL;
    if (get_str(b, "api", &api) != F_OK) {
        bad_request(r, "api", "\"api\" is required: the API version the app speaks.");
        return;
    }
    if (strncmp(api, "1.", 2) && strcmp(api, "1")) {
        fail(r, 400, "unsupported_api", "This MiniBar speaks API " NET_API_VERSION ".", "api");
        return;
    }
    if (!get_client(r, b, &client)) return;
    char label[TB_CLIENT_NAME_BYTES * 2] = "";
    fstat_t f = get_str(b, "name", &name);
    if (f == F_TYPE) { bad_request(r, "name", "\"name\" must be a string."); return; }
    if (f == F_OK) {
        size_t n = clean(label, sizeof label, name);
        if (n < 1 || n > 32 || strlen(name) > 256) { bad_value(r, "name", "\"name\" must be 1 to 32 characters."); return; }
        uint32_t bad[1];
        if (tb_text_unsupported(label, bad, 1)) label[0] = '\0';     /* the bar says "Mac" */
    }
    f = get_str(b, "time", &t);
    if (f == F_TYPE) { bad_request(r, "time", "\"time\" must be an RFC 3339 time."); return; }
    tb_epoch_t mac_time = 0;
    if (f == F_OK && !net_parse_rfc3339(t, &mac_time)) {
        bad_value(r, "time", "\"time\" must be an RFC 3339 time.");
        return;
    }
    if (get_str(b, "time_zone", &tz) == F_TYPE) {
        bad_request(r, "time_zone", "\"time_zone\" must be a string.");
        return;
    }

    if (mac_time && net_port_set_time_from_mac(mac_time)) r->now = net_port_now();
    if (tz && tz[0] && strlen(tz) < TB_TZ_NAME_BYTES && net_port_time_zone_known(tz)) tb_app_set_time_zone(s_app, tz, &r->now);
    net_macs_on_hello(&s_macs, client, label[0] ? label : NULL, &r->now);
    push_macs(NULL, &r->now);
    add_info(r);
}

static void usb_pair(rt_t *r, cJSON *b)
{
    const char *client;
    if (!get_client(r, b, &client)) return;
    const net_mac_t *m = net_macs_find(&s_macs, client);
    char token[NET_TOKEN_LEN + 1];
    const net_token_t *t = NULL;
    net_pair_err_t e = net_pair_usb(&s_pair, client, m && m->name[0] ? m->name : "Mac", &r->now, token, &t);
    if (e != NET_PAIR_OK || !t) { token_limit(r); return; }
    /* Only a confirmation: a code another device asked for over Wi-Fi stays on the bar, valid, and info.pairing keeps
     * saying "showing" (core shows this toast once that pairing ends). */
    tb_app_pairing_end(s_app, TB_PAIR_END_PAIRED_USB, t->name, &r->now);
    paired_changed();
    add_pair_reply(r, token, t);
    memset(token, 0, sizeof token);
}

/* request: any endpoint (api.md 6.6). The reply is the endpoint's body plus id and http_status. */
static void usb_request(rt_t *r, cJSON *line)
{
    cJSON *hs = cJSON_AddNumberToObject(r->o, "http_status", 400);
    r->prefix++;
    const char *method, *path;
    if (get_str(line, "method", &method) != F_OK) bad_request(r, "method", "\"method\" is required.");
    else if (strcmp(method, "GET") && strcmp(method, "POST") && strcmp(method, "PUT") && strcmp(method, "PATCH") &&
             strcmp(method, "DELETE"))
        bad_value(r, "method", "\"method\" must be GET, POST, PUT, PATCH or DELETE.");
    else if (get_str(line, "path", &path) != F_OK) bad_request(r, "path", "\"path\" is required.");
    else {
        cJSON *body = cJSON_GetObjectItemCaseSensitive(line, "body");
        if (body && cJSON_IsNull(body)) body = NULL;
        if (body && !cJSON_IsObject(body)) {
            bad_request(r, "body", "\"body\" must be an object or null.");
        } else {
            const route_t *rt = find_route(r, method, path);
            if (rt) rt->fn(r, takes_body(method) ? body : NULL);
        }
    }
    cJSON_SetNumberValue(hs, r->status ? r->status : 500);
}

static bool usb_write(rt_t *r, char *out, size_t cap)
{
    char *s = cJSON_PrintUnformatted(r->o);
    cJSON_Delete(r->o);
    if (!s) return false;
    size_t n = strlen(s);
    bool fits = n + 5 <= cap && n <= NET_REPLY_MAX;
    if (fits) {
        memcpy(out, "@tb ", 4);
        memcpy(out + 4, s, n + 1);
    }
    cJSON_free(s);
    return fits;
}

bool net_api_usb_line(const char *line, size_t len, bool too_long, char *out, size_t cap)
{
    if (len >= 1 && line[len - 1] == '\r') len--;
    if (len < 4 || memcmp(line, "@tb ", 4)) return false;
    net_resp_t resp;
    memset(&resp, 0, sizeof resp);
    rt_t r = {.resp = &resp, .via = NET_VIA_USB, .now = net_port_now(), .o = cJSON_CreateObject()};
    if (!r.o || cap < 64) {
        cJSON_Delete(r.o);
        if (cap) snprintf(out, cap, "@tb {\"id\":null,\"ok\":false,\"error\":\"busy\",\"message\":\"Out of memory.\",\"field\":null}");
        return cap > 0;
    }
    pair_tick(&r.now);
    cJSON_AddNullToObject(r.o, "id");
    r.prefix = 1;
    cJSON *j = NULL;
    if (too_long) {
        fail(&r, 413, "too_large", "Lines can be up to 2048 bytes.", NULL);
    } else if ((len >= 7 && !memcmp(line + 4, "\xEF\xBB\xBF", 3)) ||
               !net_json_depth_ok(line + 4, len - 4, NET_JSON_DEPTH_MAX) ||
               !(j = cJSON_ParseWithLength(line + 4, len - 4)) || !cJSON_IsObject(j)) {
        fail(&r, 400, "bad_json", "That line isn't a JSON object.", NULL);
    } else {
        int64_t v;
        fstat_t f = get_int(j, "id", 1, 2147483647, &v);
        const char *cmd;
        if (f == F_OK) cJSON_ReplaceItemInObjectCaseSensitive(r.o, "id", cJSON_CreateNumber((double)v));
        if (f == F_TYPE || f == F_RANGE) bad_value(&r, "id", "\"id\" must be a whole number from 1 to 2147483647.");
        else if (!s_app) fail_retry(&r, 503, "busy", "MiniBar is starting up. Try again in a second.", NULL, 1);
        else if (get_str(j, "cmd", &cmd) != F_OK) bad_request(&r, "cmd", "\"cmd\" is required.");
        else if (!strcmp(cmd, "hello")) usb_hello(&r, j);
        else if (!strcmp(cmd, "call")) h_call(&r, j);
        else if (!strcmp(cmd, "status")) h_status_get(&r, j);
        else if (!strcmp(cmd, "pair")) usb_pair(&r, j);
        else if (!strcmp(cmd, "request")) usb_request(&r, j);
        else {
            char msg[64];
            snprintf(msg, sizeof msg, "Unknown cmd \"%.24s\".", cmd);
            fail(&r, 400, "unknown_cmd", msg, "cmd");
        }
    }
    cJSON_Delete(j);
    if (usb_write(&r, out, cap)) return true;
    snprintf(out, cap, "@tb {\"id\":null,\"ok\":false,\"error\":\"internal\",\"message\":\"The reply didn't fit.\",\"field\":null}");
    return true;
}

void net_api_usb_ready_line(char *out, size_t cap)
{
    char id[13];
    net_port_device_id(id);
    snprintf(out, cap, "@tb {\"event\":\"ready\",\"device_id\":\"%s\",\"api\":\"" NET_API_VERSION "\",\"fw\":\"%s\"}", id,
             net_port_fw_version());
}

/* ======================================================================================================== */
/* Timers and effects                                                                                       */
/* ======================================================================================================== */

void net_api_tick(const tb_clock_t *now)
{
    pair_tick(now);
    bool lost = net_macs_tick(&s_macs, now);
    push_macs(lost ? "Lost contact with your Mac" : NULL, now);
}

void net_api_pairing_canceled(const tb_clock_t *now)
{
    net_pair_cancel(&s_pair, now);   /* core already said "Pairing canceled" */
}

void net_api_pairing_reset(const tb_clock_t *now)
{
    (void)now;
    net_pair_reset(&s_pair);        /* Power off or Restart: no failed pairing, no back-off; tokens stay */
}

void net_api_forget_devices(const tb_clock_t *now)
{
    /* Every Wi-Fi call goes with its token; USB isn't affected (api.md 4.8). A call that ends says so after the
     * mock-up's lead: "Forgot 3 devices · back to Busy". */
    int n = net_pair_count(&s_pair);
    for (int i = 0; i < NET_TOKENS_MAX; i++)
        if (s_pair.tokens[i].used) net_macs_forget_token(&s_macs, s_pair.tokens[i].token_id, s_pair.tokens[i].client);
    net_macs_forget_token(&s_macs, NULL, NULL);     /* and any Wi-Fi call that came with a token at all */
    net_pair_forget_all(&s_pair);
    paired_changed();
    char lead[40];
    snprintf(lead, sizeof lead, "Forgot %d device%s", n, n == 1 ? "" : "s");
    push_macs(lead, now);
}
