/*
 * test_api_http.c: the router over HTTP: the checks every request goes through (host, rate limits, paths, methods,
 * tokens, scopes, cookie and origin, body size, content type, JSON), and the status, ETag and info replies.
 * Owner: net builder.
 */
#include <stdlib.h>

#include "net_fixture.h"
#include "net_pair.h"
#include "tb_test.h"

/* ---------- Host (api.md 2.2) ---------- */

TB_TEST(http_host_check)
{
    nf_setup();
    const char *good[] = {"tinybar.local", "tinybar.local:80", "TinyBar.Local", "tinybar.local.", "10.0.4.42", "10.0.4.42:80"};
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        nf_host = good[i];
        nf_resp_t r = nf_http("GET", "/api/v1/info", NULL, NULL);
        TB_EQ_INT(r.status, 200);
        nf_free(&r);
    }
    const char *bad[] = {"evil.example", "tinybar.local:8080", "tinybar.locals", "10.0.4.4", "192.168.4.1", "", "tinybar"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        nf_host = bad[i];
        nf_resp_t r = nf_http("GET", "/api/v1/info", NULL, NULL);
        TB_EQ_INT(r.status, 421);
        TB_EQ_STR(nf_err(&r), "wrong_host");
        TB_FALSE(nf_true(r.j, "ok"));
        TB_TRUE(nf_str(r.j, "message") != NULL);
        TB_TRUE(nf_null(r.j, "field"));
        nf_free(&r);
    }
    nf_host = NULL;
    nf_resp_t r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_INT(r.status, 421);
    nf_free(&r);
    /* after an mDNS conflict the bar answers to its new name only */
    snprintf(fake_wifi.host, sizeof fake_wifi.host, "tinybar-2.local");
    nf_host = "tinybar.local";
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_INT(r.status, 421);
    nf_free(&r);
    nf_host = "tinybar-2.local";
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "host"), "tinybar-2.local");
    nf_free(&r);
    /* 192.168.4.1 only while the setup network is up */
    fake_wifi.state = NET_WIFI_SETUP;
    nf_host = "192.168.4.1";
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    TB_TRUE(net_api_host_ok("192.168.4.1:80"));
    TB_FALSE(net_api_host_ok("captive.apple.com"));
}

/* ---------- paths and methods ---------- */

TB_TEST(http_unknown_paths_and_methods)
{
    nf_setup();
    nf_resp_t r = nf_http("GET", "/api/v1/nothing", NULL, NULL);
    TB_EQ_INT(r.status, 404);
    TB_EQ_STR(nf_err(&r), "not_found");
    nf_free(&r);
    r = nf_http("POST", "/api/call", "{\"active\": true}", NULL);       /* the mock-up's unversioned path is gone */
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    r = nf_http("GET", "/api/v1/status/", NULL, NULL);
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    r = nf_http("DELETE", "/api/v1/status", NULL, NULL);
    TB_EQ_INT(r.status, 405);
    TB_EQ_STR(nf_err(&r), "method_not_allowed");
    TB_EQ_STR(r.r.allow, "GET, POST");
    nf_free(&r);
    r = nf_http("GET", "/api/v1/calendar/sync", NULL, NULL);
    TB_EQ_INT(r.status, 405);
    TB_EQ_STR(r.r.allow, "POST");
    nf_free(&r);
    r = nf_http("PUT", "/api/v1/settings", "{}", NULL);
    TB_EQ_INT(r.status, 405);
    TB_EQ_STR(r.r.allow, "GET, PATCH");
    nf_free(&r);
    r = nf_http("GET", "/api/v1/clients/self", NULL, NULL);
    TB_EQ_INT(r.status, 405);
    TB_EQ_STR(r.r.allow, "DELETE");
    nf_free(&r);
    /* a query string doesn't change the route */
    r = nf_http("GET", "/api/v1/info?x=1", NULL, NULL);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
}

/* ---------- info (api.md 7.1) ---------- */

TB_TEST(http_info_is_open_and_complete)
{
    nf_setup();
    nf_resp_t r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(nf_true(r.j, "ok"));
    TB_EQ_STR(nf_str(r.j, "device"), "TinyBar");
    TB_EQ_STR(nf_str(r.j, "device_id"), "f412fa3f2a1c");
    TB_EQ_STR(nf_str(r.j, "name"), "TinyBar 2A1C");
    TB_EQ_STR(nf_str(r.j, "fw"), "1.0.0");
    TB_EQ_STR(nf_str(r.j, "api"), "1.0");
    TB_EQ_STR(nf_str(r.j, "host"), "tinybar.local");
    TB_EQ_STR(nf_str(r.j, "auth"), "bearer");
    TB_EQ_STR(nf_str(r.j, "pairing"), "idle");
    TB_EQ_INT(nf_num(r.j, "heartbeat_s"), 30);
    TB_EQ_INT(nf_num(r.j, "timeout_s"), 90);
    TB_TRUE(nf_str(r.j, "time") != NULL);
    TB_EQ_STR(nf_str(r.j, "time_source"), "ntp");
    TB_EQ_STR(nf_str(r.j, "wifi"), "connected");
    nf_free(&r);
    /* no clock yet */
    fake_now.valid = false;
    fake_time_source = NET_TIME_NONE;
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_TRUE(nf_null(r.j, "time"));
    TB_EQ_STR(nf_str(r.j, "time_source"), "none");
    nf_free(&r);
}

TB_TEST(http_info_time_is_rfc3339_with_the_offset)
{
    nf_setup();
    nf_resp_t r = nf_http("GET", "/api/v1/info", NULL, NULL);
    const char *t = nf_str(r.j, "time");
    TB_TRUE(t && strlen(t) == 25);
    TB_TRUE(t && !strncmp(t, "2026-10-04T14:12:0", 18) && !strcmp(t + 19, "-07:00"));
    nf_free(&r);
}

/* ---------- tokens and scopes (api.md 4.4, 4.5) ---------- */

TB_TEST(http_endpoints_need_a_token)
{
    nf_setup();
    const struct { const char *m, *p, *b; } eps[] = {
        {"GET", "/api/v1/status", NULL}, {"POST", "/api/v1/call", "{}"}, {"POST", "/api/v1/status", "{}"},
        {"POST", "/api/v1/message", "{}"}, {"POST", "/api/v1/aside", "{}"}, {"POST", "/api/v1/pomodoro", "{}"},
        {"GET", "/api/v1/settings", NULL}, {"PATCH", "/api/v1/settings", "{}"}, {"GET", "/api/v1/calendar", NULL},
        {"PUT", "/api/v1/calendar", "{}"}, {"DELETE", "/api/v1/calendar", NULL}, {"POST", "/api/v1/calendar/sync", NULL},
        {"GET", "/api/v1/clients", NULL}, {"DELETE", "/api/v1/clients/self", NULL}, {"DELETE", "/api/v1/clients/12345678", NULL},
    };
    for (size_t i = 0; i < sizeof eps / sizeof eps[0]; i++) {
        nf_resp_t r = nf_http(eps[i].m, eps[i].p, eps[i].b, NULL);
        TB_EQ_INT(r.status, 401);
        TB_EQ_STR(nf_err(&r), "unauthorized");
        TB_EQ_STR(nf_str(r.j, "message"), "Pair with this TinyBar first.");
        TB_TRUE(r.r.www_authenticate);
        nf_free(&r);
        nf_advance(300);
    }
    /* a made-up token is the same as none */
    nf_resp_t r = nf_http("GET", "/api/v1/status", NULL, "tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4");
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
}

TB_TEST(http_call_scope_reaches_only_its_endpoints)
{
    nf_setup();
    const char *tok = nf_pair("mac", "call", "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60");
    TB_TRUE(tok != NULL);
    char t[NET_TOKEN_LEN + 1];
    snprintf(t, sizeof t, "%s", tok);
    nf_resp_t r = nf_http("GET", "/api/v1/status", NULL, t);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    r = nf_http("POST", "/api/v1/call", "{\"client\": \"6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60\", \"active\": false}", t);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    r = nf_http("GET", "/api/v1/info", NULL, t);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    r = nf_http("POST", "/api/v1/status", "{\"status\": \"busy\"}", t);
    TB_EQ_INT(r.status, 403);
    TB_EQ_STR(nf_err(&r), "wrong_scope");
    nf_free(&r);
    r = nf_http("GET", "/api/v1/clients", NULL, t);
    TB_EQ_INT(r.status, 403);
    nf_free(&r);
    /* it can unpair itself */
    r = nf_http("DELETE", "/api/v1/clients/self", NULL, t);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(nf_str(r.j, "revoked") != NULL);
    nf_free(&r);
    r = nf_http("GET", "/api/v1/status", NULL, t);
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
}

TB_TEST(http_cookie_and_origin)
{
    nf_setup();
    /* the Remote pairs with "cookie": true */
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"name\": \"iPhone\", \"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 202);
    char body[160];
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"%s\", \"cookie\": true}", nf_str(r.j, "pairing_id"),
             nf_app.pairing.code);
    nf_free(&r);
    nf_advance(1000);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(nf_null(r.j, "token"));
    TB_EQ_STR(nf_str(r.j, "scope"), "full");
    const char *sc = r.r.set_cookie;
    TB_TRUE(!strncmp(sc, "tb_token=tb1_", 13));
    TB_TRUE(strstr(sc, "; HttpOnly; SameSite=Strict; Path=/api/; Max-Age=31536000") != NULL);
    char token[NET_TOKEN_LEN + 1];
    snprintf(token, sizeof token, "%.47s", sc + 9);
    nf_free(&r);

    net_req_t req = {.via = NET_VIA_HTTP, .method = "GET", .path = "/api/v1/status", .host = "tinybar.local",
                     .cookie_token = token, .peer_ip = nf_ip};
    r = nf_req(&req);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    req.origin = "http://tinybar.local";
    r = nf_req(&req);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    req.origin = "http://10.0.4.42:80";
    r = nf_req(&req);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    req.origin = "https://evil.example";
    r = nf_req(&req);
    TB_EQ_INT(r.status, 403);
    TB_EQ_STR(nf_err(&r), "bad_origin");
    nf_free(&r);
    req.origin = "null";
    r = nf_req(&req);
    TB_EQ_INT(r.status, 403);
    nf_free(&r);
    /* a bearer token skips the cookie's origin rule (pages can't send one without CORS, which the bar never allows) */
    req.cookie_token = NULL;
    req.bearer = token;
    r = nf_req(&req);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    /* forgetting this phone clears the cookie */
    req.bearer = NULL;
    req.cookie_token = token;
    req.origin = "http://tinybar.local";
    req.method = "DELETE";
    req.path = "/api/v1/clients/self";
    r = nf_req(&req);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(r.r.set_cookie, "tb_token=; Max-Age=0; Path=/api/");
    nf_free(&r);
}

TB_TEST(http_auth_none_mode)
{
    nf_setup();
    net_api_set_auth(false);
    nf_resp_t r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "auth"), "none");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/status", "{\"status\": \"busy\"}", NULL);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "own.status"), "busy");
    nf_free(&r);
    /* tokens sent are ignored, even made-up ones */
    r = nf_http("GET", "/api/v1/settings", NULL, "tb1_w1rV1lN4jm2ohruSAozMZxVlcceAL7yS8r45__-ref4");
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    /* pairing still works, so a client that pairs anyway isn't stuck */
    TB_TRUE(nf_pair("mac", "call", NULL) != NULL);
    /* the unauthenticated rate limit doesn't apply: 15 quick requests from one address */
    for (int i = 0; i < 15; i++) {
        net_req_t req = {.via = NET_VIA_HTTP, .method = "GET", .path = "/api/v1/status", .host = "tinybar.local", .peer_ip = 77};
        nf_resp_t x;
        memset(&x, 0, sizeof x);
        net_api_handle(&req, &x.r);
        TB_EQ_INT(x.r.status, 200);
        free(x.r.body);
    }
}

/* ---------- rate limits (api.md 2.5) ---------- */

TB_TEST(http_rate_limits)
{
    nf_setup();
    const char *tok = nf_pair("remote", "full", NULL);
    char t[NET_TOKEN_LEN + 1];
    snprintf(t, sizeof t, "%s", tok);
    /* 20 at once with a token, then 429 */
    int ok = 0, limited = 0, retry = 0;
    for (int i = 0; i < 25; i++) {
        net_req_t req = {.via = NET_VIA_HTTP, .method = "GET", .path = "/api/v1/status", .host = "tinybar.local", .bearer = t, .peer_ip = 55};
        net_resp_t resp;
        net_api_handle(&req, &resp);
        if (resp.status == 200) ok++;
        if (resp.status == 429) {
            limited++;
            retry = resp.retry_after_s;
            TB_TRUE(resp.body && strstr(resp.body, "\"error\":\"rate_limited\"") && strstr(resp.body, "\"retry_after_s\":1"));
        }
        free(resp.body);
    }
    TB_EQ_INT(ok, 20);
    TB_EQ_INT(limited, 5);
    TB_EQ_INT(retry, 1);
    /* without a token: 5 */
    ok = 0;
    for (int i = 0; i < 8; i++) {
        net_req_t req = {.via = NET_VIA_HTTP, .method = "GET", .path = "/api/v1/info", .host = "tinybar.local", .peer_ip = 56};
        net_resp_t resp;
        net_api_handle(&req, &resp);
        ok += resp.status == 200;
        free(resp.body);
    }
    TB_EQ_INT(ok, 5);
    /* USB has no rate limit */
    for (int i = 0; i < 40; i++) {
        cJSON *j = nf_usb("@tb {\"cmd\": \"status\", \"id\": 1}");
        TB_TRUE(nf_true(j, "ok"));
        cJSON_Delete(j);
    }
}

/* ---------- the body (api.md 2.2, 2.5) ---------- */

TB_TEST(http_body_checks)
{
    nf_setup();
    const char *tok = nf_pair("remote", "full", NULL);
    char t[NET_TOKEN_LEN + 1];
    snprintf(t, sizeof t, "%s", tok);
    net_req_t req = {.via = NET_VIA_HTTP, .method = "POST", .path = "/api/v1/status", .host = "tinybar.local", .bearer = t,
                     .peer_ip = nf_ip, .body = "status=busy", .body_len = 11, .content_type_json = false};
    nf_resp_t r = nf_req(&req);
    TB_EQ_INT(r.status, 415);
    TB_EQ_STR(nf_err(&r), "unsupported_media_type");
    nf_free(&r);
    req.content_type_json = true;
    req.body_too_large = true;
    r = nf_req(&req);
    TB_EQ_INT(r.status, 413);
    TB_EQ_STR(nf_err(&r), "too_large");
    nf_free(&r);
    req.body_too_large = false;
    const char *bad[] = {"status=busy", "[1, 2]", "\"busy\"", "{\"status\": \"busy\"} {}", "{\"status\": ", "\xEF\xBB\xBF{}", "null"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        req.body = bad[i];
        req.body_len = strlen(bad[i]);
        r = nf_req(&req);
        TB_EQ_INT(r.status, 400);
        TB_EQ_STR(nf_err(&r), "bad_json");
        TB_EQ_STR(nf_str(r.j, "message"), "The body isn't a JSON object.");
        nf_free(&r);
    }
    /* an empty body where one is needed */
    req.body = NULL;
    req.body_len = 0;
    r = nf_req(&req);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_err(&r), "bad_json");
    nf_free(&r);
    /* whitespace after the object is fine; unknown fields are ignored */
    req.body = "{\"status\": \"busy\", \"mood\": \"great\"}\r\n";
    req.body_len = strlen(req.body);
    r = nf_req(&req);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    /* the path's 404 comes before the body's problems */
    req.path = "/api/v1/nope";
    req.content_type_json = false;
    r = nf_req(&req);
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
}

/* ---------- status (api.md 7.3) and its ETag (7.2) ---------- */

TB_TEST(http_status_has_every_field)
{
    nf_setup();
    const char *tok = nf_pair("remote", "full", NULL);
    char t[NET_TOKEN_LEN + 1];
    snprintf(t, sizeof t, "%s", tok);
    nf_resp_t r = nf_http("GET", "/api/v1/status", NULL, t);
    TB_EQ_INT(r.status, 200);
    const char *paths[] = {
        "ok", "device_id", "rev", "time", "time_source", "showing", "screen", "own.status", "own.since", "own.previous",
        "message.text", "message.set_at", "away.back_at", "away.note", "call.active", "call.app", "call.inputs", "call.via",
        "call.since", "call.aside", "meeting.active", "meeting.aside", "meeting.current", "meeting.next",
        "meeting.left_today", "pomodoro.state", "pomodoro.phase", "pomodoro.round", "pomodoro.rounds", "pomodoro.length_s",
        "pomodoro.remaining_s", "pomodoro.ends_at", "pomodoro.paused_by", "pomodoro.ringing", "pomodoro.done_today",
        "pomodoro.focused_today_s", "sources.calendar", "sources.mac", "macs", "calendar.saved", "calendar.last_sync",
        "calendar.syncing", "calendar.error", "wifi.state", "wifi.ssid", "wifi.ip", "wifi.host", "wifi.rssi",
    };
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        if (!nf_get(r.j, paths[i])) printf("    missing %s\n", paths[i]);
        TB_TRUE(nf_get(r.j, paths[i]) != NULL);
    }
    TB_EQ_STR(nf_str(r.j, "showing"), "own");
    TB_EQ_STR(nf_str(r.j, "screen"), "on");
    TB_EQ_STR(nf_str(r.j, "own.status"), "clock");          /* the proposed first-boot status */
    TB_EQ_STR(nf_str(r.j, "own.previous"), "available");
    TB_TRUE(nf_null(r.j, "message.text"));
    TB_FALSE(nf_true(r.j, "call.active"));
    TB_TRUE(nf_null(r.j, "call.app"));
    TB_TRUE(nf_null(r.j, "meeting.left_today"));             /* no calendar saved */
    TB_EQ_STR(nf_str(r.j, "pomodoro.state"), "ready");
    TB_EQ_STR(nf_str(r.j, "pomodoro.phase"), "focus");
    TB_EQ_INT(nf_num(r.j, "pomodoro.round"), 1);
    TB_EQ_INT(nf_num(r.j, "pomodoro.rounds"), 4);
    TB_EQ_INT(nf_num(r.j, "pomodoro.length_s"), 1500);
    TB_EQ_INT(nf_num(r.j, "pomodoro.remaining_s"), 1500);
    TB_TRUE(nf_null(r.j, "pomodoro.ends_at"));
    TB_TRUE(nf_null(r.j, "pomodoro.paused_by"));
    TB_FALSE(nf_true(r.j, "sources.calendar"));
    TB_TRUE(nf_true(r.j, "sources.mac"));
    TB_EQ_INT(cJSON_GetArraySize(nf_get(r.j, "macs")), 0);
    TB_FALSE(nf_true(r.j, "calendar.saved"));
    TB_EQ_STR(nf_str(r.j, "wifi.state"), "connected");
    TB_EQ_STR(nf_str(r.j, "wifi.ssid"), "Office-WiFi");
    TB_EQ_STR(nf_str(r.j, "wifi.ip"), "10.0.4.42");
    TB_EQ_INT(nf_num(r.j, "wifi.rssi"), -61);
    TB_TRUE(strlen(r.r.body) <= NET_REPLY_MAX);
    nf_free(&r);
}

TB_TEST(http_status_etag_and_304)
{
    nf_setup();
    const char *tok = nf_pair("remote", "full", NULL);
    char t[NET_TOKEN_LEN + 1];
    snprintf(t, sizeof t, "%s", tok);
    nf_resp_t r = nf_http("GET", "/api/v1/status", NULL, t);
    char etag[16];
    snprintf(etag, sizeof etag, "%s", r.r.etag);
    char want[16];
    snprintf(want, sizeof want, "\"r%d\"", (int)nf_num(r.j, "rev"));
    TB_EQ_STR(etag, want);
    nf_free(&r);
    /* nothing changed: 304 and no body, even as time passes */
    nf_advance(5000);
    net_req_t req = {.via = NET_VIA_HTTP, .method = "GET", .path = "/api/v1/status", .host = "tinybar.local", .bearer = t,
                     .peer_ip = nf_ip, .if_none_match = etag};
    r = nf_req(&req);
    TB_EQ_INT(r.status, 304);
    TB_TRUE(r.r.body == NULL);
    TB_EQ_STR(r.r.etag, etag);
    nf_free(&r);
    /* a running timer's countdown doesn't change rev */
    r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"start\"}", t);
    int rev_running = (int)nf_num(r.j, "rev");
    nf_free(&r);
    nf_advance(3000);
    r = nf_http("GET", "/api/v1/status", NULL, t);
    TB_EQ_INT(nf_num(r.j, "rev"), rev_running);
    TB_TRUE(nf_num(r.j, "pomodoro.remaining_s") < 1500);
    snprintf(etag, sizeof etag, "%s", r.r.etag);
    nf_free(&r);
    /* a change gives a new, higher rev */
    r = nf_http("POST", "/api/v1/status", "{\"status\": \"busy\"}", t);
    TB_TRUE(nf_num(r.j, "rev") > rev_running);
    nf_free(&r);
    r = nf_req(&req);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    /* a list of tags, or W/, still matches */
    r = nf_http("GET", "/api/v1/status", NULL, t);
    char list[64];
    snprintf(list, sizeof list, "\"r1\", W/%s", r.r.etag);
    nf_free(&r);
    req.if_none_match = list;
    r = nf_req(&req);
    TB_EQ_INT(r.status, 304);
    nf_free(&r);
}

TB_TEST(http_status_rev_sees_mac_and_calendar_changes)
{
    nf_setup();
    net_api_set_auth(false);
    nf_resp_t r = nf_http("GET", "/api/v1/status", NULL, NULL);
    int rev = (int)nf_num(r.j, "rev");
    nf_free(&r);
    /* a Mac connects: macs changes */
    r = nf_http("POST", "/api/v1/call", "{\"client\": \"abcdefgh\", \"active\": false}", NULL);
    nf_free(&r);
    r = nf_http("GET", "/api/v1/status", NULL, NULL);
    TB_TRUE(nf_num(r.j, "rev") > rev);
    rev = (int)nf_num(r.j, "rev");
    nf_free(&r);
    /* its heartbeat alone (only last_heard moves) doesn't */
    nf_advance(30000);
    r = nf_http("POST", "/api/v1/call", "{\"client\": \"abcdefgh\", \"active\": false}", NULL);
    nf_free(&r);
    r = nf_http("GET", "/api/v1/status", NULL, NULL);
    TB_EQ_INT(nf_num(r.j, "rev"), rev);
    nf_free(&r);
    /* the calendar's status (owned by the calendar service) */
    fake_cal.syncing = true;
    r = nf_http("GET", "/api/v1/status", NULL, NULL);
    TB_TRUE(nf_num(r.j, "rev") > rev);
    nf_free(&r);
}

TB_TEST(http_busy_while_starting_up)
{
    nf_setup();
    net_api_set_auth(false);
    /* a fresh boot: the splash is still up */
    tb_settings_t s = nf_app.set;
    tb_app_init(&nf_app, &s, TB_WIFI_OK, &fake_now);
    net_req_t req = {.via = NET_VIA_HTTP, .method = "POST", .path = "/api/v1/status", .host = "tinybar.local", .peer_ip = 1,
                     .body = "{\"status\": \"busy\"}", .body_len = 18, .content_type_json = true};
    net_resp_t resp;
    net_api_handle(&req, &resp);
    TB_EQ_INT(resp.status, 503);
    TB_TRUE(strstr(resp.body, "\"error\":\"busy\"") != NULL);
    TB_EQ_INT(resp.retry_after_s, 1);
    free(resp.body);
}
