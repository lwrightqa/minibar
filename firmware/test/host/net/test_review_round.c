/*
 * test_review_round.c: the router's fixes from the 2026-10-04 review round (QA's gap tests, adapted). Owner: lead
 * developer (net's folder).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_fixture.h"
#include "net_pair.h"
#include "net_util.h"
#include "tb_test.h"

#define MAC "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"

/* A phone asks for a code over Wi-Fi; meanwhile the Mac pairs over USB. The phone's code stays on the bar while it's
 * valid, info.pairing agrees with the screen, and the Mac's confirmation shows once the phone's pairing ends. */
TB_TEST(usb_pair_while_a_wifi_code_is_showing)
{
    nf_setup();
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"name\": \"iPhone\", \"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 202);
    char pid[17], code[8];
    snprintf(pid, sizeof pid, "%s", nf_str(r.j, "pairing_id"));
    snprintf(code, sizeof code, "%s", nf_app.pairing.code);
    nf_free(&r);
    TB_TRUE(tb_app_pairing_visible(&nf_app));
    TB_EQ_INT(nf_app.pairing.kind, TB_PAIR_KIND_PHONE);

    cJSON *j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 1, \"client\": \"" MAC "\", \"api\": \"1.0\"}");
    cJSON_Delete(j);
    j = nf_usb("@tb {\"cmd\": \"pair\", \"id\": 2, \"client\": \"" MAC "\"}");
    TB_TRUE(j && nf_true(j, "ok"));
    cJSON_Delete(j);

    TB_TRUE(tb_app_pairing_visible(&nf_app));
    TB_EQ_INT(nf_app.paired_count, 1);
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "showing");
    nf_free(&r);

    /* the phone types its code: paired, and then the Mac's confirmation */
    nf_advance(1100);
    char body[128];
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"%s\", \"cookie\": true}", pid, code);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    TB_FALSE(tb_app_pairing_visible(&nf_app));
    TB_EQ_STR(nf_app.toast, "Paired \xC2\xB7 iPhone");
    TB_EQ_INT(nf_app.paired_count, 2);
    nf_advance(TB_TOAST_MS + 100);
    TB_EQ_STR(nf_app.toast, "Paired \xC2\xB7 Mac \xC2\xB7 over USB");
}

/* api.md 2.5 and 4.6: a client name that's sent is 1 to 32 characters, for pair/start and for USB hello. */
TB_TEST(empty_client_names_are_refused)
{
    nf_setup();
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"name\": \"\", \"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 400);
    TB_EQ_STR(nf_err(&r), "bad_value");
    TB_EQ_STR(nf_str(r.j, "field"), "name");
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pair/start", "{\"name\": \"   \", \"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 400);
    nf_free(&r);
    TB_FALSE(nf_app.pairing.active);
    cJSON *j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 1, \"client\": \"" MAC "\", \"api\": \"1.0\", \"name\": \"\"}");
    TB_TRUE(j != NULL);
    TB_EQ_STR(nf_str(j, "error"), "bad_value");
    TB_EQ_STR(nf_str(j, "field"), "name");
    cJSON_Delete(j);
    /* a name the bar can't draw still falls back to the kind's word */
    r = nf_http("POST", "/api/v1/pair/start", "{\"name\": \"\xF0\x9F\x93\xB1\", \"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 202);
    TB_EQ_STR(nf_app.pairing.who, "Phone");
    nf_free(&r);
}

/* api.md 9.1: "Every action silences a ringing alarm first, as any control does." */
TB_TEST(pomodoro_pause_while_ringing_silences)
{
    nf_setup();
    const char *tok = nf_pair("remote", "full", NULL);
    TB_TRUE(tok != NULL);
    char t[64];
    snprintf(t, sizeof t, "%s", tok);
    nf_resp_t r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"start\"}", t);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    nf_advance(25 * 60 * 1000 + 500);
    TB_TRUE(nf_app.ringing);
    r = nf_http("POST", "/api/v1/pomodoro", "{\"action\": \"pause\"}", t);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "not_running");
    TB_FALSE(nf_app.ringing);
    nf_free(&r);
}

/* The 2 minutes count from when the code appears on the bar: a code asked for during the splash waits. */
TB_TEST(pairing_code_clock_starts_when_it_shows)
{
    nf_setup();
    tb_settings_t s = nf_app.set;
    tb_app_init(&nf_app, &s, TB_WIFI_OK, &fake_now);     /* a fresh boot: the splash is up */
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"mac\", \"scope\": \"call\"}", NULL);
    /* the router takes the code during the splash; the bar shows it after, and the 2 minutes start then */
    TB_EQ_INT(r.status, 202);
    TB_FALSE(tb_app_pairing_visible(&nf_app));
    nf_free(&r);
    nf_advance(1600);
    TB_TRUE(tb_app_pairing_visible(&nf_app));
    tb_ms_t shown = nf_app.pairing.shown_at;
    nf_advance(NET_PAIR_CODE_MS - 1000 - (fake_now.mono - shown));
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "showing");
    nf_free(&r);
    nf_advance(2000);
    TB_FALSE(nf_app.pairing.active);
    TB_EQ_STR(nf_app.toast, "Pairing timed out");
}

/* Forget all's Paired tile lists the names, most recently used first; Forget all from the bar empties it, and a call
 * the Mac reported over Wi-Fi ends with the mock-up's lead. */
TB_TEST(paired_names_and_forget_all)
{
    nf_setup();
    char mac_tok[64], phone_tok[64];
    snprintf(mac_tok, sizeof mac_tok, "%s", nf_pair("mac", "call", MAC));
    nf_advance(2000);
    snprintf(phone_tok, sizeof phone_tok, "%s", nf_pair("remote", "full", NULL));
    TB_EQ_INT(nf_app.paired_count, 2);
    TB_EQ_STR(nf_app.paired_names, "Phone, Mac");
    /* the Mac is used later: it moves to the front */
    nf_advance(3000);
    nf_resp_t r = nf_http("POST", "/api/v1/call",
                          "{\"client\": \"" MAC "\", \"session\": \"q8Zr2Lx0\", \"seq\": 1, \"active\": true, \"app\": \"Slack\"}",
                          mac_tok);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    TB_EQ_STR(nf_app.paired_names, "Mac, Phone");
    TB_TRUE(nf_app.call.active);
    nf_advance(TB_TOAST_MS + 100);
    /* Forget all on the bar (core sends TB_FX_FORGET_DEVICES; main hands it to net) */
    net_api_forget_devices(&fake_now);
    TB_EQ_INT(nf_app.paired_count, 0);
    TB_EQ_STR(nf_app.paired_names, "");
    TB_FALSE(nf_app.call.active);
    TB_TRUE(!strncmp(nf_app.toast, "Forgot 2 devices \xC2\xB7 back to ", 27));
    r = nf_http("GET", "/api/v1/status", NULL, phone_tok);
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
}

TB_TEST(join_error_headline_names_the_ip_address)
{
    TB_EQ_STR(net_join_err_screen(NET_JOIN_NO_ADDRESS), "No IP address");
    TB_EQ_STR(net_join_err_code(NET_JOIN_NO_ADDRESS), "no_address");
}
