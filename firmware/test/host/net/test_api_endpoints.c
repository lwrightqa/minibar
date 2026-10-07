/*
 * test_api_endpoints.c: every endpoint of api.md Appendix A over HTTP, with its errors: call (5), status, message,
 * aside (8), pomodoro (9), settings (10), calendar (11), clients (12), pairing (4) and the setup network (13).
 * Owner: net builder.
 */
#include <stdlib.h>

#include "net_fixture.h"
#include "net_pair.h"
#include "tb_test.h"

static char T[NET_TOKEN_LEN + 1];   /* a full-scope token */

static void setup_paired(void)
{
    nf_setup();
    snprintf(T, sizeof T, "%s", nf_pair("remote", "full", NULL));
}

#define MAC "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"

static nf_resp_t call(const char *json)
{
    return nf_http("POST", "/api/v1/call", json, T);
}

static bool toast_has(const char *s)
{
    return strstr(nf_app.toast, s) || strstr(nf_app.pending_toast, s);
}

/* ---------- POST /api/v1/call (section 5) ---------- */

TB_TEST(call_starts_shows_and_ends)
{
    setup_paired();
    nf_resp_t r = call("{\"client\": \"" MAC "\", \"session\": \"q8Zr2Lx0\", \"seq\": 1, \"active\": true, \"app\": \"Slack\", \"call_id\": 1, \"elapsed_s\": 0}");
    TB_EQ_INT(r.status, 200);
    TB_TRUE(nf_true(r.j, "ok"));
    TB_EQ_STR(nf_str(r.j, "device_id"), "f412fa3f2a1c");
    TB_EQ_STR(nf_str(r.j, "showing"), "call");
    TB_EQ_STR(nf_str(r.j, "screen"), "on");
    TB_FALSE(nf_true(r.j, "stale"));
    TB_TRUE(nf_true(r.j, "call.active"));
    TB_EQ_STR(nf_str(r.j, "call.app"), "Slack");
    TB_TRUE(nf_null(r.j, "call.inputs"));
    TB_EQ_STR(nf_str(r.j, "call.via"), "wifi");
    TB_TRUE(nf_str(r.j, "call.since") != NULL);
    TB_FALSE(nf_true(r.j, "call.aside"));
    TB_TRUE(nf_true(r.j, "sources.calendar") == false && nf_true(r.j, "sources.mac"));
    TB_EQ_INT(nf_num(r.j, "heartbeat_s"), 30);
    TB_EQ_INT(nf_num(r.j, "timeout_s"), 90);
    TB_TRUE(nf_str(r.j, "time") != NULL);
    nf_free(&r);
    TB_TRUE(nf_app.call.active);
    TB_EQ_INT(nf_app.mac_link, TB_LINK_WIFI);
    /* a late message: ignored, stale */
    r = call("{\"client\": \"" MAC "\", \"session\": \"q8Zr2Lx0\", \"seq\": 1, \"active\": false}");
    TB_TRUE(nf_true(r.j, "stale"));
    TB_TRUE(nf_true(r.j, "call.active"));
    nf_free(&r);
    /* the call ends */
    r = call("{\"client\": \"" MAC "\", \"session\": \"q8Zr2Lx0\", \"seq\": 2, \"active\": false}");
    TB_FALSE(nf_true(r.j, "stale"));
    TB_EQ_STR(nf_str(r.j, "showing"), "own");
    TB_FALSE(nf_true(r.j, "call.active"));
    TB_TRUE(nf_null(r.j, "call.app") && nf_null(r.j, "call.via") && nf_null(r.j, "call.since"));
    nf_free(&r);
    TB_FALSE(nf_app.call.active);
    TB_TRUE(toast_has("Call ended"));
}

TB_TEST(call_set_aside_and_heartbeat)
{
    setup_paired();
    nf_resp_t r = call("{\"client\": \"" MAC "\", \"session\": \"s\", \"seq\": 41, \"active\": true, \"app\": \"Slack\", \"call_id\": 7, \"elapsed_s\": 0}");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/aside", "{\"aside\": true}", T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "showing"), "own");
    TB_TRUE(nf_true(r.j, "call.aside"));
    nf_free(&r);
    TB_TRUE(toast_has("Call set aside"));
    /* the heartbeat 30 s later: the same call stays set aside */
    nf_advance(30000);
    r = call("{\"client\": \"" MAC "\", \"session\": \"s\", \"seq\": 42, \"active\": true, \"app\": \"Slack\", \"call_id\": 7, \"elapsed_s\": 30}");
    TB_EQ_STR(nf_str(r.j, "showing"), "own");
    TB_TRUE(nf_true(r.j, "call.aside"));
    nf_free(&r);
    /* Show again */
    r = nf_http("POST", "/api/v1/aside", "{\"aside\": false}", T);
    TB_EQ_STR(nf_str(r.j, "showing"), "call");
    TB_FALSE(nf_true(r.j, "call.aside"));
    nf_free(&r);
    /* set aside again, then a new call (new call_id) takes over */
    r = nf_http("POST", "/api/v1/aside", "{\"aside\": true}", T);
    nf_free(&r);
    r = call("{\"client\": \"" MAC "\", \"session\": \"s\", \"seq\": 43, \"active\": true, \"app\": \"Zoom\", \"call_id\": 8}");
    TB_EQ_STR(nf_str(r.j, "showing"), "call");
    TB_EQ_STR(nf_str(r.j, "call.app"), "Zoom");
    nf_free(&r);
}

TB_TEST(call_with_calls_from_your_mac_off_is_still_200)
{
    setup_paired();
    nf_resp_t r = nf_http("PATCH", "/api/v1/settings", "{\"automatic\": {\"mac\": false}}", T);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    r = call("{\"client\": \"" MAC "\", \"session\": \"s\", \"seq\": 45, \"active\": true, \"call_id\": 8, \"elapsed_s\": 0}");
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "showing"), "own");
    TB_TRUE(nf_true(r.j, "call.active"));
    TB_FALSE(nf_true(r.j, "sources.mac"));
    nf_free(&r);
    /* turning the switch back on during the call shows it at once */
    r = nf_http("PATCH", "/api/v1/settings", "{\"automatic\": {\"mac\": true}}", T);
    nf_free(&r);
    r = nf_http("GET", "/api/v1/status", NULL, T);
    TB_EQ_STR(nf_str(r.j, "showing"), "call");
    nf_free(&r);
}

TB_TEST(call_lost_contact_after_90_seconds)
{
    setup_paired();
    nf_resp_t r = call("{\"client\": \"" MAC "\", \"session\": \"s\", \"seq\": 1, \"active\": true, \"app\": \"Slack\"}");
    nf_free(&r);
    nf_advance(89000);
    TB_TRUE(nf_app.call.active);
    nf_advance(1500);
    TB_FALSE(nf_app.call.active);
    TB_EQ_INT(nf_app.mac_link, TB_LINK_NONE);
    TB_TRUE(toast_has("Lost contact with your Mac"));
    r = nf_http("GET", "/api/v1/status", NULL, T);
    TB_FALSE(nf_true(r.j, "macs.0.connected"));
    TB_TRUE(nf_str(r.j, "macs.0.last_heard") != NULL);
    nf_free(&r);
}

TB_TEST(call_leaving_ends_without_lost_contact)
{
    setup_paired();
    nf_resp_t r = call("{\"client\": \"" MAC "\", \"session\": \"s\", \"seq\": 1, \"active\": true}");
    nf_free(&r);
    r = call("{\"client\": \"" MAC "\", \"session\": \"s\", \"seq\": 2, \"active\": false, \"leaving\": true}");
    TB_EQ_INT(r.status, 200);
    TB_FALSE(nf_true(r.j, "call.active"));
    nf_free(&r);
    TB_EQ_INT(nf_app.mac_link, TB_LINK_NONE);
    nf_advance(100000);
    TB_FALSE(toast_has("Lost contact"));
}

TB_TEST(call_elapsed_and_inputs_and_app_cleaning)
{
    setup_paired();
    nf_resp_t r = call("{\"client\": \"" MAC "\", \"active\": true, \"app\": \"Zoom\", \"inputs\": [\"mic\", \"camera\"], \"call_id\": 9, \"elapsed_s\": 4}");
    TB_EQ_INT(r.status, 200);
    cJSON *in = nf_get(r.j, "call.inputs");
    TB_EQ_INT(cJSON_GetArraySize(in), 2);
    TB_EQ_STR(nf_str(r.j, "call.inputs.0"), "mic");
    TB_EQ_STR(nf_str(r.j, "call.inputs.1"), "camera");
    nf_free(&r);
    TB_EQ_INT(nf_app.call.since_ms, fake_now.mono - 210 - 4000);
    /* typographic punctuation is mapped, control characters go */
    r = call("{\"client\": \"" MAC "\", \"active\": true, \"app\": \"Bob’s\\tPhone\", \"call_id\": 9}");
    TB_EQ_STR(nf_str(r.j, "call.app"), "Bob's Phone");
    nf_free(&r);
}

TB_TEST(call_validation_errors)
{
    setup_paired();
    struct { const char *body, *code, *field; } cases[] = {
        {"{\"active\": true}", "bad_request", "client"},
        {"{\"client\": 5, \"active\": true}", "bad_request", "client"},
        {"{\"client\": \"short\", \"active\": true}", "bad_value", "client"},
        {"{\"client\": \"has spaces in it\", \"active\": true}", "bad_value", "client"},
        {"{\"client\": \"" MAC "\"}", "bad_request", "active"},
        {"{\"client\": \"" MAC "\", \"active\": \"yes\"}", "bad_request", "active"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"session\": \"has-dash\"}", "bad_value", "session"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"session\": \"12345678901234567\"}", "bad_value", "session"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"seq\": 0}", "bad_value", "seq"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"seq\": 1.5}", "bad_value", "seq"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"seq\": 4294967296}", "bad_value", "seq"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"seq\": \"1\"}", "bad_request", "seq"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"app\": \"\"}", "bad_value", "app"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"app\": \"0123456789012345678901234567890123456789012345678901234567890123x\"}", "bad_value", "app"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"app\": 3}", "bad_request", "app"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"inputs\": \"mic\"}", "bad_request", "inputs"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"inputs\": [\"speaker\"]}", "bad_value", "inputs"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"call_id\": 0}", "bad_value", "call_id"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"elapsed_s\": 86401}", "bad_value", "elapsed_s"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"elapsed_s\": -1}", "bad_value", "elapsed_s"},
        {"{\"client\": \"" MAC "\", \"active\": true, \"leaving\": true}", "bad_value", "leaving"},
        {"{\"client\": \"" MAC "\", \"active\": false, \"leaving\": 1}", "bad_request", "leaving"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        nf_resp_t r = call(cases[i].body);
        TB_EQ_INT(r.status, 400);
        TB_EQ_STR(nf_err(&r), cases[i].code);
        TB_EQ_STR(nf_str(r.j, "field"), cases[i].field);
        if (strcmp(nf_err(&r) ? nf_err(&r) : "", cases[i].code)) printf("    case %zu: %s\n", i, r.r.body);
        nf_free(&r);
    }
    /* null means left out; the documented error texts */
    nf_resp_t r = call("{\"client\": \"" MAC "\", \"active\": true, \"app\": null, \"session\": null, \"seq\": null}");
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    r = call("{\"client\": \"" MAC "\"}");
    TB_EQ_STR(nf_str(r.j, "message"), "\"active\" is required and must be true or false.");
    nf_free(&r);
    r = call("{\"client\": \"" MAC "\", \"active\": true, \"leaving\": true}");
    TB_EQ_STR(nf_str(r.j, "message"), "\"leaving\" needs \"active\": false.");
    nf_free(&r);
}

TB_TEST(call_two_macs)
{
    setup_paired();
    nf_resp_t r = call("{\"client\": \"" MAC "\", \"session\": \"a\", \"seq\": 1, \"active\": true, \"app\": \"Slack\"}");
    nf_free(&r);
    /* a second, idle Mac (with its own token: one token reports for one Mac, api.md 5.1) doesn't end the first one's
     * call */
    char t2[NET_TOKEN_LEN + 1];
    snprintf(t2, sizeof t2, "%s", nf_pair("mac", "call", "22222222-0000"));
    r = nf_http("POST", "/api/v1/call", "{\"client\": \"22222222-0000\", \"session\": \"b\", \"seq\": 1, \"active\": false}", t2);
    TB_TRUE(nf_true(r.j, "call.active"));
    TB_EQ_STR(nf_str(r.j, "call.app"), "Slack");
    nf_free(&r);
    r = nf_http("GET", "/api/v1/status", NULL, T);
    TB_EQ_INT(cJSON_GetArraySize(nf_get(r.j, "macs")), 2);
    TB_EQ_STR(nf_str(r.j, "macs.0.client"), "22222222-0000");     /* most recently heard first */
    TB_FALSE(nf_true(r.j, "macs.0.active"));
    TB_TRUE(nf_true(r.j, "macs.1.active"));
    TB_EQ_STR(nf_str(r.j, "macs.1.name"), "Mac");
    nf_free(&r);
}

/* ---------- POST /api/v1/status, message, aside (section 8) ---------- */

TB_TEST(status_post_picks_your_own_status)
{
    setup_paired();
    nf_resp_t r = nf_http("POST", "/api/v1/status", "{\"status\": \"busy\"}", T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "own.status"), "busy");
    TB_EQ_STR(nf_str(r.j, "own.previous"), "busy");
    TB_TRUE(nf_str(r.j, "own.since") != NULL);
    nf_free(&r);
    TB_EQ_INT(nf_app.idx, TB_ST_BUSY);
    const char *all[] = {"available", "meeting", "pomodoro", "away", "clock"};
    for (size_t i = 0; i < 5; i++) {
        char b[64];
        snprintf(b, sizeof b, "{\"status\": \"%s\"}", all[i]);
        r = nf_http("POST", "/api/v1/status", b, T);
        TB_EQ_STR(nf_str(r.j, "own.status"), all[i]);
        nf_free(&r);
    }
    r = nf_http("POST", "/api/v1/status", "{\"status\": \"away\", \"back_at\": \"13:30\", \"note\": \"Grabbing lunch\"}", T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "away.back_at"), "13:30");
    TB_EQ_STR(nf_str(r.j, "away.note"), "Grabbing lunch");
    nf_free(&r);
}

TB_TEST(status_post_errors)
{
    setup_paired();
    struct { const char *body; int st; const char *code, *field; } cases[] = {
        {"{}", 400, "bad_request", "status"},
        {"{\"status\": 3}", 400, "bad_request", "status"},
        {"{\"status\": \"on_a_call\"}", 400, "bad_value", "status"},
        {"{\"status\": \"busy\", \"back_at\": \"13:30\"}", 400, "bad_value", "back_at"},
        {"{\"status\": \"busy\", \"note\": \"Lunch\"}", 400, "bad_value", "note"},
        {"{\"status\": \"away\", \"back_at\": \"1:30\"}", 400, "bad_value", "back_at"},
        {"{\"status\": \"away\", \"back_at\": \"24:00\"}", 400, "bad_value", "back_at"},
        {"{\"status\": \"away\", \"back_at\": 1330}", 400, "bad_request", "back_at"},
        {"{\"status\": \"away\", \"note\": \"\"}", 400, "bad_value", "note"},
        {"{\"status\": \"away\", \"note\": \"01234567890123456789012345678901234567890\"}", 400, "bad_value", "note"},
        {"{\"status\": \"busy\", \"set_aside\": \"no\"}", 400, "bad_request", "set_aside"},
        {"{\"status\": \"message\"}", 409, "no_message", "status"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        nf_resp_t r = nf_http("POST", "/api/v1/status", cases[i].body, T);
        TB_EQ_INT(r.status, cases[i].st);
        TB_EQ_STR(nf_err(&r), cases[i].code);
        TB_EQ_STR(nf_str(r.j, "field"), cases[i].field);
        nf_free(&r);
    }
    nf_resp_t r = nf_http("POST", "/api/v1/status", "{\"status\": \"on_a_call\"}", T);
    TB_EQ_STR(nf_str(r.j, "message"), "Unknown status \"on_a_call\". On a call is set by the Mac app only.");
    nf_free(&r);
}

TB_TEST(message_is_shown_and_cleaned)
{
    setup_paired();
    nf_resp_t r = nf_http("POST", "/api/v1/message", "{\"text\": \"  Don’t interrupt – deadline…  \"}", T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "own.status"), "message");
    TB_EQ_STR(nf_str(r.j, "message.text"), "Don't interrupt - deadline...");
    TB_TRUE(nf_str(r.j, "message.set_at") != NULL);
    nf_free(&r);
    /* the last message is kept when another status is picked; "message" shows it again */
    r = nf_http("POST", "/api/v1/status", "{\"status\": \"busy\"}", T);
    TB_EQ_STR(nf_str(r.j, "message.text"), "Don't interrupt - deadline...");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/status", "{\"status\": \"message\"}", T);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    /* Latin-1 is fine */
    r = nf_http("POST", "/api/v1/message", "{\"text\": \"Café at 3 · back soon\"}", T);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
}

TB_TEST(message_errors)
{
    setup_paired();
    nf_resp_t r = nf_http("POST", "/api/v1/message", "{\"text\": \"Pizza \U0001F355 and 中 \U0001F355\"}", T);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_err(&r), "unsupported_chars");
    TB_EQ_STR(nf_str(r.j, "field"), "text");
    TB_EQ_INT(cJSON_GetArraySize(nf_get(r.j, "chars")), 2);
    TB_EQ_STR(nf_str(r.j, "chars.0"), "\U0001F355");
    TB_EQ_STR(nf_str(r.j, "chars.1"), "中");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/message", "{\"text\": \"   \"}", T);
    TB_EQ_STR(nf_err(&r), "bad_value");
    nf_free(&r);
    char big[200];
    snprintf(big, sizeof big, "{\"text\": \"%081d\"}", 0);
    r = nf_http("POST", "/api/v1/message", big, T);
    TB_EQ_STR(nf_err(&r), "bad_value");
    nf_free(&r);
    snprintf(big, sizeof big, "{\"text\": \"%080d\"}", 0);
    r = nf_http("POST", "/api/v1/message", big, T);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    r = nf_http("POST", "/api/v1/message", "{}", T);
    TB_EQ_STR(nf_err(&r), "bad_request");
    nf_free(&r);
}

TB_TEST(aside_errors)
{
    setup_paired();
    nf_resp_t r = nf_http("POST", "/api/v1/aside", "{\"aside\": true}", T);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "nothing_to_set_aside");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/aside", "{\"aside\": false}", T);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "nothing_set_aside");
    TB_EQ_STR(nf_str(r.j, "message"), "Nothing is set aside.");
    TB_EQ_STR(nf_str(r.j, "field"), "aside");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/aside", "{}", T);
    TB_EQ_STR(nf_err(&r), "bad_request");
    nf_free(&r);
}

TB_TEST(in_setup_refuses_status_message_aside_pomodoro_and_pairing)
{
    setup_paired();
    nf_app.wifi_mode = TB_WIFI_SETUP;   /* the bar is on its QR screen */
    const struct { const char *p, *b; } eps[] = {
        {"/api/v1/status", "{\"status\": \"busy\"}"}, {"/api/v1/message", "{\"text\": \"Hi\"}"},
        {"/api/v1/aside", "{\"aside\": true}"}, {"/api/v1/pomodoro", "{\"action\": \"start\"}"},
    };
    for (size_t i = 0; i < 4; i++) {
        nf_resp_t r = nf_http("POST", eps[i].p, eps[i].b, T);
        TB_EQ_INT(r.status, 409);
        TB_EQ_STR(nf_err(&r), "in_setup");
        nf_free(&r);
    }
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"mac\", \"scope\": \"call\"}", NULL);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "in_setup");
    nf_free(&r);
    /* pair isn't refused for setup (api.md 4.7): setup ended any code, so there's no pairing for this id */
    r = nf_http("POST", "/api/v1/pair", "{\"pairing_id\": \"x\", \"code\": \"1\"}", NULL);
    TB_EQ_STR(nf_err(&r), "not_pairing");
    nf_free(&r);
    /* settings and reading still work */
    r = nf_http("GET", "/api/v1/status", NULL, T);
    TB_EQ_STR(nf_str(r.j, "showing"), "setup");
    nf_free(&r);
}

/* ---------- POST /api/v1/pomodoro (section 9) ---------- */

TB_TEST(pomodoro_actions)
{
    setup_paired();
    nf_resp_t r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"pause\"}", T);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "not_running");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"extend\"}", T);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "nothing_to_extend");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"toggle\"}", T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "own.status"), "pomodoro");
    TB_EQ_STR(nf_str(r.j, "pomodoro.state"), "running");
    TB_TRUE(nf_str(r.j, "pomodoro.ends_at") != NULL);
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"start\"}", T);     /* already running: fine */
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "pomodoro.state"), "running");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"extend\", \"minutes\": 5}", T);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(nf_num(r.j, "pomodoro.remaining_s") > 1500);
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"pause\"}", T);
    TB_EQ_STR(nf_str(r.j, "pomodoro.state"), "paused");
    TB_EQ_STR(nf_str(r.j, "pomodoro.paused_by"), "you");
    TB_TRUE(nf_null(r.j, "pomodoro.ends_at"));
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"skip\"}", T);
    TB_EQ_STR(nf_str(r.j, "pomodoro.phase"), "short");
    TB_EQ_STR(nf_str(r.j, "pomodoro.state"), "running");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"stop\"}", T);
    TB_EQ_STR(nf_str(r.j, "pomodoro.state"), "ready");
    TB_EQ_STR(nf_str(r.j, "own.status"), "available");     /* back to own.previous */
    nf_free(&r);
    const char *bad[] = {"{\"action\": \"snooze\"}", "{\"action\": \"extend\", \"minutes\": 0}",
                         "{\"action\": \"extend\", \"minutes\": 61}"};
    for (int i = 0; i < 3; i++) {
        r = nf_http("POST", "/api/v1/pomodoro", bad[i], T);
        TB_EQ_INT(r.status, 400);
        TB_EQ_STR(nf_err(&r), "bad_value");
        nf_free(&r);
    }
    r = nf_http("POST", "/api/v1/pomodoro", "{}", T);
    TB_EQ_STR(nf_err(&r), "bad_request");
    nf_free(&r);
}

TB_TEST(pomodoro_paused_by_a_call)
{
    setup_paired();
    nf_resp_t r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"start\"}", T);
    nf_free(&r);
    r = call("{\"client\": \"" MAC "\", \"active\": true}");
    nf_free(&r);
    r = nf_http("GET", "/api/v1/status", NULL, T);
    TB_EQ_STR(nf_str(r.j, "pomodoro.state"), "paused");
    TB_EQ_STR(nf_str(r.j, "pomodoro.paused_by"), "call");
    nf_free(&r);
}

/* ---------- settings (section 10) ---------- */

TB_TEST(settings_get_and_patch)
{
    setup_paired();
    nf_resp_t r = nf_http("GET", "/api/v1/settings", NULL, T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_INT(nf_num(r.j, "settings.pomodoro.focus_min"), 25);
    TB_EQ_INT(nf_num(r.j, "settings.pomodoro.short_min"), 5);
    TB_EQ_INT(nf_num(r.j, "settings.pomodoro.long_min"), 15);
    TB_EQ_INT(nf_num(r.j, "settings.pomodoro.long_every"), 4);
    TB_FALSE(nf_true(r.j, "settings.pomodoro.auto_start"));
    TB_TRUE(nf_true(r.j, "settings.pomodoro.chime"));
    TB_FALSE(nf_true(r.j, "settings.pomodoro.ticking"));
    TB_EQ_STR(nf_str(r.j, "settings.pomodoro.tick_volume"), "soft");
    TB_EQ_INT(nf_num(r.j, "settings.display.brightness"), 70);
    TB_FALSE(nf_true(r.j, "settings.automatic.calendar"));
    TB_TRUE(nf_true(r.j, "settings.automatic.mac"));
    TB_FALSE(nf_true(r.j, "settings.automatic.meeting_titles"));
    TB_EQ_STR(nf_str(r.j, "settings.device.name"), "MiniBar 2A1C");
    TB_TRUE(nf_null(r.j, "settings.device.time_zone"));
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/settings", "{\"pomodoro\": {\"ticking\": true, \"tick_volume\": \"medium\"}}", T);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(nf_true(r.j, "settings.pomodoro.ticking"));
    TB_EQ_STR(nf_str(r.j, "settings.pomodoro.tick_volume"), "medium");
    nf_free(&r);
    TB_TRUE(toast_has("Ticking on"));
    r = nf_http("PATCH", "/api/v1/settings", "{\"pomodoro\": {\"focus_min\": 50}, \"display\": {\"brightness\": 100}, "
                "\"device\": {\"name\": \"Alex’s bar\", \"time_zone\": \"Europe/Berlin\"}}", T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_INT(nf_num(r.j, "settings.pomodoro.focus_min"), 50);
    TB_EQ_INT(nf_num(r.j, "settings.display.brightness"), 100);
    TB_EQ_STR(nf_str(r.j, "settings.device.name"), "Alex's bar");
    TB_EQ_STR(nf_str(r.j, "settings.device.time_zone"), "Europe/Berlin");
    nf_free(&r);
    TB_EQ_STR(nf_app.set.device.name, "Alex's bar");
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "name"), "Alex's bar");
    nf_free(&r);
}

/* Time format (decisions.md "Time format (2026-10-07)"): display.time_format "12h" (default) or "24h". */
TB_TEST(settings_time_format)
{
    setup_paired();
    nf_resp_t r = nf_http("GET", "/api/v1/settings", NULL, T);
    TB_EQ_STR(nf_str(r.j, "settings.display.time_format"), "12h");         /* default: today's behavior */
    nf_free(&r);
    r = nf_http("GET", "/api/v1/info", NULL, NULL);                        /* in info too, with no token: it isn't secret */
    TB_EQ_STR(nf_str(r.j, "time_format"), "12h");
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/settings", "{\"display\": {\"time_format\": \"24h\"}}", T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "settings.display.time_format"), "24h");
    nf_free(&r);
    TB_TRUE(nf_app.set.more.time_24h);
    TB_TRUE(toast_has("Time format \xC2\xB7 24-hour"));
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "time_format"), "24h");
    nf_free(&r);
    /* the same value again: accepted, nothing changes */
    r = nf_http("PATCH", "/api/v1/settings", "{\"display\": {\"time_format\": \"24h\"}}", T);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    /* it applies with other fields and only to its own */
    r = nf_http("PATCH", "/api/v1/settings", "{\"display\": {\"time_format\": \"12h\", \"brightness\": 40}}", T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "settings.display.time_format"), "12h");
    TB_EQ_INT(nf_num(r.j, "settings.display.brightness"), 40);
    nf_free(&r);
    TB_FALSE(nf_app.set.more.time_24h);
    /* anything else is bad_value on that field, and nothing else in the request applies */
    const char *bad[] = {"\"24H\"", "\"25h\"", "\"\"", "\"12\"", "24", "true", "null", "[\"24h\"]", "{}"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char body[128];
        snprintf(body, sizeof body, "{\"display\": {\"brightness\": 100, \"time_format\": %s}}", bad[i]);
        r = nf_http("PATCH", "/api/v1/settings", body, T);
        TB_EQ_INT(r.status, 400);
        TB_EQ_STR(nf_err(&r), "bad_value");
        TB_EQ_STR(nf_str(r.j, "field"), "display.time_format");
        nf_free(&r);
    }
    TB_EQ_INT(nf_app.set.display.brightness, 40);
    TB_FALSE(nf_app.set.more.time_24h);
}

/* Meeting chime (decisions.md "Meeting-start sound (2026-10-07)"): sound.meeting_chime, on by default. */
TB_TEST(settings_meeting_chime)
{
    setup_paired();
    nf_resp_t r = nf_http("GET", "/api/v1/settings", NULL, T);
    TB_TRUE(nf_true(r.j, "settings.sound.meeting_chime"));                  /* default on */
    nf_free(&r);
    r = nf_http("PATCH", "/api/v1/settings", "{\"sound\": {\"meeting_chime\": false}}", T);
    TB_EQ_INT(r.status, 200);
    TB_FALSE(nf_true(r.j, "settings.sound.meeting_chime"));
    nf_free(&r);
    TB_FALSE(nf_app.set.more.meeting_chime);
    TB_TRUE(toast_has("Meeting chime off"));
    r = nf_http("PATCH", "/api/v1/settings", "{\"sound\": {\"meeting_chime\": true}, \"display\": {\"time_format\": \"24h\"}}", T);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(nf_true(r.j, "settings.sound.meeting_chime"));
    TB_EQ_STR(nf_str(r.j, "settings.display.time_format"), "24h");
    nf_free(&r);
    TB_TRUE(nf_app.set.more.meeting_chime);
    TB_TRUE(toast_has("Meeting chime on"));
    /* a change through the API plays nothing (only the tile's sample does) */
    tb_effect_t fx[TB_EFFECTS_MAX];
    int n = tb_app_take_effects(&nf_app, fx, TB_EFFECTS_MAX);
    for (int i = 0; i < n; i++) TB_TRUE(fx[i].kind != TB_FX_MEETING_CHIME);
    /* anything but true or false is bad_value on that field, and nothing else in the request applies */
    const char *bad[] = {"\"off\"", "\"true\"", "0", "1", "null", "[]", "{}"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char body[160];
        snprintf(body, sizeof body, "{\"display\": {\"brightness\": 100}, \"sound\": {\"meeting_chime\": %s}}", bad[i]);
        r = nf_http("PATCH", "/api/v1/settings", body, T);
        TB_EQ_INT(r.status, 400);
        TB_EQ_STR(nf_err(&r), "bad_value");
        TB_EQ_STR(nf_str(r.j, "field"), "sound.meeting_chime");
        nf_free(&r);
    }
    TB_EQ_INT(nf_app.set.display.brightness, 70);
    TB_TRUE(nf_app.set.more.meeting_chime);
    r = nf_http("PATCH", "/api/v1/settings", "{\"sound\": 5}", T);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_str(r.j, "field"), "sound");
    nf_free(&r);
}

TB_TEST(settings_patch_is_all_or_nothing)
{
    setup_paired();
    struct { const char *body; int st; const char *code, *field; } cases[] = {
        {"{\"pomodoro\": {\"focus_min\": 0}}", 400, "bad_value", "pomodoro.focus_min"},
        {"{\"pomodoro\": {\"focus_min\": 121}}", 400, "bad_value", "pomodoro.focus_min"},
        {"{\"pomodoro\": {\"focus_min\": 25.5}}", 400, "bad_value", "pomodoro.focus_min"},
        {"{\"pomodoro\": {\"focus_min\": \"25\"}}", 400, "bad_value", "pomodoro.focus_min"},
        {"{\"pomodoro\": {\"focus_min\": null}}", 400, "bad_value", "pomodoro.focus_min"},
        {"{\"pomodoro\": {\"short_min\": 61}}", 400, "bad_value", "pomodoro.short_min"},
        {"{\"pomodoro\": {\"long_min\": 0}}", 400, "bad_value", "pomodoro.long_min"},
        {"{\"pomodoro\": {\"long_every\": 1}}", 400, "bad_value", "pomodoro.long_every"},
        {"{\"pomodoro\": {\"long_every\": 9}}", 400, "bad_value", "pomodoro.long_every"},
        {"{\"pomodoro\": {\"chime\": 1}}", 400, "bad_value", "pomodoro.chime"},
        {"{\"pomodoro\": {\"tick_volume\": \"loud\"}}", 400, "bad_value", "pomodoro.tick_volume"},
        {"{\"display\": {\"brightness\": 9}}", 400, "bad_value", "display.brightness"},
        {"{\"display\": {\"brightness\": 101}}", 400, "bad_value", "display.brightness"},
        {"{\"automatic\": {\"mac\": null}}", 400, "bad_value", "automatic.mac"},
        {"{\"automatic\": {\"calendar\": true}}", 409, "no_calendar", "automatic.calendar"},
        {"{\"automatic\": {\"meeting_titles\": true}}", 409, "no_calendar", "automatic.meeting_titles"},
        {"{\"device\": {\"name\": \"\"}}", 400, "bad_value", "device.name"},
        {"{\"device\": {\"name\": \"0123456789012345678901234\"}}", 400, "bad_value", "device.name"},
        {"{\"device\": {\"name\": \"Bar \U0001F355\"}}", 400, "unsupported_chars", "device.name"},
        {"{\"device\": {\"time_zone\": \"Mars/Olympus\"}}", 400, "bad_value", "device.time_zone"},
        {"{\"pomodoro\": 5}", 400, "bad_value", "pomodoro"},
        /* the good part isn't applied when another part is bad */
        {"{\"display\": {\"brightness\": 40}, \"pomodoro\": {\"focus_min\": 500}}", 400, "bad_value", "pomodoro.focus_min"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        nf_resp_t r = nf_http("PATCH", "/api/v1/settings", cases[i].body, T);
        TB_EQ_INT(r.status, cases[i].st);
        TB_EQ_STR(nf_err(&r), cases[i].code);
        TB_EQ_STR(nf_str(r.j, "field"), cases[i].field);
        nf_free(&r);
    }
    TB_EQ_INT(nf_app.set.display.brightness, 70);
    TB_EQ_INT(nf_app.set.pomodoro.focus_min, 25);
    /* an empty patch and unknown fields change nothing and succeed */
    nf_resp_t r = nf_http("PATCH", "/api/v1/settings", "{\"later\": {\"x\": 1}}", T);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
}

/* ---------- calendar (section 11) ---------- */

static void saved_calendar(void)
{
    fake_cal.saved = true;
    fake_cal.last_sync = fake_now.wall - 240;
    nf_app.cal_saved = true;
    nf_app.set.automatic.calendar = true;
}

TB_TEST(calendar_get_without_and_with_an_address)
{
    setup_paired();
    nf_resp_t r = nf_http("GET", "/api/v1/calendar", NULL, T);
    TB_EQ_INT(r.status, 200);
    TB_FALSE(nf_true(r.j, "calendar.saved"));
    TB_TRUE(nf_null(r.j, "calendar.address") && nf_null(r.j, "calendar.last_sync") && nf_null(r.j, "calendar.today") &&
            nf_null(r.j, "calendar.left_today") && nf_null(r.j, "calendar.error") && nf_null(r.j, "calendar.check"));
    TB_FALSE(nf_true(r.j, "calendar.syncing"));
    nf_free(&r);

    saved_calendar();
    /* three meetings later today, one already over, one tomorrow; one private with a title */
    tb_meeting_t m[5];
    memset(m, 0, sizeof m);
    tb_epoch_t now = fake_now.wall;
    m[0] = (tb_meeting_t){.id = 1, .start = now - 3600, .end = now - 1800};
    m[1] = (tb_meeting_t){.id = 2, .start = now + 2880, .end = now + 2880 + 2700};
    snprintf(m[1].title, sizeof m[1].title, "Design review");
    snprintf(m[1].location, sizeof m[1].location, "Room 4");
    m[2] = (tb_meeting_t){.id = 3, .start = now + 6480, .end = now + 6480 + 1800, .priv = true};
    snprintf(m[2].title, sizeof m[2].title, "Doctor");
    m[3] = (tb_meeting_t){.id = 4, .start = now + 11880, .end = now + 11880 + 3600};
    snprintf(m[3].location, sizeof m[3].location, "https://meet.google.com/abc");
    m[4] = (tb_meeting_t){.id = 5, .start = now + 86400, .end = now + 86400 + 1800};
    tb_app_set_meetings(&nf_app, m, 5, &fake_now);
    fake_cal.error = "calendar_unreachable";
    snprintf(fake_cal.error_message, sizeof fake_cal.error_message, "No answer.");
    fake_cal.error_at = now - 60;
    r = nf_http("GET", "/api/v1/calendar", NULL, T);
    TB_TRUE(nf_true(r.j, "calendar.saved"));
    TB_TRUE(nf_null(r.j, "calendar.address"));            /* since 1.0.8 not even the masked form */
    TB_TRUE(nf_str(r.j, "calendar.last_sync") != NULL);
    TB_EQ_STR(nf_str(r.j, "calendar.error.error"), "calendar_unreachable");
    TB_TRUE(nf_str(r.j, "calendar.error.at") != NULL);
    TB_EQ_INT(nf_num(r.j, "calendar.left_today"), 3);
    TB_EQ_INT(cJSON_GetArraySize(nf_get(r.j, "calendar.today")), 3);
    TB_TRUE(nf_null(r.j, "calendar.today.0.title"));       /* titles off */
    TB_TRUE(strstr(r.r.body, "Design review") == NULL);
    TB_TRUE(strstr(r.r.body, "https://") == NULL);           /* never the address */
    nf_free(&r);
    /* titles on: the title and location show, except the private event's and a web address */
    nf_app.set.automatic.meeting_titles = true;
    r = nf_http("GET", "/api/v1/calendar", NULL, T);
    TB_EQ_STR(nf_str(r.j, "calendar.today.0.title"), "Design review");
    TB_EQ_STR(nf_str(r.j, "calendar.today.0.location"), "Room 4");
    TB_TRUE(nf_null(r.j, "calendar.today.1.title"));
    TB_TRUE(nf_null(r.j, "calendar.today.2.location"));
    nf_free(&r);
    r = nf_http("GET", "/api/v1/status", NULL, T);
    TB_EQ_INT(nf_num(r.j, "meeting.left_today"), 3);
    TB_EQ_STR(nf_str(r.j, "meeting.next.title"), "Design review");
    TB_FALSE(nf_true(r.j, "meeting.active"));
    TB_EQ_STR(nf_str(r.j, "calendar.error"), "calendar_unreachable");
    nf_free(&r);
}

TB_TEST(calendar_put_checks_the_format_then_answers_202)
{
    setup_paired();
    struct { const char *body, *code; int st; } cases[] = {
        {"{}", "bad_request", 400},
        {"{\"url\": \"\"}", "bad_request", 400},
        {"{\"url\": \"calendar please\"}", "not_a_url", 400},
        {"{\"url\": \"http://calendar.google.com/calendar/ical/x/private-1/basic.ics\"}", "http_not_allowed", 400},
        {"{\"url\": \"https://calendar.google.com/calendar/ical/x/public/basic.ics\"}", "public_address", 400},
        {"{\"url\": \"https://calendar.google.com/calendar/ical/x/private-1/basic.html\"}", "not_ics", 400},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        nf_resp_t r = nf_http("PUT", "/api/v1/calendar", cases[i].body, T);
        TB_EQ_INT(r.status, cases[i].st);
        TB_EQ_STR(nf_err(&r), cases[i].code);
        TB_EQ_STR(nf_str(r.j, "field"), "url");
        nf_free(&r);
    }
    nf_resp_t r = nf_http("PUT", "/api/v1/calendar", "{\"url\": \"https://calendar.google.com/calendar/ical/you%40example.com/private-8c1d5e2a9b7f40c3a6e1d2b3c4f53f2a/basic.ics\"}", T);
    TB_EQ_INT(r.status, 202);
    TB_EQ_STR(nf_str(r.j, "calendar.check.state"), "checking");
    TB_TRUE(nf_true(r.j, "calendar.syncing"));
    TB_TRUE(strstr(r.r.body, "8c1d5e2a") == NULL);
    nf_free(&r);
    TB_EQ_INT(fake_cal_put_calls, 1);
    TB_FALSE(fake_cal_put_from_setup);
}

TB_TEST(calendar_delete_and_sync)
{
    setup_paired();
    nf_resp_t r = nf_http("DELETE", "/api/v1/calendar", NULL, T);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "no_calendar");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/calendar/sync", NULL, T);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "no_calendar");
    nf_free(&r);
    saved_calendar();
    r = nf_http("POST", "/api/v1/calendar/sync", "{}", T);
    TB_EQ_INT(r.status, 202);
    TB_TRUE(nf_true(r.j, "calendar.syncing"));
    nf_free(&r);
    fake_cal_sync_result = -2;
    r = nf_http("POST", "/api/v1/calendar/sync", NULL, T);
    TB_EQ_INT(r.status, 503);
    TB_EQ_STR(nf_err(&r), "offline");
    nf_free(&r);
    r = nf_http("DELETE", "/api/v1/calendar", NULL, T);
    TB_EQ_INT(r.status, 200);
    TB_FALSE(nf_true(r.j, "calendar.saved"));
    TB_TRUE(nf_null(r.j, "calendar.address"));
    nf_free(&r);
}

/* ---------- pairing (section 4) ---------- */

TB_TEST(pairing_start_and_finish)
{
    nf_setup();
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"mac\", \"scope\": \"call\", \"client\": \"" MAC "\"}", NULL);
    TB_EQ_INT(r.status, 202);
    TB_EQ_INT(strlen(nf_str(r.j, "pairing_id")), 16);
    TB_EQ_INT(nf_num(r.j, "expires_in_s"), 120);
    TB_EQ_INT(nf_num(r.j, "code_length"), 6);
    TB_EQ_INT(nf_num(r.j, "attempts"), 3);
    char pid[17];
    snprintf(pid, sizeof pid, "%s", nf_str(r.j, "pairing_id"));
    nf_free(&r);
    /* the bar shows the code with the client's label */
    TB_TRUE(nf_app.pairing.active);
    TB_EQ_INT(strlen(nf_app.pairing.code), 6);
    TB_EQ_STR(nf_app.pairing.who, "Mac");
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "showing");
    nf_free(&r);
    /* another device can't start meanwhile */
    nf_advance(46000);
    r = nf_http("POST", "/api/v1/pair/start", "{\"name\": \"iPhone\", \"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "pairing_busy");
    TB_TRUE(nf_num(r.j, "retry_after_s") > 70 && nf_num(r.j, "retry_after_s") <= 74);
    TB_EQ_INT(r.r.retry_after_s, (int)nf_num(r.j, "retry_after_s"));
    nf_free(&r);
    /* a wrong code */
    char body[128], bad[8];
    memcpy(bad, nf_app.pairing.code, sizeof bad);
    bad[0] = bad[0] == '9' ? '1' : (char)(bad[0] + 1);
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"%s\"}", pid, bad);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 403);
    TB_EQ_STR(nf_err(&r), "wrong_code");
    TB_EQ_INT(nf_num(r.j, "attempts_left"), 2);
    TB_EQ_STR(nf_str(r.j, "message"), "That code doesn't match. 2 tries left.");
    TB_EQ_STR(nf_str(r.j, "field"), "code");
    nf_free(&r);
    /* more than one a second */
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 429);
    nf_free(&r);
    nf_advance(1000);
    /* the right code, with a space */
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"%.3s %s\"}", pid, nf_app.pairing.code, nf_app.pairing.code + 3);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 200);
    TB_EQ_INT(strlen(nf_str(r.j, "token")), 47);
    TB_EQ_INT(strlen(nf_str(r.j, "token_id")), 8);
    TB_EQ_STR(nf_str(r.j, "scope"), "call");
    TB_EQ_STR(nf_str(r.j, "device_id"), "f412fa3f2a1c");
    TB_EQ_STR(nf_str(r.j, "name"), "MiniBar 2A1C");
    TB_EQ_STR(nf_str(r.j, "host"), "minibar.local");
    nf_free(&r);
    TB_FALSE(nf_app.pairing.active);
    TB_TRUE(toast_has("Paired \xC2\xB7 Mac"));
    TB_EQ_INT(nf_app.paired_count, 1);
    /* it's used once */
    nf_advance(1000);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "not_pairing");
    nf_free(&r);
}

TB_TEST(pairing_ends_after_three_wrong_codes_and_times_out)
{
    nf_setup();
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"name\": \"iPhone\", \"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    char body[128];
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"000000\"}", nf_str(r.j, "pairing_id"));
    nf_free(&r);
    TB_EQ_STR(nf_app.pairing.who, "iPhone");
    if (!strcmp(nf_app.pairing.code, "000000")) memcpy(body + strlen(body) - 8, "111111", 6);
    for (int i = 2; i >= 0; i--) {
        nf_advance(1000);
        r = nf_http("POST", "/api/v1/pair", body, NULL);
        TB_EQ_INT(nf_num(r.j, "attempts_left"), i);
        nf_free(&r);
    }
    TB_FALSE(nf_app.pairing.active);
    TB_TRUE(toast_has("Pairing canceled \xC2\xB7 wrong code"));
    /* a new code, left to time out */
    nf_advance(2000);
    r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"automation\", \"scope\": \"full\", \"name\": \"\U0001F916 bot\"}", NULL);
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_STR(nf_app.pairing.who, "Script");     /* a name the bar can't draw: the kind's word */
    nf_advance(121000);
    TB_FALSE(nf_app.pairing.active);
    TB_TRUE(toast_has("Pairing timed out"));
    /* two failures in a row: locked for 30 s */
    r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"mac\", \"scope\": \"call\"}", NULL);
    TB_EQ_INT(r.status, 429);
    TB_EQ_STR(nf_err(&r), "rate_limited");
    TB_TRUE(nf_num(r.j, "retry_after_s") >= 1);
    nf_free(&r);
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "locked");
    nf_free(&r);
}

TB_TEST(pairing_canceled_on_the_bar)
{
    nf_setup();
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"mac\", \"scope\": \"call\"}", NULL);
    char body[128];
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"%s\"}", nf_str(r.j, "pairing_id"), nf_app.pairing.code);
    nf_free(&r);
    /* a tap on the pairing screen: core cancels and tells net */
    net_api_pairing_canceled(&fake_now);
    nf_advance(1000);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "not_pairing");
    nf_free(&r);
}

TB_TEST(pairing_validation)
{
    nf_setup();
    struct { const char *body, *code, *field; } cases[] = {
        {"{\"scope\": \"call\"}", "bad_request", "kind"},
        {"{\"kind\": \"mac\"}", "bad_request", "scope"},
        {"{\"kind\": \"mac\", \"scope\": \"admin\"}", "bad_value", "scope"},
        {"{\"kind\": \"mac\", \"scope\": \"call\", \"client\": \"tiny\"}", "bad_value", "client"},
        {"{\"kind\": \"mac\", \"scope\": \"call\", \"name\": \"012345678901234567890123456789012\"}", "bad_value", "name"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        nf_resp_t r = nf_http("POST", "/api/v1/pair/start", cases[i].body, NULL);
        TB_EQ_INT(r.status, 400);
        TB_EQ_STR(nf_err(&r), cases[i].code);
        TB_EQ_STR(nf_str(r.j, "field"), cases[i].field);
        nf_free(&r);
        nf_advance(300);
    }
    /* an unknown kind is "Device" */
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"watch\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_STR(nf_app.pairing.who, "Device");
    r = nf_http("POST", "/api/v1/pair", "{\"code\": \"123456\"}", NULL);
    TB_EQ_STR(nf_err(&r), "bad_request");
    TB_EQ_STR(nf_str(r.j, "field"), "pairing_id");
    nf_free(&r);
}

TB_TEST(pairing_token_limit)
{
    nf_setup();
    for (int i = 0; i < 10; i++) {
        char client[24];
        snprintf(client, sizeof client, "client-%04d", i);
        TB_TRUE(nf_pair("remote", "full", client) != NULL);
    }
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"mac\", \"scope\": \"call\"}", NULL);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "token_limit");
    nf_free(&r);
    TB_FALSE(nf_app.pairing.active);    /* nobody is shown a code that can't work */
}

/* ---------- paired devices (section 12) ---------- */

TB_TEST(clients_list_and_revoke)
{
    nf_setup();
    char mac_tok[NET_TOKEN_LEN + 1];
    snprintf(mac_tok, sizeof mac_tok, "%s", nf_pair("mac", "call", MAC));
    snprintf(T, sizeof T, "%s", nf_pair("remote", "full", "phone-0001"));
    nf_resp_t r = nf_http("GET", "/api/v1/clients", NULL, T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_INT(nf_num(r.j, "max"), 10);
    TB_EQ_INT(cJSON_GetArraySize(nf_get(r.j, "clients")), 2);
    TB_EQ_STR(nf_str(r.j, "clients.0.name"), "Mac");
    TB_EQ_STR(nf_str(r.j, "clients.0.kind"), "mac");
    TB_EQ_STR(nf_str(r.j, "clients.0.scope"), "call");
    TB_EQ_STR(nf_str(r.j, "clients.0.paired_via"), "wifi");
    TB_EQ_STR(nf_str(r.j, "clients.0.last_ip"), "10.0.4.17");
    TB_FALSE(nf_true(r.j, "clients.0.self"));
    TB_TRUE(nf_true(r.j, "clients.1.self"));
    TB_EQ_STR(nf_str(r.j, "clients.1.kind"), "remote");
    TB_TRUE(nf_str(r.j, "clients.1.paired_at") != NULL);
    char mac_id[9];
    snprintf(mac_id, sizeof mac_id, "%s", nf_str(r.j, "clients.0.token_id"));
    nf_free(&r);
    /* the Mac is on a call over Wi-Fi */
    r = nf_http("POST", "/api/v1/call", "{\"client\": \"" MAC "\", \"active\": true}", mac_tok);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    TB_TRUE(nf_app.call.active);
    /* revoking its token ends the call, and its next request gets 401 */
    char path[64];
    snprintf(path, sizeof path, "/api/v1/clients/%s", mac_id);
    r = nf_http("DELETE", path, NULL, T);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "revoked"), mac_id);
    nf_free(&r);
    TB_FALSE(nf_app.call.active);
    TB_TRUE(toast_has("Removed Mac"));
    TB_EQ_INT(nf_app.paired_count, 1);
    r = nf_http("POST", "/api/v1/call", "{\"client\": \"" MAC "\", \"active\": true}", mac_tok);
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
    r = nf_http("DELETE", path, NULL, T);
    TB_EQ_INT(r.status, 404);
    TB_EQ_STR(nf_err(&r), "not_found");
    nf_free(&r);
}

TB_TEST(forget_all_devices_on_the_bar)
{
    nf_setup();
    snprintf(T, sizeof T, "%s", nf_pair("remote", "full", NULL));
    net_api_forget_devices(&fake_now);
    TB_EQ_INT(nf_app.paired_count, 0);
    nf_resp_t r = nf_http("GET", "/api/v1/status", NULL, T);
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
}

/* ---------- the setup network (section 13) ---------- */

static nf_resp_t setup_req(const char *method, const char *path, const char *body)
{
    net_req_t req = {.via = NET_VIA_SETUP, .method = method, .path = path, .body = body, .body_len = body ? strlen(body) : 0,
                     .content_type_json = true, .host = NET_SETUP_IP, .peer_ip = 0x02020304};
    return nf_req(&req);
}

static void in_setup_mode(void)
{
    nf_setup();
    fake_wifi = (net_wifi_info_t){.state = NET_WIFI_SETUP, .host = "minibar.local"};
    nf_app.wifi_mode = TB_WIFI_SETUP;
}

TB_TEST(setup_networks_and_state)
{
    in_setup_mode();
    nf_resp_t r = setup_req("GET", "/api/v1/setup/networks", NULL);
    TB_EQ_INT(r.status, 200);
    TB_EQ_INT(cJSON_GetArraySize(nf_get(r.j, "networks")), 4);
    TB_EQ_STR(nf_str(r.j, "networks.0.ssid"), "Office-WiFi");
    TB_EQ_STR(nf_str(r.j, "networks.0.security"), "password");
    TB_EQ_INT(nf_num(r.j, "networks.0.rssi"), -52);
    TB_EQ_STR(nf_str(r.j, "networks.0.signal"), "good");
    TB_EQ_STR(nf_str(r.j, "networks.1.security"), "work_login");
    TB_EQ_STR(nf_str(r.j, "networks.2.security"), "open");
    TB_EQ_STR(nf_str(r.j, "networks.3.signal"), "weak");
    nf_free(&r);
    r = setup_req("GET", "/api/v1/setup/state", NULL);
    TB_EQ_STR(nf_str(r.j, "state"), "idle");
    TB_TRUE(nf_null(r.j, "error") && nf_null(r.j, "message") && nf_null(r.j, "host") && nf_null(r.j, "ip"));
    nf_free(&r);
    fake_join = (net_join_status_t){.state = NET_JOIN_FAILED, .error = "wrong_password", .message = "The password didn't work."};
    r = setup_req("GET", "/api/v1/setup/state", NULL);
    TB_EQ_STR(nf_str(r.j, "state"), "failed");
    TB_EQ_STR(nf_str(r.j, "error"), "wrong_password");
    TB_EQ_STR(nf_str(r.j, "message"), "The password didn't work.");
    nf_free(&r);
    fake_join = (net_join_status_t){.state = NET_JOIN_CONNECTED, .host = "minibar.local", .ip = "10.0.4.42"};
    r = setup_req("GET", "/api/v1/setup/state", NULL);
    TB_EQ_STR(nf_str(r.j, "state"), "connected");
    TB_EQ_STR(nf_str(r.j, "host"), "minibar.local");
    TB_EQ_STR(nf_str(r.j, "ip"), "10.0.4.42");
    nf_free(&r);
}

TB_TEST(setup_wifi_join)
{
    in_setup_mode();
    nf_resp_t r = setup_req("POST", "/api/v1/setup/wifi", "{\"ssid\": \"Office-Corp\", \"username\": \"sam.lee\", "
                            "\"password\": \"correct horse battery staple\", \"calendar_url\": null, \"time_zone\": \"America/Los_Angeles\"}");
    TB_EQ_INT(r.status, 202);
    TB_EQ_STR(nf_str(r.j, "state"), "connecting");
    nf_free(&r);
    TB_EQ_INT(fake_join_calls, 1);
    TB_EQ_STR(fake_join_ssid, "Office-Corp");
    TB_EQ_STR(fake_join_user, "sam.lee");
    TB_EQ_STR(fake_join_pass, "correct horse battery staple");
    TB_EQ_STR(fake_join_cal, "");
    TB_EQ_INT(nf_app.wifi_mode, TB_WIFI_CONNECTING);
    TB_EQ_STR(nf_app.wifi_ssid, "Office-Corp");
    TB_EQ_STR(nf_app.set.device.time_zone, "America/Los_Angeles");
    /* setting the bar up again (after a move) takes the phone's zone, unlike the Mac's hello (lead decision) */
    nf_app.wifi_mode = TB_WIFI_SETUP;
    nf_app.toast[0] = '\0';
    r = setup_req("POST", "/api/v1/setup/wifi", "{\"ssid\": \"Office-Corp\", \"username\": \"sam.lee\", "
                  "\"password\": \"correct horse battery staple\", \"time_zone\": \"Europe/Berlin\"}");
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_STR(nf_app.set.device.time_zone, "Europe/Berlin");
    TB_EQ_STR(nf_app.toast, "");    /* a zone change shows no toast */
    /* an unknown zone is ignored */
    nf_app.wifi_mode = TB_WIFI_SETUP;
    r = setup_req("POST", "/api/v1/setup/wifi", "{\"ssid\": \"Office-Corp\", \"username\": \"sam.lee\", "
                  "\"password\": \"correct horse battery staple\", \"time_zone\": \"Mars/Olympus\"}");
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_STR(nf_app.set.device.time_zone, "Europe/Berlin");
    /* a password network with a calendar address checked at once */
    nf_app.wifi_mode = TB_WIFI_SETUP;
    r = setup_req("POST", "/api/v1/setup/wifi", "{\"ssid\": \"Office-WiFi\", \"password\": \"hunter2hunter2\", "
                  "\"calendar_url\": \"webcal://calendar.google.com/calendar/ical/x/private-abcd/basic.ics\"}");
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_STR(fake_join_user, "");
    TB_TRUE(strstr(fake_join_cal, "basic.ics") != NULL);
    /* an open network: no password is sent even if one came */
    nf_app.wifi_mode = TB_WIFI_SETUP;
    r = setup_req("POST", "/api/v1/setup/wifi", "{\"ssid\": \"Office-Guest\", \"password\": \"whatever1\"}");
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_STR(fake_join_pass, "");
}

TB_TEST(setup_wifi_errors)
{
    in_setup_mode();
    struct { const char *body, *code, *field; } cases[] = {
        {"{}", "bad_request", "ssid"},
        {"{\"ssid\": \"\"}", "bad_value", "ssid"},
        {"{\"ssid\": \"012345678901234567890123456789012\"}", "bad_value", "ssid"},
        {"{\"ssid\": \"Office-WiFi\"}", "bad_request", "password"},
        {"{\"ssid\": \"Office-WiFi\", \"password\": \"short\"}", "bad_value", "password"},
        {"{\"ssid\": \"Office-Corp\", \"password\": \"pw\"}", "bad_request", "username"},
        {"{\"ssid\": \"Office-Corp\", \"username\": \"me\"}", "bad_request", "password"},
        {"{\"ssid\": \"Office-WiFi\", \"password\": 12345678}", "bad_request", "password"},
        {"{\"ssid\": \"Office-WiFi\", \"password\": \"longenough\", \"calendar_url\": \"http://x.example/a.ics\"}", "http_not_allowed", "calendar_url"},
        {"{\"ssid\": \"Office-WiFi\", \"password\": \"longenough\", \"calendar_url\": \"https://x.example/public/a.ics\"}", "public_address", "calendar_url"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        nf_resp_t r = setup_req("POST", "/api/v1/setup/wifi", cases[i].body);
        TB_EQ_INT(r.status, 400);
        TB_EQ_STR(nf_err(&r), cases[i].code);
        TB_EQ_STR(nf_str(r.j, "field"), cases[i].field);
        nf_free(&r);
    }
    TB_EQ_INT(fake_join_calls, 0);
    /* a hidden network (not in the scan) with a username is a work login */
    nf_resp_t r = setup_req("POST", "/api/v1/setup/wifi", "{\"ssid\": \"Hidden\", \"username\": \"me\", \"password\": \"pw\"}");
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_STR(fake_join_user, "me");
}

TB_TEST(setup_endpoints_only_on_the_setup_network)
{
    in_setup_mode();
    /* on the setup network only setup and info */
    nf_resp_t r = setup_req("GET", "/api/v1/info", NULL);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "wifi"), "setup");
    nf_free(&r);
    r = setup_req("GET", "/api/v1/status", NULL);
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    r = setup_req("POST", "/api/v1/pair/start", "{\"kind\": \"mac\", \"scope\": \"call\"}");
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    /* the Host must be the setup address there */
    net_req_t req = {.via = NET_VIA_SETUP, .method = "GET", .path = "/api/v1/setup/state", .host = "captive.apple.com", .peer_ip = 9};
    r = nf_req(&req);
    TB_EQ_INT(r.status, 421);
    nf_free(&r);
    /* not over the office Wi-Fi */
    fake_wifi.sta_up = true;
    snprintf(fake_wifi.ip, sizeof fake_wifi.ip, "10.0.4.42");
    r = nf_http("GET", "/api/v1/setup/networks", NULL, NULL);
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    /* and gone once setup is over */
    fake_wifi.state = NET_WIFI_CONNECTED;
    nf_app.wifi_mode = TB_WIFI_OK;
    r = setup_req("POST", "/api/v1/setup/wifi", "{\"ssid\": \"Office-WiFi\", \"password\": \"longenough\"}");
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    /* the page can still read the result while the setup network lingers after Connected */
    fake_join = (net_join_status_t){.state = NET_JOIN_CONNECTED, .host = "minibar.local", .ip = "10.0.4.42"};
    r = setup_req("GET", "/api/v1/setup/state", NULL);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "state"), "connected");
    nf_free(&r);
    /* and over the office Wi-Fi the setup address isn't the bar's */
    nf_host = NET_SETUP_IP;
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_INT(r.status, 421);
    nf_free(&r);
    cJSON *j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 3, \"method\": \"GET\", \"path\": \"/api/v1/setup/networks\"}");
    TB_EQ_INT(nf_num(j, "http_status"), 404);
    cJSON_Delete(j);
}
