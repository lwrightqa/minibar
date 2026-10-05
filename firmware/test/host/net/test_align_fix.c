/*
 * test_align_fix.c: the review of the firmware's pairing alignment (2026-10-05). Owner: lead developer (net's folder).
 *   - Removing a Mac that is on a Wi-Fi call toasts once, with the removal as the lead ("Removed Mac · back to Busy"),
 *     as the mock-up's removeDevice() does; without a call, "Removed Mac".
 *   - pairing_seq (api.md 4.6, 7.1): pair/start's reply and info carry the number of the code on the screen, so the
 *     device that asked can tell its own code from the next device's within one poll of info.
 *   - pair and pair/cancel's one-a-second rate_limited carries retry_after_s 1 (api.md 4.7).
 *   - GET /api/v1/clients reports kind "device" for a token paired with a kind the bar didn't know (api.md 12.1).
 */
#include <stdio.h>
#include <string.h>

#include "fake_port.h"
#include "net_fixture.h"
#include "net_pair.h"
#include "tb_test.h"

#define MAC "6F1C2A9E-5B7D-4E0A-9C3B-2D8F1A7E4B60"

/* A Mac (scope call, client MAC) and a phone (full) paired over Wi-Fi; the phone picks Busy. */
static void mac_and_phone(char mac_tok[NET_TOKEN_LEN + 1], char phone_tok[NET_TOKEN_LEN + 1])
{
    nf_setup();
    snprintf(mac_tok, NET_TOKEN_LEN + 1, "%s", nf_pair("mac", "call", MAC));
    nf_advance(1100);
    snprintf(phone_tok, NET_TOKEN_LEN + 1, "%s", nf_pair("remote", "full", NULL));
    nf_advance(TB_TOAST_MS + 100);
    nf_resp_t r = nf_http("POST", "/api/v1/status", "{\"status\": \"busy\"}", phone_tok);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    nf_advance(TB_TOAST_MS + 100);
}

static void wifi_call(const char *mac_tok, bool active)
{
    char body[256];
    snprintf(body, sizeof body, "{\"client\": \"" MAC "\", \"session\": \"s1\", \"seq\": %d, \"active\": %s, \"app\": \"Slack\", "
             "\"call_id\": 1, \"elapsed_s\": 0}", active ? 1 : 2, active ? "true" : "false");
    nf_resp_t r = nf_http("POST", "/api/v1/call", body, mac_tok);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
}

/* The Mac's token_id from the list. */
static void mac_token_id(const char *phone_tok, char id[9])
{
    id[0] = '\0';
    nf_resp_t r = nf_http("GET", "/api/v1/clients", NULL, phone_tok);
    TB_EQ_INT(r.status, 200);
    for (int i = 0; i < NET_TOKENS_MAX; i++) {
        char path[32];
        snprintf(path, sizeof path, "clients.%d.kind", i);
        const char *k = nf_str(r.j, path);
        if (!k) break;
        if (!strcmp(k, "mac")) {
            snprintf(path, sizeof path, "clients.%d.token_id", i);
            snprintf(id, 9, "%s", nf_str(r.j, path));
        }
    }
    nf_free(&r);
    TB_TRUE(id[0] != 0);
}

static void remove_mac(const char *phone_tok)
{
    char id[9], path[64];
    mac_token_id(phone_tok, id);
    snprintf(path, sizeof path, "/api/v1/clients/%s", id);
    nf_resp_t r = nf_http("DELETE", path, NULL, phone_tok);
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "revoked"), id);
    nf_free(&r);
    nf_advance(50);
}

TB_TEST(remove_mac_on_a_wifi_call_toasts_once)
{
    char mac_tok[NET_TOKEN_LEN + 1], phone_tok[NET_TOKEN_LEN + 1];
    mac_and_phone(mac_tok, phone_tok);
    wifi_call(mac_tok, true);
    nf_advance(TB_TOAST_MS + 200);
    TB_EQ_INT(tb_app_showing(&nf_app, &fake_now), TB_SHOWING_CALL);
    remove_mac(phone_tok);
    TB_EQ_INT(tb_app_showing(&nf_app, &fake_now), TB_SHOWING_OWN);
    /* the mock-up's removeDevice(): if (!syncAuto(gone)) toast(gone), so the call's end carries the removal */
    TB_EQ_STR(nf_app.toast, "Removed Mac \xC2\xB7 back to Busy");
    TB_EQ_STR(nf_app.pending_toast, "");
    /* and the Mac's token is gone: its next heartbeat is refused */
    nf_resp_t r = nf_http("POST", "/api/v1/call", "{\"client\": \"" MAC "\", \"active\": false}", mac_tok);
    TB_EQ_INT(r.status, 401);
    nf_free(&r);
}

TB_TEST(remove_mac_without_a_call_says_removed)
{
    char mac_tok[NET_TOKEN_LEN + 1], phone_tok[NET_TOKEN_LEN + 1];
    mac_and_phone(mac_tok, phone_tok);
    wifi_call(mac_tok, false);      /* a heartbeat with no call: the Mac is connected, nothing to end */
    nf_advance(TB_TOAST_MS + 200);
    TB_EQ_INT(tb_app_showing(&nf_app, &fake_now), TB_SHOWING_OWN);
    remove_mac(phone_tok);
    TB_EQ_STR(nf_app.toast, "Removed Mac");
    TB_EQ_STR(nf_app.pending_toast, "");
}

TB_TEST(remove_mac_with_its_call_set_aside)
{
    char mac_tok[NET_TOKEN_LEN + 1], phone_tok[NET_TOKEN_LEN + 1];
    mac_and_phone(mac_tok, phone_tok);
    wifi_call(mac_tok, true);
    nf_advance(TB_TOAST_MS + 200);
    /* a tap on the On a call screen sets the call aside */
    tb_app_pointer(&nf_app, true, 300, 80, TB_TILE_NONE, &fake_now);
    tb_app_pointer(&nf_app, false, 300, 80, TB_TILE_NONE, &fake_now);
    nf_advance(TB_TOAST_MS + 200);
    TB_EQ_INT(tb_app_showing(&nf_app, &fake_now), TB_SHOWING_OWN);
    TB_TRUE(nf_app.aside_call != 0);
    remove_mac(phone_tok);
    /* the screen doesn't change, so the toast says the set-aside call ended with it (one toast, as for Forget all) */
    TB_EQ_STR(nf_app.toast, "Removed Mac \xC2\xB7 call ended");
    TB_EQ_STR(nf_app.pending_toast, "");
    TB_FALSE(tb_app_call_now(&nf_app));
}

TB_TEST(self_unpair_on_a_call_toasts_once)
{
    char mac_tok[NET_TOKEN_LEN + 1], phone_tok[NET_TOKEN_LEN + 1];
    mac_and_phone(mac_tok, phone_tok);
    wifi_call(mac_tok, true);
    nf_advance(TB_TOAST_MS + 200);
    TB_EQ_INT(tb_app_showing(&nf_app, &fake_now), TB_SHOWING_CALL);
    /* the Mac app's Unpair (or its answer to 403 wrong_client, api.md 16): DELETE /clients/self with its token */
    nf_resp_t r = nf_http("DELETE", "/api/v1/clients/self", NULL, mac_tok);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    nf_advance(50);
    TB_EQ_INT(tb_app_showing(&nf_app, &fake_now), TB_SHOWING_OWN);
    TB_EQ_STR(nf_app.toast, "Removed Mac \xC2\xB7 back to Busy");
    TB_EQ_STR(nf_app.pending_toast, "");
}

TB_TEST(pairing_seq_tells_one_code_from_the_next)
{
    nf_setup();
    nf_resp_t r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "idle");
    TB_TRUE(nf_null(r.j, "pairing_seq"));        /* present and null: no code on the screen (api.md 2.3) */
    nf_free(&r);
    /* a USB pairing shows no code and takes no number: the first code is still number 1 */
    cJSON *j = nf_usb("@tb {\"cmd\": \"pair\", \"id\": 1, \"client\": \"usb-0001\"}");
    TB_TRUE(nf_true(j, "ok"));
    cJSON_Delete(j);
    nf_advance(TB_TOAST_MS + 100);
    r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"remote\", \"scope\": \"full\", \"name\": \"iPhone\"}", NULL);
    TB_EQ_INT(r.status, 202);
    double seq = nf_num(r.j, "pairing_seq");
    TB_EQ_INT((int)seq, 1);
    nf_free(&r);
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "showing");
    TB_EQ_INT((int)nf_num(r.j, "pairing_seq"), (int)seq);
    nf_free(&r);
    /* the USB hello reply is the same object */
    j = nf_usb("@tb {\"cmd\": \"hello\", \"id\": 2, \"client\": \"" MAC "\", \"api\": \"1.0\"}");
    TB_EQ_INT((int)nf_num(j, "pairing_seq"), (int)seq);
    cJSON_Delete(j);
    /* a tap on the bar cancels it; the next device's code gets the next number, so a prompt that polls info can tell
     * the two apart even when the second code follows within its 2 s poll */
    tb_app_pointer(&nf_app, true, 300, 80, TB_TILE_NONE, &fake_now);
    tb_app_pointer(&nf_app, false, 300, 80, TB_TILE_NONE, &fake_now);
    nf_advance(100);
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "idle");
    TB_TRUE(nf_null(r.j, "pairing_seq"));
    nf_free(&r);
    j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 3, \"method\": \"POST\", \"path\": \"/api/v1/pair/start\", "
               "\"body\": {\"kind\": \"mac\", \"scope\": \"call\", \"client\": \"" MAC "\"}}");
    TB_EQ_INT(nf_num(j, "http_status"), 202);
    TB_EQ_INT((int)nf_num(j, "pairing_seq"), (int)seq + 1);
    char pid[32], body[96];
    snprintf(pid, sizeof pid, "%s", nf_str(j, "pairing_id"));
    cJSON_Delete(j);
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_EQ_STR(nf_str(r.j, "pairing"), "showing");
    TB_EQ_INT((int)nf_num(r.j, "pairing_seq"), (int)seq + 1);
    nf_free(&r);
    /* the Mac types its code (a success, so no back-off from the tap above); the next code is number 3 */
    nf_advance(1100);
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"%s\"}", pid, nf_app.pairing.code);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    nf_advance(TB_TOAST_MS + 100);
    r = nf_http("GET", "/api/v1/info", NULL, NULL);
    TB_TRUE(nf_null(r.j, "pairing_seq"));
    nf_free(&r);
    r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"automation\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 202);
    TB_EQ_INT((int)nf_num(r.j, "pairing_seq"), (int)seq + 2);
    nf_free(&r);
}

TB_TEST(pair_and_cancel_rate_limit_carry_retry_after_s)
{
    nf_setup();
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 202);
    char pid[32], body[96];
    snprintf(pid, sizeof pid, "%s", nf_str(r.j, "pairing_id"));
    nf_free(&r);
    nf_advance(100);
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"000000\"}", pid);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 403);           /* a wrong code (or the right one): it went through */
    nf_free(&r);
    nf_advance(200);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 429);
    TB_EQ_STR(nf_err(&r), "rate_limited");
    TB_EQ_INT((int)nf_num(r.j, "retry_after_s"), 1);     /* api.md 4.7: the Mac app waits exactly this long */
    TB_EQ_INT(r.r.retry_after_s, 1);                      /* and the Retry-After header agrees */
    nf_free(&r);
    nf_advance(200);
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\"}", pid);
    r = nf_http("POST", "/api/v1/pair/cancel", body, NULL);
    TB_EQ_INT(r.status, 429);
    TB_EQ_STR(nf_err(&r), "rate_limited");
    TB_EQ_INT((int)nf_num(r.j, "retry_after_s"), 1);
    TB_EQ_INT(r.r.retry_after_s, 1);
    nf_free(&r);
    nf_advance(1000);
    r = nf_http("POST", "/api/v1/pair/cancel", body, NULL);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    /* safe to repeat (api.md 2.6): a second cancel answers not_pairing and counts no second failure, so the next
     * pair/start still gets a code (two failures in a row would have locked it for 30 s) */
    nf_advance(1100);
    r = nf_http("POST", "/api/v1/pair/cancel", body, NULL);
    TB_EQ_INT(r.status, 409);
    TB_EQ_STR(nf_err(&r), "not_pairing");
    nf_free(&r);
    nf_advance(100);
    r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"remote\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
}

TB_TEST(clients_list_says_device_for_an_unknown_kind)
{
    nf_setup();
    const char *phone_tok = nf_pair("remote", "full", NULL);
    TB_TRUE(phone_tok != NULL);
    char keep[NET_TOKEN_LEN + 1];
    snprintf(keep, sizeof keep, "%s", phone_tok);
    nf_advance(TB_TOAST_MS + 100);
    nf_resp_t r = nf_http("POST", "/api/v1/pair/start", "{\"kind\": \"watch\", \"scope\": \"full\"}", NULL);
    TB_EQ_INT(r.status, 202);
    char pid[32], body[96];
    snprintf(pid, sizeof pid, "%s", nf_str(r.j, "pairing_id"));
    nf_free(&r);
    TB_EQ_STR(nf_app.pairing.who, "Device");         /* the screen's label for a kind it doesn't know (api.md 4.6) */
    nf_advance(1100);
    snprintf(body, sizeof body, "{\"pairing_id\": \"%s\", \"code\": \"%s\"}", pid, nf_app.pairing.code);
    r = nf_http("POST", "/api/v1/pair", body, NULL);
    TB_EQ_INT(r.status, 200);
    nf_free(&r);
    r = nf_http("GET", "/api/v1/clients", NULL, keep);
    TB_EQ_INT(r.status, 200);
    bool found = false;
    for (int i = 0; i < 2; i++) {
        char path[32];
        snprintf(path, sizeof path, "clients.%d.name", i);
        if (nf_str(r.j, path) && !strcmp(nf_str(r.j, path), "Device")) {
            snprintf(path, sizeof path, "clients.%d.kind", i);
            TB_EQ_STR(nf_str(r.j, path), "device");  /* the original string ("watch") isn't kept (api.md 12.1) */
            found = true;
        }
    }
    TB_TRUE(found);
    nf_free(&r);
}
