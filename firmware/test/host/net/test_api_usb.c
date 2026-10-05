/* test_api_usb.c: the USB serial protocol (api.md section 6): hello, call, status, pair, request, errors, ready.
 * Owner: net builder. */
#include <stdlib.h>

#include "net_fixture.h"
#include "net_pair.h"
#include "tb_test.h"

#define MAC "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"

TB_TEST(usb_lines_without_the_marker_get_no_reply)
{
    nf_setup();
    TB_TRUE(nf_usb("hello") == NULL);
    TB_EQ_STR(nf_last_line, "");
    TB_TRUE(nf_usb("{\"cmd\": \"hello\"}") == NULL);
    TB_TRUE(nf_usb("@tb") == NULL);
    TB_TRUE(nf_usb("") == NULL);
    /* a too-long line without the marker is ignored too */
    char out[64];
    TB_FALSE(net_api_usb_line("xxxxxxxx", 8, true, out, sizeof out));
}

TB_TEST(usb_hello)
{
    nf_setup();
    cJSON *j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 1, \"client\": \"" MAC "\", \"app_version\": \"1.0 (12)\", \"api\": \"1.0\"}");
    TB_TRUE(j != NULL);
    TB_TRUE(!strncmp(nf_last_line, "@tb {\"id\":1,", 12));   /* the id comes first */
    TB_EQ_INT(nf_num(j, "id"), 1);
    TB_TRUE(nf_true(j, "ok"));
    TB_EQ_STR(nf_str(j, "device"), "MiniBar");
    TB_EQ_STR(nf_str(j, "device_id"), "f412fa3f2a1c");
    TB_EQ_STR(nf_str(j, "api"), "1.0");
    TB_EQ_STR(nf_str(j, "auth"), "bearer");
    TB_EQ_STR(nf_str(j, "wifi"), "connected");
    TB_TRUE(nf_get(j, "http_status") == NULL);
    cJSON_Delete(j);
    /* it counts as a heartbeat: the Mac is connected over USB, with no call */
    TB_EQ_INT(nf_app.mac_link, TB_LINK_USB);
    TB_FALSE(nf_app.call.active);
    j = nf_usb("@tb {\"cmd\": \"status\", \"id\": 2}");
    TB_EQ_STR(nf_str(j, "macs.0.via"), "usb");
    TB_TRUE(nf_true(j, "macs.0.connected"));
    TB_EQ_STR(nf_str(j, "macs.0.name"), "Mac");
    cJSON_Delete(j);
    /* with a name */
    j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 3, \"client\": \"" MAC "\", \"api\": \"1.0\", \"name\": \"Work Mac\"}");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"status\", \"id\": 4}");
    TB_EQ_STR(nf_str(j, "macs.0.name"), "Work Mac");
    cJSON_Delete(j);
}

TB_TEST(usb_hello_errors)
{
    nf_setup();
    cJSON *j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 8, \"client\": \"" MAC "\", \"api\": \"2.0\"}");
    TB_EQ_INT(nf_num(j, "id"), 8);
    TB_FALSE(nf_true(j, "ok"));
    TB_EQ_STR(nf_str(j, "error"), "unsupported_api");
    TB_EQ_STR(nf_str(j, "message"), "This MiniBar speaks API 1.0.");
    TB_EQ_STR(nf_str(j, "field"), "api");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 9, \"client\": \"" MAC "\"}");
    TB_EQ_STR(nf_str(j, "error"), "bad_request");
    TB_EQ_STR(nf_str(j, "field"), "api");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 10, \"api\": \"1.0\"}");
    TB_EQ_STR(nf_str(j, "field"), "client");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 11, \"api\": \"1.0\", \"client\": \"" MAC "\", \"time\": \"yesterday\"}");
    TB_EQ_STR(nf_str(j, "error"), "bad_value");
    TB_EQ_STR(nf_str(j, "field"), "time");
    cJSON_Delete(j);
}

TB_TEST(usb_hello_sets_the_clock_and_the_zone)
{
    nf_setup();
    /* a bar with no clock (Wi-Fi skipped, RTC lost its time) and no zone */
    fake_now.valid = false;
    fake_time_source = NET_TIME_NONE;
    cJSON *j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 1, \"client\": \"" MAC "\", \"api\": \"1.0\", \"time\": \"2026-10-04T14:11:58-07:00\", \"time_zone\": \"America/Los_Angeles\"}");
    TB_EQ_INT(fake_mac_time_calls, 1);
    TB_EQ_INT(fake_mac_time, 1791148318);
    TB_EQ_STR(nf_str(j, "time_source"), "mac");
    TB_TRUE(nf_str(j, "time") != NULL);
    cJSON_Delete(j);
    TB_EQ_STR(nf_app.set.device.time_zone, "America/Los_Angeles");
    /* the zone is only taken when the bar has none */
    j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 2, \"client\": \"" MAC "\", \"api\": \"1.0\", \"time_zone\": \"Europe/Berlin\"}");
    cJSON_Delete(j);
    TB_EQ_STR(nf_app.set.device.time_zone, "America/Los_Angeles");
    /* an unknown zone is ignored, not an error */
    nf_app.set.device.time_zone[0] = '\0';
    j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 3, \"client\": \"" MAC "\", \"api\": \"1.0\", \"time_zone\": \"Mars/Base\"}");
    TB_TRUE(nf_true(j, "ok"));
    cJSON_Delete(j);
    TB_EQ_STR(nf_app.set.device.time_zone, "");
}

TB_TEST(usb_call_and_status)
{
    nf_setup();
    cJSON *j = nf_usb("@tb {\"cmd\": \"call\", \"id\": 2, \"client\": \"" MAC "\", \"session\": \"q8Zr2Lx0\", \"seq\": 1, \"active\": true, \"app\": \"Slack\", \"call_id\": 1, \"elapsed_s\": 0}");
    TB_EQ_INT(nf_num(j, "id"), 2);
    TB_TRUE(nf_true(j, "ok"));
    TB_EQ_STR(nf_str(j, "showing"), "call");
    TB_EQ_STR(nf_str(j, "call.via"), "usb");
    TB_EQ_STR(nf_str(j, "call.app"), "Slack");
    TB_FALSE(nf_true(j, "stale"));
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"status\", \"id\": 3}");
    TB_EQ_INT(nf_num(j, "id"), 3);
    TB_EQ_STR(nf_str(j, "showing"), "call");
    TB_TRUE(nf_num(j, "rev") > 0);
    TB_EQ_STR(nf_str(j, "macs.0.via"), "usb");
    cJSON_Delete(j);
    /* the same Mac switches to Wi-Fi with the next seq: no stale call left behind */
    net_api_set_auth(false);
    nf_resp_t r = nf_http("POST", "/api/v1/call", "{\"client\": \"" MAC "\", \"session\": \"q8Zr2Lx0\", \"seq\": 2, \"active\": false}", NULL);
    TB_FALSE(nf_true(r.j, "call.active"));
    nf_free(&r);
    /* a late USB message is stale */
    j = nf_usb("@tb {\"cmd\": \"call\", \"id\": 4, \"client\": \"" MAC "\", \"session\": \"q8Zr2Lx0\", \"seq\": 1, \"active\": true}");
    TB_TRUE(nf_true(j, "stale"));
    TB_FALSE(nf_true(j, "call.active"));
    cJSON_Delete(j);
}

TB_TEST(usb_needs_no_token_even_in_bearer_mode)
{
    nf_setup();
    cJSON *j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 5, \"method\": \"PATCH\", \"path\": \"/api/v1/settings\", \"body\": {\"display\": {\"brightness\": 40}}}");
    TB_TRUE(!strncmp(nf_last_line, "@tb {\"id\":5,\"http_status\":200,\"ok\":true", 39));
    TB_EQ_INT(nf_num(j, "http_status"), 200);
    TB_EQ_INT(nf_num(j, "settings.display.brightness"), 40);
    TB_EQ_INT(nf_num(j, "settings.pomodoro.focus_min"), 25);
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 6, \"method\": \"POST\", \"path\": \"/api/v1/status\", \"body\": {\"status\": \"busy\"}}");
    TB_EQ_INT(nf_num(j, "http_status"), 200);
    TB_EQ_STR(nf_str(j, "own.status"), "busy");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 7, \"method\": \"GET\", \"path\": \"/api/v1/clients\"}");
    TB_EQ_INT(nf_num(j, "http_status"), 200);
    TB_EQ_INT(nf_num(j, "max"), 10);
    cJSON_Delete(j);
}

TB_TEST(usb_request_errors)
{
    nf_setup();
    cJSON *j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 1, \"method\": \"GET\", \"path\": \"/api/v1/nope\"}");
    TB_EQ_INT(nf_num(j, "http_status"), 404);
    TB_EQ_STR(nf_str(j, "error"), "not_found");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 2, \"method\": \"DELETE\", \"path\": \"/api/v1/info\"}");
    TB_EQ_INT(nf_num(j, "http_status"), 405);
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 3, \"method\": \"FETCH\", \"path\": \"/api/v1/info\"}");
    TB_EQ_INT(nf_num(j, "http_status"), 400);
    TB_EQ_STR(nf_str(j, "field"), "method");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 4, \"method\": \"GET\"}");
    TB_EQ_STR(nf_str(j, "field"), "path");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 5, \"method\": \"POST\", \"path\": \"/api/v1/message\", \"body\": [1]}");
    TB_EQ_STR(nf_str(j, "field"), "body");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 6, \"method\": \"POST\", \"path\": \"/api/v1/message\"}");
    TB_EQ_STR(nf_str(j, "error"), "bad_json");
    cJSON_Delete(j);
    /* clients/self has no meaning without a token */
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 7, \"method\": \"DELETE\", \"path\": \"/api/v1/clients/self\"}");
    TB_EQ_INT(nf_num(j, "http_status"), 400);
    cJSON_Delete(j);
    /* the status reply keeps its rev steady between two USB reads (the id isn't part of it) */
    j = nf_usb("@tb {\"cmd\": \"status\", \"id\": 100}");
    double rev = nf_num(j, "rev");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"status\", \"id\": 101}");
    TB_EQ_INT(nf_num(j, "rev"), rev);
    cJSON_Delete(j);
}

TB_TEST(usb_errors_as_in_api_md)
{
    nf_setup();
    cJSON *j = nf_usb("@tb {\"cmd\": \"call\", \"id\": 6, \"active\": \"yes\"");
    TB_EQ_STR(nf_last_line, "@tb {\"id\":null,\"ok\":false,\"error\":\"bad_json\",\"message\":\"That line isn't a JSON object.\",\"field\":null}");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"dance\", \"id\": 7}");
    TB_EQ_STR(nf_last_line, "@tb {\"id\":7,\"ok\":false,\"error\":\"unknown_cmd\",\"message\":\"Unknown cmd \\\"dance\\\".\",\"field\":\"cmd\"}");
    cJSON_Delete(j);
    j = nf_usb("@tb [1, 2]");
    TB_EQ_STR(nf_str(j, "error"), "bad_json");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"id\": 9}");
    TB_EQ_STR(nf_str(j, "error"), "bad_request");
    TB_EQ_STR(nf_str(j, "field"), "cmd");
    TB_EQ_INT(nf_num(j, "id"), 9);
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"status\", \"id\": 0}");
    TB_EQ_STR(nf_str(j, "field"), "id");
    TB_TRUE(nf_null(j, "id"));
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"status\", \"id\": 2147483648}");
    TB_EQ_STR(nf_str(j, "field"), "id");
    cJSON_Delete(j);
    /* no id: the reply's id is null */
    j = nf_usb("@tb {\"cmd\": \"status\"}");
    TB_TRUE(nf_null(j, "id"));
    TB_TRUE(nf_true(j, "ok"));
    cJSON_Delete(j);
    /* a CR before the LF is fine */
    j = nf_usb("@tb {\"cmd\": \"status\", \"id\": 12}\r");
    TB_EQ_INT(nf_num(j, "id"), 12);
    cJSON_Delete(j);
    /* too long */
    char out[NET_REPLY_MAX + 16];
    TB_TRUE(net_api_usb_line("@tb {\"cmd\": \"st", 15, true, out, sizeof out));
    TB_EQ_STR(out, "@tb {\"id\":null,\"ok\":false,\"error\":\"too_large\",\"message\":\"Lines can be up to 2048 bytes.\",\"field\":null}");
    /* every reply is one line */
    TB_TRUE(strchr(out, '\n') == NULL);
}

TB_TEST(usb_pair_gives_a_call_token)
{
    nf_setup();
    cJSON *j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 1, \"client\": \"" MAC "\", \"api\": \"1.0\", \"name\": \"Work Mac\"}");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"pair\", \"id\": 4, \"client\": \"" MAC "\"}");
    TB_EQ_INT(nf_num(j, "id"), 4);
    TB_TRUE(nf_true(j, "ok"));
    TB_EQ_INT(strlen(nf_str(j, "token")), 47);
    TB_EQ_STR(nf_str(j, "scope"), "call");
    TB_EQ_STR(nf_str(j, "device_id"), "f412fa3f2a1c");
    TB_EQ_STR(nf_str(j, "name"), "MiniBar 2A1C");
    TB_EQ_STR(nf_str(j, "host"), "minibar.local");
    char tok[NET_TOKEN_LEN + 1];
    snprintf(tok, sizeof tok, "%s", nf_str(j, "token"));
    cJSON_Delete(j);
    TB_TRUE(strstr(nf_app.toast, "Paired \xC2\xB7 Work Mac \xC2\xB7 over USB") != NULL);
    /* the token works over Wi-Fi with the call scope */
    nf_resp_t r = nf_http("POST", "/api/v1/call", "{\"client\": \"" MAC "\", \"active\": false}", tok);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    r = nf_http("GET", "/api/v1/clients", NULL, tok);
    TB_EQ_INT(r.status, 403);
    nf_free(&r);
    /* pairing again replaces the token */
    j = nf_usb("@tb {\"cmd\": \"pair\", \"id\": 5, \"client\": \"" MAC "\"}");
    cJSON_Delete(j);
    r = nf_http("GET", "/api/v1/status", NULL, tok);
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
    /* it works on the setup screens too */
    nf_app.wifi_mode = TB_WIFI_SETUP;
    j = nf_usb("@tb {\"cmd\": \"pair\", \"id\": 6, \"client\": \"" MAC "\"}");
    TB_TRUE(nf_true(j, "ok"));
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"pair\", \"id\": 7}");
    TB_EQ_STR(nf_str(j, "field"), "client");
    cJSON_Delete(j);
}

TB_TEST(usb_pair_token_limit)
{
    nf_setup();
    for (int i = 0; i < 10; i++) {
        char line[160];
        snprintf(line, sizeof line, "@tb {\"cmd\": \"pair\", \"id\": %d, \"client\": \"client-%04d\"}", i + 1, i);
        cJSON *j = nf_usb(line);
        TB_TRUE(nf_true(j, "ok"));
        cJSON_Delete(j);
    }
    cJSON *j = nf_usb("@tb {\"cmd\": \"pair\", \"id\": 4, \"client\": \"" MAC "\"}");
    TB_EQ_STR(nf_last_line, "@tb {\"id\":4,\"ok\":false,\"error\":\"token_limit\",\"message\":\"MiniBar already has 10 paired devices. Remove one on the Remote.\",\"field\":null}");
    cJSON_Delete(j);
}

TB_TEST(usb_ready_line)
{
    nf_setup();
    char out[200];
    net_api_usb_ready_line(out, sizeof out);
    TB_EQ_STR(out, "@tb {\"event\":\"ready\",\"device_id\":\"f412fa3f2a1c\",\"api\":\"1.0\",\"fw\":\"1.0.0\"}");
}

TB_TEST(usb_setup_endpoints_while_setting_up)
{
    nf_setup();
    fake_wifi = (net_wifi_info_t){.state = NET_WIFI_SETUP, .host = "minibar.local"};
    nf_app.wifi_mode = TB_WIFI_SETUP;
    cJSON *j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 1, \"method\": \"GET\", \"path\": \"/api/v1/setup/networks\"}");
    TB_EQ_INT(nf_num(j, "http_status"), 200);
    TB_EQ_INT(cJSON_GetArraySize(nf_get(j, "networks")), 4);
    cJSON_Delete(j);
    /* calls are recorded during setup and show once it closes */
    j = nf_usb("@tb {\"cmd\": \"call\", \"id\": 2, \"client\": \"" MAC "\", \"active\": true}");
    TB_EQ_STR(nf_str(j, "showing"), "setup");
    TB_TRUE(nf_true(j, "call.active"));
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 3, \"method\": \"POST\", \"path\": \"/api/v1/status\", \"body\": {\"status\": \"busy\"}}");
    TB_EQ_INT(nf_num(j, "http_status"), 409);
    TB_EQ_STR(nf_str(j, "error"), "in_setup");
    cJSON_Delete(j);
}

TB_TEST(usb_garbage_never_crashes)
{
    nf_setup();
    char line[600], out[NET_REPLY_MAX + 16];
    for (int seed = 0; seed < 3000; seed++) {
        srand((unsigned)seed);
        int n = 4 + rand() % 500;
        memcpy(line, "@tb ", 4);
        const char *alphabet = "{}[]\":,0123456789abcdefghijklmnopqrstuvwxyz-.\\ \x01\xC3\xA9\xFF";
        for (int i = 4; i < n; i++) line[i] = alphabet[rand() % (int)strlen(alphabet)];
        if (seed % 3 == 0) {     /* start from a valid command and break it */
            int k = snprintf(line, sizeof line, "@tb {\"cmd\": \"%s\", \"id\": %d, \"client\": \"" MAC "\", \"api\": \"1.0\", \"active\": true, \"method\": \"POST\", \"path\": \"/api/v1/pomodoro\", \"body\": {\"action\": \"toggle\"}}",
                             (const char *[]){"hello", "call", "status", "pair", "request"}[seed % 5], seed);
            if (k > 10) line[4 + rand() % (k - 4)] = alphabet[rand() % 20];
            n = k;
        }
        line[n] = '\0';
        TB_TRUE(net_api_usb_line(line, (size_t)n, false, out, sizeof out));
        TB_TRUE(!strncmp(out, "@tb {", 5));
        TB_TRUE(strchr(out, '\n') == NULL);
        if (seed % 50 == 0) nf_advance(1000);
    }
}
