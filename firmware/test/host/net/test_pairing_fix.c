/*
 * test_pairing_fix.c: the router and pairing after the mock-up's pairing fix round (2026-10-04, approved with
 * pairing). Owner: lead developer (net's folder).
 *   - A code on the screen holds one of the 10 places (api.md 4.3): USB pairing of another new device meanwhile gets
 *     token_limit, and pair refuses an eleventh as a safeguard that isn't a failed pairing.
 *   - POST /api/v1/pair/cancel (api.md 4.7): only the pairing_id works, 200 {"ok": true} or 409 not_pairing, pair's
 *     one-a-second limit, a failed pairing for the back-off, "Pairing canceled" on the bar.
 *   - Power off and Restart clear the back-off (api.md 4.9); pair/start isn't answered with a code while "Powering
 *     off" shows.
 *   - The text mapping of api.md 2.3 reaches the API (messages, names).
 *   - Appendix A: every endpoint is routed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_port.h"
#include "net_fixture.h"
#include "net_pair.h"
#include "tb_test.h"

#define MAC "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"

/* ---------- net_pair on its own ---------- */

static tb_clock_t at(tb_ms_t ms)
{
    return (tb_clock_t){.mono = ms, .wall = 1791148320 + ms / 1000, .valid = true};
}

static void fresh(net_pair_t *p)
{
    fake_reset();
    fake_tokens_clear();
    net_pair_init(p);
}

/* n tokens over USB, clients "usb-0000"... */
static void fill_usb(net_pair_t *p, int n, tb_ms_t t)
{
    for (int i = 0; i < n; i++) {
        char client[16], tok[NET_TOKEN_LEN + 1];
        snprintf(client, sizeof client, "usb-%04d", i);
        tb_clock_t now = at(t);
        TB_EQ_INT(net_pair_usb(p, client, "Mac", &now, tok, NULL), NET_PAIR_OK);
    }
}

TB_TEST(pair_code_holds_a_place)
{
    net_pair_t p;
    fresh(&p);
    fill_usb(&p, 9, 0);
    char pid[17], tok[NET_TOKEN_LEN + 1];
    int retry, left;
    tb_clock_t now = at(1000);
    /* a phone (no client ID) asks: 9 + its code = 10 */
    TB_EQ_INT(net_pair_start(&p, "iPhone", NET_KIND_REMOTE, NET_SCOPE_FULL, NULL, &now, pid, &retry), NET_PAIR_OK);
    /* a new Mac over USB meanwhile would make 11: refused (the cable keeps working without a token) */
    now = at(2000);
    TB_EQ_INT(net_pair_usb(&p, MAC, "Mac", &now, tok, NULL), NET_PAIR_TOKEN_LIMIT);
    TB_EQ_INT(net_pair_count(&p), 9);
    /* a device that's paired already replaces its own token, so it always has room */
    TB_EQ_INT(net_pair_usb(&p, "usb-0003", "Mac", &now, tok, NULL), NET_PAIR_OK);
    TB_EQ_INT(net_pair_count(&p), 9);
    /* the phone's code still works: 10 */
    char code[8];
    memcpy(code, p.code, sizeof code);
    now = at(3000);
    TB_EQ_INT(net_pair_finish(&p, pid, code, 0, &now, tok, NULL, &left), NET_PAIR_OK);
    TB_EQ_INT(net_pair_count(&p), 10);
    /* the place goes with the code: with 8 paired and a code up, one new USB pairing still fits */
    fresh(&p);
    fill_usb(&p, 8, 0);
    now = at(1000);
    TB_EQ_INT(net_pair_start(&p, "iPhone", NET_KIND_REMOTE, NET_SCOPE_FULL, NULL, &now, pid, &retry), NET_PAIR_OK);
    TB_EQ_INT(net_pair_usb(&p, MAC, "Mac", &now, tok, NULL), NET_PAIR_OK);
    TB_EQ_INT(net_pair_usb(&p, "another-mac", "Mac", &now, tok, NULL), NET_PAIR_TOKEN_LIMIT);
    /* once the code ends (canceled), its place is free again */
    net_pair_cancel(&p, &now);
    TB_EQ_INT(net_pair_usb(&p, "another-mac", "Mac", &now, tok, NULL), NET_PAIR_OK);
    TB_EQ_INT(net_pair_count(&p), 10);
}

/* The Mac asked for a code over Wi-Fi and is then plugged in: its own code doesn't keep it out. */
TB_TEST(pair_own_code_is_no_obstacle_over_usb)
{
    net_pair_t p;
    fresh(&p);
    fill_usb(&p, 9, 0);
    char pid[17], tok[NET_TOKEN_LEN + 1], code[8];
    int retry, left;
    tb_clock_t now = at(1000);
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, MAC, &now, pid, &retry), NET_PAIR_OK);
    TB_EQ_INT(net_pair_usb(&p, MAC, "Mac", &now, tok, NULL), NET_PAIR_OK);
    TB_EQ_INT(net_pair_count(&p), 10);
    /* and the code, typed after all, replaces that token: still 10 */
    memcpy(code, p.code, sizeof code);
    now = at(3000);
    TB_EQ_INT(net_pair_finish(&p, pid, code, 0, &now, tok, NULL, &left), NET_PAIR_OK);
    TB_EQ_INT(net_pair_count(&p), 10);
}

/* pair's safeguard: if the bar is somehow full when the right code comes, the pairing ends, not as a failure. */
TB_TEST(pair_safeguard_is_not_a_failure)
{
    net_pair_t p;
    fresh(&p);
    char pid[17], tok[NET_TOKEN_LEN + 1], code[8];
    int retry, left;
    tb_clock_t now = at(1000);
    TB_EQ_INT(net_pair_start(&p, "iPhone", NET_KIND_REMOTE, NET_SCOPE_FULL, NULL, &now, pid, &retry), NET_PAIR_OK);
    memcpy(code, p.code, sizeof code);
    p.failures_in_row = 1;          /* one failure before: a second would lock pair/start */
    fill_usb(&p, 9, 1500);          /* the held place keeps the tenth free... */
    TB_EQ_INT(net_pair_count(&p), 9);
    p.tokens[9] = p.tokens[0];      /* ...so take it behind the router's back */
    snprintf(p.tokens[9].client, sizeof p.tokens[9].client, "sneaked-in");
    snprintf(p.tokens[9].token_id, sizeof p.tokens[9].token_id, "0badf00d");
    TB_EQ_INT(net_pair_count(&p), 10);
    now = at(3000);
    TB_EQ_INT(net_pair_finish(&p, pid, code, 0, &now, tok, NULL, &left), NET_PAIR_TOKEN_LIMIT);
    TB_FALSE(p.showing);
    TB_EQ_INT(p.failures_in_row, 1);
    TB_EQ_STR(net_pair_state(&p, &now), "idle");
}

TB_TEST(pair_cancel_by_id)
{
    net_pair_t p;
    fresh(&p);
    char pid[17];
    int retry;
    tb_clock_t now = at(1000);
    TB_EQ_INT(net_pair_start(&p, "iPhone", NET_KIND_REMOTE, NET_SCOPE_FULL, NULL, &now, pid, &retry), NET_PAIR_OK);
    /* another pairing_id can't cancel it */
    TB_EQ_INT(net_pair_cancel_id(&p, "0000000000000000", &now), NET_PAIR_NOT_PAIRING);
    TB_TRUE(p.showing);
    /* within a second of that (pair's limit, shared) */
    now = at(1500);
    TB_EQ_INT(net_pair_cancel_id(&p, pid, &now), NET_PAIR_RATE_LIMITED);
    TB_TRUE(p.showing);
    now = at(2100);
    TB_EQ_INT(net_pair_cancel_id(&p, pid, &now), NET_PAIR_OK);
    TB_FALSE(p.showing);
    TB_EQ_INT(p.failures_in_row, 1);            /* a failed pairing, like a tap */
    now = at(3200);
    TB_EQ_INT(net_pair_cancel_id(&p, pid, &now), NET_PAIR_NOT_PAIRING);   /* already ended */
    TB_EQ_INT(p.failures_in_row, 1);
    /* a second one in a row: the back-off starts */
    now = at(4000);
    TB_EQ_INT(net_pair_start(&p, "iPhone", NET_KIND_REMOTE, NET_SCOPE_FULL, NULL, &now, pid, &retry), NET_PAIR_OK);
    now = at(5000);
    TB_EQ_INT(net_pair_cancel_id(&p, pid, &now), NET_PAIR_OK);
    TB_EQ_STR(net_pair_state(&p, &now), "locked");
    TB_EQ_INT(net_pair_locked_s(&p, &now), 30);
    /* shares the limit with pair: a pair right after a cancel waits */
    char tok[NET_TOKEN_LEN + 1];
    int left;
    TB_EQ_INT(net_pair_finish(&p, pid, "123456", 0, &now, tok, NULL, &left), NET_PAIR_RATE_LIMITED);
    /* an expired code can't be canceled */
    fresh(&p);
    now = at(1000);
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, MAC, &now, pid, &retry), NET_PAIR_OK);
    now = at(1000 + NET_PAIR_CODE_MS);
    TB_EQ_INT(net_pair_cancel_id(&p, pid, &now), NET_PAIR_NOT_PAIRING);
    TB_EQ_INT(net_pair_cancel_id(&p, NULL, &now), NET_PAIR_RATE_LIMITED);
    now = at(2500 + NET_PAIR_CODE_MS);
    TB_EQ_INT(net_pair_cancel_id(&p, NULL, &now), NET_PAIR_NOT_PAIRING);
}

TB_TEST(pair_reset_clears_the_back_off)
{
    net_pair_t p;
    fresh(&p);
    fill_usb(&p, 2, 0);
    char pid[17];
    int retry;
    tb_clock_t now = at(1000);
    for (int i = 0; i < 4; i++) {           /* four failures: 2 minutes */
        TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, MAC, &now, pid, &retry), i < 2 ? NET_PAIR_OK : NET_PAIR_RATE_LIMITED);
        if (i >= 2) { p.locked_until = 0; TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, MAC, &now, pid, &retry), NET_PAIR_OK); }
        net_pair_cancel(&p, &now);
    }
    TB_EQ_INT(net_pair_locked_s(&p, &now), 120);
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, MAC, &now, pid, &retry), NET_PAIR_RATE_LIMITED);
    net_pair_reset(&p);
    TB_EQ_STR(net_pair_state(&p, &now), "idle");
    TB_EQ_INT(p.failures_in_row, 0);
    TB_EQ_INT(net_pair_count(&p), 2);       /* paired devices stay paired */
    /* a code that's up ends without a failure */
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, MAC, &now, pid, &retry), NET_PAIR_OK);
    net_pair_reset(&p);
    TB_FALSE(p.showing);
    TB_EQ_INT(p.failures_in_row, 0);
    TB_EQ_INT(net_pair_start(&p, "Mac", NET_KIND_MAC, NET_SCOPE_CALL, MAC, &now, pid, &retry), NET_PAIR_OK);
}

/* ---------- through the router ---------- */

static void start_code(const char *body, char pid[17])
{
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", body, NULL);
    TB_EQ_INT(r.status, 202);
    snprintf(pid, 17, "%s", r.j ? nf_str(r.j, "pairing_id") : "");
    nf_free(&r);
}

static nf_resp_t cancel(const char *pid)
{
    char body[96];
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\"}", pid);
    return nf_http("POST", "/api/v1/pair/cancel", body, NULL);
}

TB_TEST(http_pair_cancel)
{
    nf_setup();
    char pid[17];
    start_code("{\"name\": \"iPhone\", \"kind\": \"remote\", \"scope\": \"full\"}", pid);
    TB_TRUE(tb_app_pairing_visible(&nf_app));
    char code[8];
    snprintf(code, sizeof code, "%s", nf_app.pairing.code);
    /* someone else's pairing_id: nothing happens */
    nf_resp_t r = cancel("0123456789abcdef");
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "not_pairing");
    TB_EQ_STR(nf_str(r.j, "field"), "pairing_id");
    nf_free(&r);
    TB_TRUE(tb_app_pairing_visible(&nf_app));
    /* within a second of that: pair's limit */
    r = cancel(pid);
    TB_EQ_INT(r.status, 429);
    TB_EQ_STR(nf_err(&r), "rate_limited");
    TB_EQ_INT(nf_num(r.j, "retry_after_s"), 1);
    TB_EQ_INT(r.r.retry_after_s, 1);
    nf_free(&r);
    nf_advance(1000);
    /* the device that asked: the code goes, as after a tap */
    r = cancel(pid);
    TB_EQ_INT(r.status, 200);
    TB_TRUE(nf_true(r.j, "ok"));
    TB_EQ_INT(cJSON_GetArraySize(r.j), 1);       /* {"ok": true} */
    nf_free(&r);
    TB_FALSE(nf_app.pairing.active);
    TB_EQ_STR(nf_app.toast, "Pairing canceled");
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "idle");
    nf_free(&r);
    /* the code is gone for pair too */
    nf_advance(1000);
    char body[128];
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"%s\"}", pid, code);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_STR(nf_err(&r), "not_pairing");
    nf_free(&r);
    /* safe to repeat: already ended */
    nf_advance(1000);
    r = cancel(pid);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "not_pairing");
    nf_free(&r);
    /* another device can ask at once */
    start_code("{\"kind\": \"mac\", \"scope\": \"call\"}", pid);
    TB_EQ_STR(nf_app.pairing.who, "Mac");
}

TB_TEST(http_pair_cancel_validation_and_methods)
{
    nf_setup();
    nf_resp_t r = nf_http("POST", "/api/v1/pair/cancel", "{}", NULL);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_err(&r), "bad_request");
    TB_EQ_STR(nf_str(r.j, "field"), "pairing_id");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pair/cancel", "{\"pairing_id\": 7}", NULL);
    TB_EQ_STR(nf_err(&r), "bad_request");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pair/cancel", NULL, NULL);
    TB_EQ_STR(nf_err(&r), "bad_json");
    nf_free(&r);
    r = nf_http("GET", "/api/v1/pair/cancel", NULL, NULL);
    TB_EQ_INT(r.status, 405);
    TB_EQ_STR(r.r.allow, "POST");
    nf_free(&r);
    /* no token needed, even with pairing on; not on the setup network */
    nf_advance(1000);
    r = nf_http("POST", "/api/v1/pair/cancel", "{\"pairing_id\": \"0123456789abcdef\"}", NULL);
    TB_EQ_INT(r.status, 409);
    nf_free(&r);
    net_req_t req = {.via = NET_VIA_SETUP, .method = "POST", .path = "/api/v1/pair/cancel", .host = "4.3.2.1",
                     .body = "{\"pairing_id\": \"x\"}", .body_len = 19, .content_type_json = true, .peer_ip = 0x0502030a};
    fake_wifi.state = NET_WIFI_SETUP;
    r = nf_req(&req);
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
}

/* Starting Wi-Fi setup ends any pairing, so pair/cancel answers not_pairing then (api.md 13), over USB too. */
TB_TEST(usb_pair_cancel_during_setup)
{
    nf_setup();
    char pid[17];
    start_code("{\"kind\": \"mac\", \"scope\": \"call\", \"client\": \"" MAC "\"}", pid);
    /* Wi-Fi setup starts on the bar (hold, Wi-Fi, Set up again): the code ends as canceled */
    tb_app_pointer(&nf_app, true, 200, 80, TB_TILE_NONE, &fake_now);
    nf_advance(100);
    tb_app_pointer(&nf_app, false, 200, 80, TB_TILE_NONE, &fake_now);   /* the tap cancels it first */
    nf_advance(100);
    TB_FALSE(nf_app.pairing.active);
    fake_wifi = (net_wifi_info_t){.state = NET_WIFI_SETUP, .host = "tinybar.local"};
    nf_app.wifi_mode = TB_WIFI_SETUP;
    char line[160];
    snprintf(line, sizeof line, "@tb {\"cmd\": \"request\", \"id\": 9, \"method\": \"POST\", \"path\": \"/api/v1/pair/cancel\", \"body\": {\"pairing_id\": \"%s\"}}", pid);
    cJSON *j = nf_usb(line);
    TB_EQ_INT(nf_num(j, "http_status"), 409);
    TB_EQ_STR(nf_str(j, "error"), "not_pairing");
    cJSON_Delete(j);
    /* pair/start answers in_setup meanwhile */
    nf_advance(1000);
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 10, \"method\": \"POST\", \"path\": \"/api/v1/pair/start\", \"body\": {\"kind\": \"mac\", \"scope\": \"call\"}}");
    TB_EQ_STR(nf_str(j, "error"), "in_setup");
    cJSON_Delete(j);
}

TB_TEST(http_pair_cancel_counts_as_a_failure)
{
    nf_setup();
    char pid[17];
    for (int i = 0; i < 2; i++) {
        start_code("{\"kind\": \"remote\", \"scope\": \"full\"}", pid);
        nf_advance(1000);
        nf_resp_t r = cancel(pid);
        TB_EQ_INT(r.status, 200);
        nf_free(&r);
    }
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 429);
    TB_EQ_STR(nf_err(&r), "rate_limited");
    TB_TRUE(nf_num(r.j, "retry_after_s") >= 29 && nf_num(r.j, "retry_after_s") <= 30);
    nf_free(&r);
    TB_FALSE(nf_app.pairing.active);
}

/* A phone's code holds the tenth place: a new Mac over USB gets token_limit, the phone's code still works. */
TB_TEST(http_code_holds_a_place_against_usb)
{
    nf_setup();
    for (int i = 0; i < 9; i++) {
        char line[160];
        snprintf(line, sizeof line, "@tb {\"cmd\": \"pair\", \"id\": %d, \"client\": \"client-%04d\"}", i + 1, i);
        cJSON *j = nf_usb(line);
        TB_TRUE(nf_true(j, "ok"));
        cJSON_Delete(j);
    }
    char pid[17];
    start_code("{\"name\": \"iPhone\", \"kind\": \"remote\", \"scope\": \"full\"}", pid);
    cJSON *j = nf_usb("@tb {\"cmd\": \"pair\", \"id\": 20, \"client\": \"" MAC "\"}");
    TB_EQ_STR(nf_str(j, "error"), "token_limit");
    cJSON_Delete(j);
    TB_TRUE(tb_app_pairing_visible(&nf_app));        /* the code stays up; no "Paired · Mac" waits behind it */
    TB_EQ_STR(nf_app.pending_toast, "");
    /* the cable keeps working without a token */
    j = nf_usb("@tb {\"cmd\": \"call\", \"id\": 21, \"client\": \"" MAC "\", \"session\": \"s\", \"seq\": 1, \"active\": true}");
    TB_TRUE(nf_true(j, "ok"));
    cJSON_Delete(j);
    nf_advance(1000);
    char body[128];
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"%s\", \"cookie\": true}", pid, nf_app.pairing.code);
    nf_resp_t r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    TB_EQ_INT(nf_app.paired_count, 10);
    TB_EQ_STR(nf_app.toast, "Paired \xC2\xB7 iPhone");
    /* full now: another device's pair/start is refused before any code shows */
    r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"automation\", \"scope\": \"full\"}", NULL);
    TB_EQ_STR(nf_err(&r), "token_limit");
    nf_free(&r);
    TB_FALSE(nf_app.pairing.active);
}

/* PWR held 3 s, Power off and Restart clear the back-off; "Powering off" answers no code. */
TB_TEST(power_off_and_restart_clear_the_back_off)
{
    nf_setup();
    char pid[17];
    for (int i = 0; i < 2; i++) {
        start_code("{\"kind\": \"remote\", \"scope\": \"full\"}", pid);
        nf_advance(1000);
        nf_free((nf_resp_t[]){cancel(pid)});
    }
    nf_resp_t r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "locked");
    nf_free(&r);
    /* PWR held: "Powering off" shows, the back-off is gone, and pair/start gets no code */
    tb_app_button(&nf_app, TB_BTN_PWR_DOWN, &fake_now);
    nf_advance(3050);
    TB_TRUE(nf_app.powering_off);
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "idle");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 503);
    TB_EQ_STR(nf_err(&r), "busy");
    nf_free(&r);
    TB_FALSE(nf_app.pairing.active);
    /* Restart from the menu (hold, Power, Restart) */
    nf_setup();
    for (int i = 0; i < 2; i++) {
        start_code("{\"kind\": \"remote\", \"scope\": \"full\"}", pid);
        nf_advance(1000);
        nf_free((nf_resp_t[]){cancel(pid)});
    }
    tb_app_pointer(&nf_app, true, 200, 80, TB_TILE_NONE, &fake_now);
    nf_advance(700);
    tb_app_pointer_poll(&nf_app, &fake_now);
    tb_app_pointer(&nf_app, false, 200, 80, TB_TILE_NONE, &fake_now);
    for (int step = 0; step < 2; step++) {
        tb_action_t want = step == 0 ? TB_ACT_POWER : TB_ACT_RESTART;
        int8_t t = -1;
        for (int i = 0; i < nf_app.menu.n; i++)
            if (nf_app.menu.tiles[i].action == want) t = (int8_t)i;
        TB_TRUE(t >= 0);
        tb_app_pointer(&nf_app, true, 60, 80, t, &fake_now);
        nf_advance(50);
        tb_app_pointer(&nf_app, false, 60, 80, t, &fake_now);
        nf_advance(50);
    }
    TB_TRUE(nf_app.booting);
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "idle");
    nf_free(&r);
    /* during the splash a code may be asked for: it shows once the bar has started */
    r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_FALSE(tb_app_pairing_visible(&nf_app));
    nf_advance(TB_BOOT_SPLASH_MS + 100);
    TB_TRUE(tb_app_pairing_visible(&nf_app));
}

/* api.md 2.3 through the API: what nobody sees never makes a message fail; what's left is named. */
TB_TEST(http_text_mapping_reaches_the_api)
{
    nf_setup();
    const char *tok = nf_pair("remote", "full", "phone-0001");
    TB_TRUE(tok != NULL);
    nf_resp_t r = nf_http("POST", "/api/v1/message", "{\"text\": \"Back at 3:00\xE2\x80\xAFPM\xE2\x80\x8B\"}", tok);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "message.text"), "Back at 3:00 PM");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/message", "{\"text\": \"\xEF\xBB\xBF" "Cafe\xCC\x81 at noon\"}", tok);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "message.text"), "Caf\xC3\xA9 at noon");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/message", "{\"text\": \"Busy\xEF\xBF\xB9\"}", tok);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_err(&r), "unsupported_chars");
    TB_EQ_STR(nf_str(r.j, "chars.0"), "\xEF\xBF\xB9");
    nf_free(&r);
    /* a decomposed name pairs under its composed form */
    nf_advance(1000);
    r = nf_http("POST", "/api/v1/pair/start", "{\"name\": \"Jose\xCC\x81's phone\", \"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_STR(nf_app.pairing.who, "Jos\xC3\xA9's phone");
}

/* Appendix A: every endpoint answers on its method (not 404 or 405), with a full token or over USB. */
TB_TEST(appendix_a_every_endpoint_is_routed)
{
    nf_setup();
    const char *tok = nf_pair("remote", "full", "phone-0002");
    TB_TRUE(tok != NULL);
    static const struct { const char *m, *p, *body; } E[] = {
        {"GET", "/api/v1/info", NULL},
        {"POST", "/api/v1/pair/start", "{}"},
        {"POST", "/api/v1/pair", "{}"},
        {"POST", "/api/v1/pair/cancel", "{}"},
        {"GET", "/api/v1/status", NULL},
        {"POST", "/api/v1/call", "{}"},
        {"POST", "/api/v1/status", "{}"},
        {"POST", "/api/v1/message", "{}"},
        {"POST", "/api/v1/aside", "{}"},
        {"POST", "/api/v1/pomodoro", "{}"},
        {"GET", "/api/v1/settings", NULL},
        {"PATCH", "/api/v1/settings", "{}"},
        {"GET", "/api/v1/calendar", NULL},
        {"PUT", "/api/v1/calendar", "{}"},
        {"POST", "/api/v1/calendar/sync", "{}"},
        {"GET", "/api/v1/clients", NULL},
        {"DELETE", "/api/v1/clients/00000000", NULL},
    };
    for (size_t i = 0; i < sizeof E / sizeof E[0]; i++) {
        nf_advance(1000);
        nf_resp_t r = nf_http(E[i].m, E[i].p, E[i].body, tok);
        if (r.status == 405 || (r.status == 404 && strcmp(E[i].p, "/api/v1/clients/00000000")))
            TB_FAIL_AT("%s %s answered %d", E[i].m, E[i].p, r.status);
        nf_free(&r);
    }
    nf_resp_t r = nf_http("DELETE", "/api/v1/clients/00000000", NULL, tok);
    TB_EQ_STR(nf_err(&r), "not_found");                 /* an unknown token_id, not an unknown path */
    TB_EQ_STR(nf_str(r.j, "field"), "token_id");
    nf_free(&r);
    /* the setup endpoints: over USB while setting up */
    fake_wifi = (net_wifi_info_t){.state = NET_WIFI_SETUP, .host = "tinybar.local"};
    static const char *const SETUP[] = {
        "{\"method\": \"GET\", \"path\": \"/api/v1/setup/networks\"}",
        "{\"method\": \"POST\", \"path\": \"/api/v1/setup/wifi\", \"body\": {}}",
        "{\"method\": \"GET\", \"path\": \"/api/v1/setup/state\"}",
    };
    for (size_t i = 0; i < sizeof SETUP / sizeof SETUP[0]; i++) {
        char line[200];
        snprintf(line, sizeof line, "@tb {\"cmd\": \"request\", \"id\": %d, %s", (int)i + 1, SETUP[i] + 1);
        cJSON *j = nf_usb(line);
        int st = (int)nf_num(j, "http_status");
        if (st == 404 || st == 405) TB_FAIL_AT("%s answered %d", SETUP[i], st);
        cJSON_Delete(j);
    }
    /* DELETE /api/v1/clients/self last (it unpairs this token) */
    r = nf_http("DELETE", "/api/v1/clients/self", NULL, tok);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
}

/* Appendix B: the pairing codes and their statuses as the router sends them. */
TB_TEST(appendix_b_pairing_codes)
{
    nf_setup();
    char pid[17];
    start_code("{\"kind\": \"mac\", \"scope\": \"call\"}", pid);
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "pairing_busy");
    TB_TRUE(nf_num(r.j, "retry_after_s") > 0);
    TB_EQ_INT(r.r.retry_after_s, (int)nf_num(r.j, "retry_after_s"));
    nf_free(&r);
    nf_advance(1000);
    char body[96];
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"abc\"}", pid);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 403);
    TB_EQ_STR(nf_err(&r), "wrong_code");
    TB_EQ_INT(nf_num(r.j, "attempts_left"), 2);
    nf_free(&r);
    nf_advance(1000);
    r = cancel(pid);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    nf_advance(1000);
    r = cancel(pid);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "not_pairing");
    nf_free(&r);
}
