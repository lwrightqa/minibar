/*
 * test_sec_review.c: the 2026-10-04 defensive security review's checks, now part of the suite, with the checks added
 * when its findings were fixed. Owner: lead developer.
 *
 * Each check states what docs/api.md (or decisions.md) asks for: JSON nesting, which token may report which call,
 * revoking and Forget all, the cookie's Origin rule, the 8 KB reply limit, the setup network's SSID and password
 * rules, the log output that must never pass for a protocol line, and the JSON 405 for every other method.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_fixture.h"
#include "net_macs.h"
#include "net_pair.h"
#include "net_util.h"
#include "tb_test.h"

#define USB_MAC "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"

/* An object whose one unknown member is nested `depth` arrays deep. */
static char *nested_body(const char *prefix, int depth)
{
    size_t n = strlen(prefix) + (size_t)depth * 2 + 16;
    char *b = malloc(n);
    size_t o = (size_t)snprintf(b, n, "%s\"x\": ", prefix);
    for (int i = 0; i < depth; i++) b[o++] = '[';
    for (int i = 0; i < depth; i++) b[o++] = ']';
    b[o++] = '}';
    b[o] = '\0';
    return b;
}

static char *token_copy(const char *t)
{
    static char buf[4][NET_TOKEN_LEN + 1];
    static int k;
    char *b = buf[k++ % 4];
    snprintf(b, NET_TOKEN_LEN + 1, "%s", t ? t : "");
    return b;
}

/* The token_id of the paired device of this kind (via GET /api/v1/clients with a full token). */
static void token_id_of(const char *full, const char *kind, const char *name, char out[9])
{
    out[0] = '\0';
    nf_resp_t r = nf_http("GET", "/api/v1/clients", NULL, full);
    for (int i = 0; i < NET_TOKENS_MAX; i++) {
        char k[32], t[32], n[32];
        snprintf(k, sizeof k, "clients.%d.kind", i);
        snprintf(t, sizeof t, "clients.%d.token_id", i);
        snprintf(n, sizeof n, "clients.%d.name", i);
        if (nf_str(r.j, k) && !strcmp(nf_str(r.j, k), kind) && (!name || !strcmp(nf_str(r.j, n), name)))
            snprintf(out, 9, "%s", nf_str(r.j, t));
    }
    nf_free(&r);
}

static int revoke(const char *full, const char *id)
{
    char path[64];
    snprintf(path, sizeof path, "/api/v1/clients/%s", id);
    nf_resp_t r = nf_http("DELETE", path, NULL, full);
    int st = r.status;
    nf_free(&r);
    return st;
}

static nf_resp_t wifi_call(const char *token, const char *client, bool active)
{
    char body[200];
    snprintf(body, sizeof body, "{\"client\": \"%s\", \"active\": %s}", client, active ? "true" : "false");
    return nf_http("POST", "/api/v1/call", body, token);
}

static void usb_call(const char *client, bool active)
{
    char line[256];
    snprintf(line, sizeof line, "@tb {\"cmd\": \"call\", \"id\": 1, \"client\": \"%s\", \"session\": \"u1\", "
             "\"seq\": %d, \"active\": %s}", client, (int)(fake_now.mono / 100), active ? "true" : "false");
    cJSON *j = nf_usb(line);
    TB_TRUE(nf_true(j, "ok"));
    cJSON_Delete(j);
}

/* ======================================================================================================== */
/* 1. JSON nesting                                                                                          */
/* ======================================================================================================== */

/* The review found no nesting limit below cJSON's 1000: on the bar the parse runs on the 12 KB app task with a
 * 64-byte frame per level, so a few hundred levels overflow it. */
TB_TEST(sec_router_refuses_deeply_nested_json_without_a_token)
{
    nf_setup();
    char *b = nested_body("{\"kind\": \"mac\", \"scope\": \"call\", ", 300);
    TB_TRUE(strlen(b) < NET_BODY_MAX);
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", b, NULL);   /* no token needed for this endpoint */
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_err(&r), "bad_json");
    nf_free(&r);
    free(b);
    TB_FALSE(nf_app.pairing.active);
}

TB_TEST(sec_usb_refuses_deeply_nested_json)
{
    nf_setup();
    char *b = nested_body("@tb {\"cmd\": \"status\", \"id\": 1, ", 300);
    cJSON *j = nf_usb(b);
    TB_EQ_STR(nf_str(j, "error"), "bad_json");
    cJSON_Delete(j);
    free(b);
    /* a request's body is part of the same line */
    b = nested_body("@tb {\"cmd\": \"request\", \"id\": 2, \"method\": \"PATCH\", \"path\": \"/api/v1/settings\", "
                    "\"body\": {\"pomodoro\": {\"focus_min\": 30}}, ", 40);
    j = nf_usb(b);
    TB_EQ_STR(nf_str(j, "error"), "bad_json");
    TB_EQ_INT(nf_app.set.pomodoro.focus_min, 25);
    cJSON_Delete(j);
    free(b);
}

TB_TEST(sec_json_nesting_limit_is_exactly_the_documented_one)
{
    nf_setup();
    /* the object itself is level 1: 15 arrays inside make 16, which is allowed; 17 isn't */
    char *ok16 = nested_body("{\"kind\": \"mac\", \"scope\": \"call\", ", NET_JSON_DEPTH_MAX - 1);
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", ok16, NULL);
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    free(ok16);
    nf_setup();
    char *bad17 = nested_body("{\"kind\": \"mac\", \"scope\": \"call\", ", NET_JSON_DEPTH_MAX);
    r = nf_http("POST", "/api/v1/pair/start", bad17, NULL);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_err(&r), "bad_json");
    nf_free(&r);
    free(bad17);
    /* the deepest real request, 3 levels over USB (the line, its body, a settings section), still works */
    cJSON *j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 3, \"method\": \"PATCH\", \"path\": \"/api/v1/settings\", "
                      "\"body\": {\"pomodoro\": {\"focus_min\": 30}, \"automatic\": {\"mac\": true}}}");
    TB_EQ_INT(nf_num(j, "http_status"), 200);
    TB_EQ_INT(nf_app.set.pomodoro.focus_min, 30);
    cJSON_Delete(j);
}

TB_TEST(sec_cjson_itself_has_the_build_limit)
{
    /* CJSON_NESTING_LIMIT reached cJSON.c in this build (test/host/CMakeLists.txt; firmware/CMakeLists.txt on the
     * bar), so even a parse that skipped net_json_depth_ok couldn't go deep */
    TB_EQ_INT(CJSON_NESTING_LIMIT, NET_JSON_DEPTH_MAX);
    char deep[64], deeper[64];
    memset(deep, 0, sizeof deep);
    memset(deeper, 0, sizeof deeper);
    for (int i = 0; i < CJSON_NESTING_LIMIT; i++) deep[i] = '[', deep[2 * CJSON_NESTING_LIMIT - 1 - i] = ']';
    for (int i = 0; i <= CJSON_NESTING_LIMIT; i++) deeper[i] = '[', deeper[2 * CJSON_NESTING_LIMIT + 1 - i] = ']';
    cJSON *a = cJSON_Parse(deep), *b = cJSON_Parse(deeper);
    TB_TRUE(a != NULL);
    TB_TRUE(b == NULL);
    cJSON_Delete(a);
}

TB_TEST(sec_json_depth_check_reads_strings_right)
{
    TB_TRUE(net_json_depth_ok("{\"a\": \"[[[[[[[[[[[[[[[[[[[[\"}", 27, 2));     /* brackets in a string don't count */
    TB_TRUE(net_json_depth_ok("{\"a\": \"\\\"[[[[\"}", 15, 1));                   /* nor after an escaped quote */
    TB_FALSE(net_json_depth_ok("{\"a\": [[]]}", 11, 2));
    TB_TRUE(net_json_depth_ok("{\"a\": [[]]}", 11, 3));
    TB_TRUE(net_json_depth_ok("{}{}{}{}", 8, 1));                                 /* siblings aren't depth */
    TB_TRUE(net_json_depth_ok("]]]]{", 5, 1));                                    /* cJSON refuses it, not this */
    TB_TRUE(net_json_depth_ok("", 0, 1));
}

/* ======================================================================================================== */
/* 3. Which token may report which call; revoke and Forget all end calls by token (api.md 5.1, 12.2, 4.8)    */
/* ======================================================================================================== */

/* The review: a call's client wasn't tied to its token, so revoking a token ended only calls whose client matched
 * the pairing's. Now a token paired with a client ID can't report for another client at all (403 wrong_client), and
 * revoking ends every Wi-Fi call that came with the token. */
TB_TEST(sec_revoke_ends_the_wifi_call_the_token_reported)
{
    nf_setup();
    char *mac_tok = token_copy(nf_pair("mac", "call", "AAAAAAAA-0001"));
    char *full = token_copy(nf_pair("remote", "full", "phone-0001"));
    /* another client's ID with this token: refused, nothing starts */
    nf_resp_t r = wifi_call(mac_tok, "BBBBBBBB-0002", true);
    TB_EQ_INT(r.status, 403);
    TB_EQ_STR(nf_err(&r), "wrong_client");
    TB_EQ_STR(nf_str(r.j, "field"), "client");
    nf_free(&r);
    TB_FALSE(nf_app.call.active);
    /* its own client: the call starts, and revoking the token ends it */
    r = wifi_call(mac_tok, "AAAAAAAA-0001", true);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    TB_TRUE(nf_app.call.active);
    char id[9];
    token_id_of(full, "mac", NULL, id);
    TB_TRUE(id[0] != '\0');
    TB_EQ_INT(revoke(full, id), 200);
    TB_FALSE(nf_app.call.active);
    r = wifi_call(mac_tok, "AAAAAAAA-0001", true);
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
}

TB_TEST(sec_revoke_ends_a_call_from_a_token_paired_without_a_client)
{
    nf_setup();
    char *full = token_copy(nf_pair("remote", "full", NULL));
    char *script = token_copy(nf_pair("automation", "call", NULL));
    nf_resp_t r = wifi_call(script, "LINUXBOX-0001", true);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    TB_TRUE(nf_app.call.active);
    char id[9];
    token_id_of(full, "automation", NULL, id);
    TB_EQ_INT(revoke(full, id), 200);
    TB_FALSE(nf_app.call.active);       /* whatever client it named */
}

TB_TEST(sec_revoke_leaves_the_same_macs_usb_call)
{
    nf_setup();
    char *full = token_copy(nf_pair("remote", "full", NULL));
    char *mac_tok = token_copy(nf_pair("mac", "call", USB_MAC));
    nf_resp_t r = wifi_call(mac_tok, USB_MAC, true);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    /* the Mac is plugged in: the same client goes on over USB */
    usb_call(USB_MAC, true);
    char id[9];
    token_id_of(full, "mac", NULL, id);
    TB_EQ_INT(revoke(full, id), 200);
    TB_TRUE(nf_app.call.active);        /* api.md 12.2: over USB, the call carries on */
}

/* The review: a call-scope token paired by one device could change the call of a Mac it isn't (here the Mac on USB),
 * because the router trusted the `client` field. */
TB_TEST(sec_wifi_token_cannot_end_the_usb_macs_call)
{
    nf_setup();
    char line[256];
    snprintf(line, sizeof line, "@tb {\"cmd\": \"call\", \"id\": 1, \"client\": \"%s\", \"session\": \"s1\", \"seq\": 5, "
             "\"active\": true, \"call_id\": 3}", USB_MAC);
    cJSON *j = nf_usb(line);
    TB_TRUE(nf_true(j, "ok"));
    cJSON_Delete(j);
    TB_TRUE(nf_app.call.active);
    uint32_t call_id = nf_app.call.id;
    char *bound = token_copy(nf_pair("automation", "call", "script-0001"));
    char *unbound = token_copy(nf_pair("automation", "call", NULL));
    char body[256];
    /* end it, or restart it as a new call: refused for a token paired with another client... */
    snprintf(body, sizeof body, "{\"client\": \"%s\", \"session\": \"zz\", \"seq\": 1, \"active\": false}", USB_MAC);
    nf_resp_t r = nf_http("POST", "/api/v1/call", body, bound);
    TB_EQ_INT(r.status, 403);
    TB_EQ_STR(nf_err(&r), "wrong_client");
    nf_free(&r);
    /* ...and for one paired without a client, since the USB Mac holds that client */
    r = nf_http("POST", "/api/v1/call", body, unbound);
    TB_EQ_INT(r.status, 403);
    nf_free(&r);
    snprintf(body, sizeof body, "{\"client\": \"%s\", \"active\": true, \"call_id\": 99, \"leaving\": false}", USB_MAC);
    r = nf_http("POST", "/api/v1/call", body, unbound);
    TB_EQ_INT(r.status, 403);
    nf_free(&r);
    TB_TRUE(nf_app.call.active);
    TB_EQ_INT(nf_app.call.id, call_id);
    TB_EQ_INT(nf_app.call.via, TB_LINK_USB);
}

TB_TEST(sec_a_token_cannot_claim_a_client_paired_by_another_device)
{
    nf_setup();
    char *mac_tok = token_copy(nf_pair("mac", "call", "MACAPP-00000001"));
    char *script = token_copy(nf_pair("automation", "full", NULL));
    /* that Mac hasn't reported anything yet, but its client ID is its own */
    nf_resp_t r = wifi_call(script, "MACAPP-00000001", true);
    TB_EQ_INT(r.status, 403);
    TB_EQ_STR(nf_err(&r), "wrong_client");
    nf_free(&r);
    TB_FALSE(nf_app.call.active);
    r = wifi_call(mac_tok, "MACAPP-00000001", true);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    TB_TRUE(nf_app.call.active);
}

TB_TEST(sec_a_token_without_a_client_reports_for_one_mac_at_a_time)
{
    nf_setup();
    char *script = token_copy(nf_pair("automation", "call", NULL));
    usb_call(USB_MAC, true);
    uint32_t usb_call_id = nf_app.call.id;
    /* a script that keeps changing its client ID moves its one entry; it can't fill the 4-Mac table and push the
     * USB Mac's call out */
    static const char *ids[] = {"script-0001", "script-0002", "script-0003", "script-0004", "script-0005"};
    for (int i = 0; i < 5; i++) {
        nf_resp_t r = wifi_call(script, ids[i], true);
        TB_EQ_INT(r.status, 200);
        nf_free(&r);
    }
    nf_resp_t r = nf_http("GET", "/api/v1/status", NULL, script);
    TB_EQ_INT(cJSON_GetArraySize(nf_get(r.j, "macs")), 2);     /* the USB Mac and the script's latest */
    bool usb_kept = false;
    for (int i = 0; i < 2; i++) {
        char k[32], a[32];
        snprintf(k, sizeof k, "macs.%d.client", i);
        snprintf(a, sizeof a, "macs.%d.active", i);
        if (nf_str(r.j, k) && !strcmp(nf_str(r.j, k), USB_MAC)) usb_kept = nf_true(r.j, a);
    }
    TB_TRUE(usb_kept);
    TB_EQ_STR(nf_str(r.j, "macs.0.client"), "script-0005");
    nf_free(&r);
    /* the script's old entries are gone: their calls with them */
    TB_TRUE(nf_app.call.active);
    TB_TRUE(nf_app.call.id != usb_call_id);    /* the script's latest call is the one on screen (it started last) */
    r = wifi_call(script, "script-0005", false);
    nf_free(&r);
    TB_TRUE(nf_app.call.active);
    TB_EQ_INT(nf_app.call.via, TB_LINK_USB);  /* ...and when it ends, the USB Mac's call is still there */
}

TB_TEST(sec_a_full_table_keeps_the_macs_with_calls)
{
    net_macs_t t;
    net_macs_init(&t);
    tb_clock_t now = {.mono = 1000, .wall = 1791148320, .valid = true};
    net_call_msg_t m = {.client = USB_MAC, .active = true, .inputs = -1, .elapsed_s = -1, .via = TB_LINK_USB};
    net_macs_on_call(&t, &m, &now);
    static const char *ids[] = {"idle-0001", "idle-0002", "idle-0003", "idle-0004", "idle-0005", "idle-0006"};
    for (int i = 0; i < 6; i++) {
        now.mono += 1000;
        net_call_msg_t x = {.client = ids[i], .active = false, .inputs = -1, .elapsed_s = -1, .via = TB_LINK_WIFI,
                            .token_id = "0000000a"};
        net_macs_on_call(&t, &x, &now);
    }
    /* the least recently heard is the USB Mac, but it has a call: an idle one went instead */
    TB_TRUE(net_macs_find(&t, USB_MAC) != NULL);
    tb_call_t c;
    net_macs_aggregate(&t, &c, NULL, NULL);
    TB_TRUE(c.active);
    TB_EQ_INT(c.via, TB_LINK_USB);
}

TB_TEST(sec_forget_all_ends_wifi_calls_by_token_and_keeps_usb)
{
    nf_setup();
    char *script = token_copy(nf_pair("automation", "call", NULL));
    nf_resp_t r = wifi_call(script, "WIFIMAC-0001", true);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    TB_TRUE(nf_app.call.active);
    TB_EQ_INT(nf_app.call.via, TB_LINK_WIFI);
    net_api_forget_devices(&fake_now);
    TB_FALSE(nf_app.call.active);
    /* with a USB call on too, the USB one stays */
    nf_setup();
    script = token_copy(nf_pair("automation", "call", NULL));
    r = wifi_call(script, "WIFIMAC-0001", true);
    nf_free(&r);
    usb_call(USB_MAC, true);
    net_api_forget_devices(&fake_now);
    TB_TRUE(nf_app.call.active);
    TB_EQ_INT(nf_app.call.via, TB_LINK_USB);
    r = wifi_call(script, "WIFIMAC-0001", true);
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
}

TB_TEST(sec_usb_calls_need_no_token_and_may_take_any_client)
{
    nf_setup();
    char *mac_tok = token_copy(nf_pair("mac", "call", "MACAPP-00000001"));
    nf_resp_t r = wifi_call(mac_tok, "MACAPP-00000001", true);
    nf_free(&r);
    /* the same Mac plugged in: USB takes the client over, and switches back to Wi-Fi with its token */
    usb_call("MACAPP-00000001", true);
    TB_EQ_INT(nf_app.call.via, TB_LINK_USB);
    r = wifi_call(mac_tok, "MACAPP-00000001", true);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    TB_EQ_INT(nf_app.call.via, TB_LINK_WIFI);
}

/* ======================================================================================================== */
/* 5. The cookie's Origin                                                                                   */
/* ======================================================================================================== */

static const char *cookie_token(void)
{
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    char body[160];
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"%s\", \"cookie\": true}", nf_str(r.j, "pairing_id"),
             nf_app.pairing.code);
    nf_free(&r);
    nf_advance(1000);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    static char token[NET_TOKEN_LEN + 1];
    snprintf(token, sizeof token, "%.47s", r.r.set_cookie + 9);
    nf_free(&r);
    return token;
}

static int cookie_get(const char *token, const char *origin, bool unreadable)
{
    net_req_t req = {.via = NET_VIA_HTTP, .method = "GET", .path = "/api/v1/settings", .host = "minibar.local",
                     .cookie_token = token, .peer_ip = nf_ip, .origin = origin, .origin_unreadable = unreadable};
    nf_resp_t r = nf_req(&req);
    int st = r.status;
    if (st == 403) TB_EQ_STR(nf_err(&r), "bad_origin");
    nf_free(&r);
    return st;
}

TB_TEST(sec_cookie_with_foreign_origin_is_refused)
{
    nf_setup();
    const char *token = cookie_token();
    TB_EQ_INT(cookie_get(token, "http://minibar.local.evil.example", false), 403);
}

/* The review: net_http.c read Origin with a 255-byte cap, and a longer value (or no memory) came through as "no
 * Origin", which passes. Now it's told apart and refused. */
TB_TEST(sec_cookie_with_an_origin_that_cannot_be_read_is_refused)
{
    nf_setup();
    const char *token = cookie_token();
    TB_EQ_INT(cookie_get(token, NULL, true), 403);
    TB_EQ_INT(cookie_get(token, "", false), 403);          /* "Origin:" with nothing after it */
    TB_EQ_INT(cookie_get(token, NULL, false), 200);        /* none at all (a same-origin GET from some browsers) */
    TB_EQ_INT(cookie_get(token, "http://minibar.local", false), 200);
    /* a bearer token isn't held to the Origin rule (a page can't send one without CORS) */
    char *bearer = token_copy(nf_pair("automation", "full", NULL));
    net_req_t req = {.via = NET_VIA_HTTP, .method = "GET", .path = "/api/v1/settings", .host = "minibar.local",
                     .bearer = bearer, .peer_ip = nf_ip, .origin_unreadable = true};
    nf_resp_t r = nf_req(&req);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
}

/* ======================================================================================================== */
/* 8. Replies stay within 8 KB (api.md 2.5)                                                                 */
/* ======================================================================================================== */

static void busy_day(int n, char fill)
{
    fake_cal.saved = true;
    fake_cal.last_sync = fake_now.wall - 60;
    nf_app.cal_saved = true;
    nf_app.set.automatic.calendar = true;
    nf_app.set.automatic.meeting_titles = true;
    static tb_meeting_t m[TB_MEETINGS_MAX];
    memset(m, 0, sizeof m);
    for (int i = 0; i < n; i++) {
        m[i].id = (uint32_t)(i + 1);
        m[i].start = fake_now.wall + 60 * (i + 1);
        m[i].end = m[i].start + 30;
        memset(m[i].title, fill, sizeof m[i].title - 1);          /* a long title, as a shared calendar can have */
        memset(m[i].location, fill, sizeof m[i].location - 1);
    }
    tb_app_set_meetings(&nf_app, m, n, &fake_now);
}

/* The review: GET /api/v1/calendar put up to 32 of today's meetings with full titles and locations in one reply,
 * which can pass 8 KB on a busy day with titles on, and the router turned that into 500 internal. */
TB_TEST(sec_calendar_reply_fits_on_a_busy_day)
{
    nf_setup();
    char *full = token_copy(nf_pair("remote", "full", NULL));
    busy_day(TB_MEETINGS_MAX, 'T');
    nf_resp_t r = nf_http("GET", "/api/v1/calendar", NULL, full);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(r.r.len <= NET_REPLY_MAX);
    /* the list is cut, in order, and left_today still counts them all */
    int listed = cJSON_GetArraySize(nf_get(r.j, "calendar.today"));
    TB_TRUE(listed > 0 && listed < TB_MEETINGS_MAX);
    TB_EQ_INT(nf_num(r.j, "calendar.left_today"), TB_MEETINGS_MAX);
    TB_TRUE(r.r.len > NET_REPLY_MAX - 700);   /* ...and it doesn't stop much short of the limit */
    printf("    %d of %d meetings listed, %zu bytes\n", listed, TB_MEETINGS_MAX, r.r.len);
    nf_free(&r);
}

TB_TEST(sec_calendar_reply_fits_with_quotes_doubling_every_title_and_over_usb)
{
    nf_setup();
    char *full = token_copy(nf_pair("remote", "full", NULL));
    busy_day(TB_MEETINGS_MAX, '"');          /* each '"' prints as \" */
    nf_resp_t r = nf_http("GET", "/api/v1/calendar", NULL, full);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(r.r.len <= NET_REPLY_MAX);
    TB_TRUE(cJSON_GetArraySize(nf_get(r.j, "calendar.today")) > 0);
    nf_free(&r);
    cJSON *j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 7, \"method\": \"GET\", \"path\": \"/api/v1/calendar\"}");
    TB_EQ_INT(nf_num(j, "http_status"), 200);
    TB_TRUE(strlen(nf_last_line) <= NET_REPLY_MAX + 4);
    cJSON_Delete(j);
    /* a normal day lists everything */
    busy_day(20, 'a');
    for (int i = 0; i < 20; i++) nf_app.meetings[i].title[30] = nf_app.meetings[i].location[20] = '\0';
    r = nf_http("GET", "/api/v1/calendar", NULL, full);
    TB_EQ_INT(cJSON_GetArraySize(nf_get(r.j, "calendar.today")), 20);
    nf_free(&r);
}

TB_TEST(sec_status_reply_fits_at_its_largest)
{
    nf_setup();
    char *full = token_copy(nf_pair("remote", "full", NULL));
    busy_day(TB_MEETINGS_MAX, '"');
    /* the longest message of characters that print longest */
    char msg[600] = "{\"text\": \"";
    for (int i = 0; i < TB_MESSAGE_MAX_CHARS; i++) strcat(msg, "\\\"");
    strcat(msg, "\"}");
    nf_resp_t r = nf_http("POST", "/api/v1/message", msg, full);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    /* four Macs with the longest client IDs, names and app names */
    for (int i = 0; i < NET_MACS_MAX; i++) {
        char line[512], client[65];
        memset(client, 'A' + i, 64);
        client[64] = '\0';
        snprintf(line, sizeof line, "@tb {\"cmd\": \"hello\", \"id\": %d, \"api\": \"1.0\", \"client\": \"%s\", "
                 "\"name\": \"%s\"}", i + 1, client, "\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"");
        cJSON_Delete(nf_usb(line));
        snprintf(line, sizeof line, "@tb {\"cmd\": \"call\", \"id\": %d, \"client\": \"%s\", \"active\": true, "
                 "\"app\": \"%s\"}", i + 10, client,
                 "\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\""
                 "\\\"\\\"\\\"\\\"\\\"\\\"\\\"\\\"");
        cJSON_Delete(nf_usb(line));
    }
    r = nf_http("GET", "/api/v1/status", NULL, full);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(r.r.len <= NET_REPLY_MAX);
    printf("    the largest status reply: %zu bytes\n", r.r.len);
    nf_free(&r);
}

/* ======================================================================================================== */
/* 4 and the nit: the setup network's SSID and password                                                     */
/* ======================================================================================================== */

static nf_resp_t setup_wifi(const char *body)
{
    net_req_t req = {.via = NET_VIA_SETUP, .method = "POST", .path = "/api/v1/setup/wifi", .body = body,
                     .body_len = strlen(body), .content_type_json = true, .host = NET_SETUP_IP, .peer_ip = 0x02020304};
    return nf_req(&req);
}

static void setup_mode(void)
{
    nf_setup();
    fake_wifi = (net_wifi_info_t){.state = NET_WIFI_SETUP, .host = "minibar.local"};
    nf_app.wifi_mode = TB_WIFI_SETUP;
}

/* An SSID can carry a line break from the open setup page, and the bar logs and shows it. */
TB_TEST(sec_setup_refuses_control_characters_in_the_ssid)
{
    setup_mode();
    static const char *bad[] = {
        "{\"ssid\": \"Office\\n@tb {\\\"event\\\": \\\"ready\\\"}\", \"password\": \"longenough\"}",
        "{\"ssid\": \"Office\\r\", \"password\": \"longenough\"}",
        "{\"ssid\": \"Off\\u001bice\", \"password\": \"longenough\"}",
        "{\"ssid\": \"Off\\u007fice\", \"password\": \"longenough\"}",
        "{\"ssid\": \"Off\\u0085ice\", \"password\": \"longenough\"}",     /* NEL, a C1 control */
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        nf_resp_t r = setup_wifi(bad[i]);
        TB_EQ_INT(r.status, 400);
        TB_EQ_STR(nf_err(&r), "bad_value");
        TB_EQ_STR(nf_str(r.j, "field"), "ssid");
        nf_free(&r);
    }
    TB_EQ_INT(fake_join_calls, 0);
    /* names with spaces, punctuation and accents are fine */
    nf_resp_t r = setup_wifi("{\"ssid\": \"Caf\\u00e9 @ 2nd floor\", \"password\": \"longenough\"}");
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
}

TB_TEST(sec_setup_password_rule_matches_its_message)
{
    setup_mode();
    char body[256];
    /* 63 characters, and 64 hex digits (the key itself, WPA2's other form), are accepted */
    snprintf(body, sizeof body, "{\"ssid\": \"Office-WiFi\", \"password\": \"%.63s\"}",
             "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
    nf_resp_t r = setup_wifi(body);
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    nf_app.wifi_mode = TB_WIFI_SETUP;
    snprintf(body, sizeof body, "{\"ssid\": \"Office-WiFi\", \"password\": \"%s\"}",
             "0123456789abcdefABCDEF0123456789abcdef0123456789abcdef0123456789");
    r = setup_wifi(body);
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_INT(strlen(fake_join_pass), 64);
    /* 64 characters that aren't all hex digits aren't a WPA password */
    nf_app.wifi_mode = TB_WIFI_SETUP;
    snprintf(body, sizeof body, "{\"ssid\": \"Office-WiFi\", \"password\": \"%s\"}",
             "0123456789abcdefABCDEF0123456789abcdef0123456789abcdef012345678g");
    r = setup_wifi(body);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_str(r.j, "field"), "password");
    TB_EQ_STR(nf_str(r.j, "message"), "Wi-Fi passwords are 8 to 63 characters, or 64 hex digits.");
    nf_free(&r);
    r = setup_wifi("{\"ssid\": \"Office-WiFi\", \"password\": \"1234567\"}");
    TB_EQ_INT(r.status, 400);
    nf_free(&r);
}

/* ======================================================================================================== */
/* 4. The log never writes the marker at a line's start (api.md 6.4)                                        */
/* ======================================================================================================== */

/* log_write's loop (esp/net_usb.c), writing into a buffer instead of the port. */
static void log_out(net_log_state_t *st, const char *buf, size_t len, char *wire, size_t *w)
{
    size_t i = 0;
    while (i < len) {
        size_t k = net_log_scan(st, buf + i, len - i);
        memcpy(wire + *w, buf + i, k);
        *w += k;
        i += k;
        if (i < len) {
            wire[(*w)++] = ' ';
            *st = NET_LOG_MID;
        }
    }
}

/* What the Mac app does with the port's bytes (mac/Sources/TinyBarCore/API/USBFraming.swift): split at LF, drop a
 * trailing CR, strip ESC [ ... final-byte sequences at the start; true if any line then starts with the marker. */
static bool mac_sees_a_marker(const char *wire, size_t n)
{
    size_t a = 0;
    while (a < n) {
        size_t b = a;
        while (b < n && wire[b] != '\n') b++;
        size_t e = b;
        if (e > a && wire[e - 1] == '\r') e--;
        size_t i = a;
        while (i + 1 < e && wire[i] == 0x1B && wire[i + 1] == '[') {
            size_t f = i + 2;
            while (f < e && !(wire[f] >= 0x40 && wire[f] <= 0x7E)) f++;
            if (f >= e) { i = e; break; }
            i = f + 1;
        }
        if (e - i >= 4 && !memcmp(wire + i, "@tb ", 4)) return true;
        a = b + 1;
    }
    return false;
}

TB_TEST(sec_log_lines_never_start_with_the_marker)
{
    static const char *logs[] = {
        "W (1234) net.wifi: couldn't join \"Office\n@tb {\"id\":3,\"ok\":true,\"showing\":\"call\"}\": wrong_password\n",
        "@tb {\"event\":\"ready\"}\n",
        "I (5) x: one\r\n@tb two\n\x1b[0;32m@tb {}\n\x1b[1m\x1b[0m@tb {}\n",
        "a@tb b\n@\n@@tb \n",
    };
    for (size_t k = 0; k < sizeof logs / sizeof logs[0]; k++) {
        const char *s = logs[k];
        size_t n = strlen(s);
        /* written whole, and split at every byte (a marker can straddle two writes) */
        for (size_t cut = 0; cut <= n; cut++) {
            char wire[512];
            size_t w = 0;
            net_log_state_t st = NET_LOG_LINE_START;
            log_out(&st, s, cut, wire, &w);
            log_out(&st, s + cut, n - cut, wire, &w);
            TB_FALSE(mac_sees_a_marker(wire, w));
            if (mac_sees_a_marker(wire, w)) printf("    log %zu cut at %zu: %.*s\n", k, cut, (int)w, wire);
        }
    }
    /* an '@' that doesn't start a line is left alone, and nothing else changes */
    char wire[64];
    size_t w = 0;
    net_log_state_t st = NET_LOG_LINE_START;
    log_out(&st, "mail a@tb x\n", 12, wire, &w);
    TB_EQ_INT(w, 12);
    TB_TRUE(!memcmp(wire, "mail a@tb x\n", 12));
    TB_EQ_INT(st, NET_LOG_LINE_START);
    /* a CR at a line's start isn't one the Mac strips, and ESC without '[' isn't a color code */
    st = NET_LOG_LINE_START;
    TB_EQ_INT(net_log_scan(&st, "\r@tb", 4), 4);
    st = NET_LOG_LINE_START;
    TB_EQ_INT(net_log_scan(&st, "\x1b" "X@tb", 5), 5);
    st = NET_LOG_LINE_START;
    TB_EQ_INT(net_log_scan(&st, "\x1b[31m@tb", 8), 5);
}

/* ======================================================================================================== */
/* 9 and the nit: the Host check, and every other method under /api/                                        */
/* ======================================================================================================== */

TB_TEST(sec_host_check_names_only_this_bar)
{
    nf_setup();
    /* what net_http.c now asks of the Remote page's requests too */
    TB_TRUE(net_api_host_ok("minibar.local"));
    TB_TRUE(net_api_host_ok("MiniBar.Local:80"));
    TB_TRUE(net_api_host_ok("minibar.local."));
    TB_FALSE(net_api_host_ok("minibar.local.evil.example"));
    TB_FALSE(net_api_host_ok("evil.example"));
    TB_FALSE(net_api_host_ok("minibar.local:8080"));
    TB_FALSE(net_api_host_ok(""));
    TB_FALSE(net_api_host_ok(NULL));
    TB_FALSE(net_api_host_ok(NET_SETUP_IP));      /* only while in setup */
}

TB_TEST(sec_other_methods_get_the_json_405_with_allow)
{
    nf_setup();
    static const char *methods[] = {"OPTIONS", "HEAD", "PROPFIND", "TRACE"};
    for (size_t i = 0; i < sizeof methods / sizeof methods[0]; i++) {
        net_req_t req = {.via = NET_VIA_HTTP, .method = methods[i], .path = "/api/v1/status", .host = "minibar.local",
                         .peer_ip = nf_ip};
        nf_resp_t r = nf_req(&req);
        TB_EQ_INT(r.status, 405);
        TB_EQ_STR(nf_err(&r), "method_not_allowed");
        TB_EQ_STR(r.r.allow, "GET, POST");
        nf_free(&r);
    }
    /* a wrong host still comes first */
    net_req_t req = {.via = NET_VIA_HTTP, .method = "OPTIONS", .path = "/api/v1/status", .host = "evil.example",
                     .peer_ip = nf_ip};
    nf_resp_t r = nf_req(&req);
    TB_EQ_INT(r.status, 421);
    nf_free(&r);
}
