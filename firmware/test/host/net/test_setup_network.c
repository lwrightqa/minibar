/*
 * test_setup_network.c: the setup network once setup is over (2026-10-05). Owner: lead developer (net's folder).
 *
 * The user asked: "Once the bar is setup, I want to make it stop broadcasting its network." The bar already closed
 * MiniBar-Setup 15 s after the Connected screen; this round makes that hold when something goes wrong:
 *   - POST setup/wifi is refused from the moment a join works (the Connected screen on), so nobody on the open
 *     network can point the bar elsewhere and keep the network up (api.md 13.2; decisions.md "Connected is the end of
 *     setup"). The page can still read the result (setup/state).
 *   - net catches up with core when core's TB_FX_WIFI_DONE or _SKIP was lost (core's effect queue drops effects when
 *     it's full): net_setup_catch_up(), which the app task asks every loop through net_setup_follow().
 *   - net finds the network up (or still meant to be) outside setup with no close on its way, and closes it:
 *     net_setup_ap_stray(), checked once a second.
 *   - a close that fails is tried again, changing the radio's mode twice more and then restarting the radio in
 *     station mode (net_ap_close_retry_ms()), and a save of the network that fails is tried again
 *     (net_creds_save_retry_ms()), so a failed write doesn't reopen the setup network at the next start.
 * The device side (net_wifi.c) was also run on a modelled ESP-IDF; see firmware/README.md, "What's verified".
 */
#include <stdio.h>
#include <string.h>

#include "fake_port.h"
#include "net_fixture.h"
#include "net_util.h"
#include "tb_test.h"

/* ======================================================================================================== */
/* POST setup/wifi ends with a join that worked                                                            */
/* ======================================================================================================== */

static nf_resp_t sn_post(const char *body)
{
    net_req_t req = {.via = NET_VIA_SETUP, .method = "POST", .path = "/api/v1/setup/wifi", .body = body,
                     .body_len = strlen(body), .content_type_json = true, .host = NET_SETUP_IP, .peer_ip = 0x02020304};
    return nf_req(&req);
}

static nf_resp_t sn_state(void)
{
    net_req_t req = {.via = NET_VIA_SETUP, .method = "GET", .path = "/api/v1/setup/state", .host = NET_SETUP_IP,
                     .peer_ip = 0x02020304};
    return nf_req(&req);
}

/* A bar on its QR code, with the setup network up. */
static void sn_setup_screens(void)
{
    nf_setup();
    fake_wifi = (net_wifi_info_t){.state = NET_WIFI_SETUP, .host = "minibar.local"};
    nf_app.wifi_mode = TB_WIFI_SETUP;
}

/* The phone sends the office Wi-Fi and the join works: net's join status and core's Connected screen. */
static void sn_join_works(void)
{
    nf_resp_t r = sn_post("{\"ssid\": \"Office-WiFi\", \"password\": \"longenough\"}");
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    fake_join = (net_join_status_t){.state = NET_JOIN_CONNECTED, .host = "minibar.local", .ip = "10.0.4.42"};
    tb_app_wifi_connected(&nf_app, "Office-WiFi", "10.0.4.42", "minibar.local", &fake_now);
}

TB_TEST(setup_wifi_refused_on_the_connected_screen)
{
    sn_setup_screens();
    sn_join_works();
    TB_EQ_INT(nf_app.wifi_mode, TB_WIFI_CONNECTED);
    TB_EQ_INT(fake_join_calls, 1);
    /* A second phone on the open network (or the same page's Connect again) during the 3 s Connected screen: before
     * 1.0.4 this got 202, sent the bar back to Connecting and kept the setup network up for another join. */
    nf_resp_t r = sn_post("{\"ssid\": \"Elsewhere\", \"password\": \"longenough\"}");
    TB_EQ_INT(r.status, 404);
    TB_EQ_STR(nf_err(&r), "not_found");
    TB_EQ_STR(nf_str(r.j, "message"), "MiniBar isn't in Wi-Fi setup anymore.");
    nf_free(&r);
    TB_EQ_INT(fake_join_calls, 1);                      /* nothing joined */
    TB_EQ_INT(nf_app.wifi_mode, TB_WIFI_CONNECTED);     /* the screen stays on Connected */
    TB_EQ_STR(nf_app.wifi_ssid, "Office-WiFi");
    /* the page still reads the result */
    r = sn_state();
    TB_EQ_INT(r.status, 200);
    TB_EQ_STR(nf_str(r.j, "state"), "connected");
    TB_EQ_STR(nf_str(r.j, "host"), "minibar.local");
    nf_free(&r);
    /* over USB too (the cable works during setup, but Connected is the end of it) */
    cJSON *j = nf_usb("@tb {\"cmd\": \"request\", \"id\": 7, \"method\": \"POST\", \"path\": \"/api/v1/setup/wifi\", "
                      "\"body\": {\"ssid\": \"Elsewhere\", \"password\": \"longenough\"}}");
    TB_EQ_INT(nf_num(j, "http_status"), 404);
    cJSON_Delete(j);
    TB_EQ_INT(fake_join_calls, 1);
    TB_EQ_INT(nf_app.wifi_mode, TB_WIFI_CONNECTED);
}

/* net has joined and reports "connected", but core hasn't heard yet (the event is on the bus): already refused. */
TB_TEST(setup_wifi_refused_once_net_has_joined)
{
    sn_setup_screens();
    nf_resp_t r = sn_post("{\"ssid\": \"Office-WiFi\", \"password\": \"longenough\"}");
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_INT(nf_app.wifi_mode, TB_WIFI_CONNECTING);
    fake_join = (net_join_status_t){.state = NET_JOIN_CONNECTED, .host = "minibar.local", .ip = "10.0.4.42"};
    r = sn_post("{\"ssid\": \"Elsewhere\", \"password\": \"longenough\"}");
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    TB_EQ_INT(fake_join_calls, 1);
    TB_EQ_INT(nf_app.wifi_mode, TB_WIFI_CONNECTING);    /* core moves on when it hears */
}

/* Before a join works the page may send again: after Couldn't connect (a wrong password), and while Connecting (a
 * corrected password). Only a join that worked ends it. */
TB_TEST(setup_wifi_still_taken_until_a_join_works)
{
    sn_setup_screens();
    nf_resp_t r = sn_post("{\"ssid\": \"Office-WiFi\", \"password\": \"wrongpass1\"}");
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    r = sn_post("{\"ssid\": \"Office-WiFi\", \"password\": \"rightpass1\"}");     /* while Connecting */
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    fake_join = (net_join_status_t){.state = NET_JOIN_FAILED, .error = "wrong_password", .message = "The password didn't work."};
    tb_app_wifi_failed(&nf_app, "Office-WiFi", "Wrong password", &fake_now);
    TB_EQ_INT(nf_app.wifi_mode, TB_WIFI_FAILED);
    r = sn_post("{\"ssid\": \"Office-WiFi\", \"password\": \"rightpass2\"}");     /* after Couldn't connect */
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_INT(fake_join_calls, 3);
    TB_EQ_INT(nf_app.wifi_mode, TB_WIFI_CONNECTING);
}

/* Set up again (hold, Wi-Fi, Set up) starts a new setup: net's join status is back to idle, so the page works. */
TB_TEST(setup_wifi_taken_again_after_set_up_again)
{
    sn_setup_screens();
    sn_join_works();
    nf_advance(TB_CONNECTED_MS + 100);                  /* Connected moves on */
    TB_EQ_INT(nf_app.wifi_mode, TB_WIFI_OK);
    fake_wifi = (net_wifi_info_t){.state = NET_WIFI_CONNECTED, .sta_up = true, .ssid = "Office-WiFi", .ip = "10.0.4.42",
                                  .host = "minibar.local"};
    nf_resp_t r = sn_post("{\"ssid\": \"Elsewhere\", \"password\": \"longenough\"}");    /* the 15 s linger */
    TB_EQ_INT(r.status, 404);
    nf_free(&r);
    /* hold, Wi-Fi, Set up: core shows the QR code, net_wifi_setup_begin() clears the join status */
    nf_app.wifi_mode = TB_WIFI_SETUP;
    fake_wifi = (net_wifi_info_t){.state = NET_WIFI_SETUP, .host = "minibar.local"};
    memset(&fake_join, 0, sizeof fake_join);
    r = sn_post("{\"ssid\": \"Elsewhere\", \"password\": \"longenough\"}");
    TB_EQ_INT(r.status, 202);
    nf_free(&r);
    TB_EQ_INT(fake_join_calls, 2);
}

/* ======================================================================================================== */
/* The rules net_wifi.c follows (net_util.c)                                                                */
/* ======================================================================================================== */

/* Every pair of core's Wi-Fi mode and net's: only "net still in setup, core off its setup screens" acts. */
TB_TEST(setup_catch_up_follows_core)
{
    static const tb_wifi_mode_t SCREENS[] = {TB_WIFI_SETUP, TB_WIFI_CONNECTING, TB_WIFI_CONNECTED, TB_WIFI_FAILED};
    for (size_t i = 0; i < sizeof SCREENS / sizeof SCREENS[0]; i++) {
        TB_EQ_INT(net_setup_catch_up(SCREENS[i], true), NET_SETUP_KEEP);      /* the network belongs up */
        TB_EQ_INT(net_setup_catch_up(SCREENS[i], false), NET_SETUP_KEEP);
    }
    TB_EQ_INT(net_setup_catch_up(TB_WIFI_OK, true), NET_SETUP_FINISH);        /* DONE was lost */
    TB_EQ_INT(net_setup_catch_up(TB_WIFI_OFFLINE, true), NET_SETUP_SKIP);     /* SKIP was lost */
    TB_EQ_INT(net_setup_catch_up(TB_WIFI_OK, false), NET_SETUP_KEEP);         /* the usual state after setup */
    TB_EQ_INT(net_setup_catch_up(TB_WIFI_OFFLINE, false), NET_SETUP_KEEP);
}

TB_TEST(setup_ap_stray_only_outside_setup_without_a_close_on_its_way)
{
    for (int bits = 0; bits < 16; bits++) {
        bool in_setup = bits & 1, want = bits & 2, up = bits & 4, pending = bits & 8;
        bool expect = !in_setup && (want || up) && !pending;
        if (net_setup_ap_stray(in_setup, want, up, pending) != expect)
            TB_FAIL_AT("in_setup %d want %d up %d pending %d: expected %d", in_setup, want, up, pending, expect);
    }
    TB_FALSE(net_setup_ap_stray(true, true, true, false));      /* the QR code: up on purpose */
    TB_FALSE(net_setup_ap_stray(false, true, true, true));      /* the 15 s linger after Connected */
    TB_TRUE(net_setup_ap_stray(false, true, true, false));      /* a J_STOP_AP the full queue dropped */
    TB_TRUE(net_setup_ap_stray(false, false, true, false));     /* the driver put it back (AP_START) */
    TB_FALSE(net_setup_ap_stray(false, false, false, false));   /* closed: the usual state after setup */
}

TB_TEST(ap_close_retry_schedule)
{
    bool restart = true;
    TB_EQ_INT(net_ap_close_retry_ms(1, &restart), 1000);
    TB_FALSE(restart);
    TB_EQ_INT(net_ap_close_retry_ms(2, &restart), 2000);
    TB_FALSE(restart);
    TB_EQ_INT(net_ap_close_retry_ms(3, &restart), 5000);        /* three mode changes failed: restart the radio */
    TB_TRUE(restart);
    TB_EQ_INT(net_ap_close_retry_ms(4, &restart), 10000);
    TB_TRUE(restart);
    for (int f = 5; f < 1000; f += 7) {
        TB_EQ_INT(net_ap_close_retry_ms(f, &restart), 30000);   /* and on, every 30 s, until it works */
        TB_TRUE(restart);
    }
    TB_EQ_INT(net_ap_close_retry_ms(0, NULL), 1000);            /* out of range: as the first */
    /* Within a minute of a first failure the radio has been restarted twice, so a mode change that keeps failing
     * can't keep the network up for long. */
    int32_t t = 0;
    int restarts = 0;
    for (int f = 1; t < 60000; f++) {
        t += net_ap_close_retry_ms(f, &restart);
        if (restart && t < 60000) restarts++;
    }
    TB_TRUE(restarts >= 2);
}

TB_TEST(creds_save_retry_schedule)
{
    TB_EQ_INT(net_creds_save_retry_ms(1), 5000);
    TB_EQ_INT(net_creds_save_retry_ms(2), 30000);
    TB_EQ_INT(net_creds_save_retry_ms(3), 120000);
    TB_EQ_INT(net_creds_save_retry_ms(4), 600000);
    TB_EQ_INT(net_creds_save_retry_ms(5), 1800000);
    TB_EQ_INT(net_creds_save_retry_ms(NET_CREDS_SAVE_TRIES), 0);    /* six tries in all, then it gives up */
    TB_EQ_INT(net_creds_save_retry_ms(100), 0);
    int64_t total = 0;
    for (int f = 1; net_creds_save_retry_ms(f); f++) total += net_creds_save_retry_ms(f);
    TB_EQ_INT(total, 2555000);      /* about 43 minutes: a few writes, no wear to speak of */
}

/* ======================================================================================================== */
/* core and net together: a lost TB_FX_WIFI_DONE or _SKIP                                                  */
/* ======================================================================================================== */

/* A flood of settings changes over USB (each one a TB_FX_SAVE_SETTINGS, and the light one a TB_FX_BACKLIGHT) within
 * one app-task loop, before main takes core's effects: the queue of 16 is full. */
static void sn_flood_effects(void)
{
    for (int i = 0; i < 40 && nf_app.n_fx < TB_EFFECTS_MAX; i++) {
        char line[160];
        snprintf(line, sizeof line, "@tb {\"cmd\": \"request\", \"id\": %d, \"method\": \"PATCH\", \"path\": "
                 "\"/api/v1/settings\", \"body\": {\"display\": {\"brightness\": %d}}}", 100 + i, i % 2 ? 40 : 70);
        char reply[NET_REPLY_MAX + 16];
        net_api_usb_line(line, strlen(line), false, reply, sizeof reply);  /* as the app task runs it: no drain */
        cJSON *j = cJSON_Parse(reply + 4);
        TB_EQ_INT(nf_num(j, "http_status"), 200);
        cJSON_Delete(j);
    }
    TB_EQ_INT(nf_app.n_fx, TB_EFFECTS_MAX);
}

/* core's effect queue holds 16; one more is dropped (fx_dropped). If that one is TB_FX_WIFI_DONE, core shows your
 * status while net would stay in setup with the network up: net_setup_follow() sees it and finishes setup. */
TB_TEST(lost_wifi_done_is_caught_up)
{
    sn_setup_screens();
    tb_app_wifi_connected(&nf_app, "Office-WiFi", "10.0.4.42", "minibar.local", &fake_now);
    tb_effect_t fx[TB_EFFECTS_MAX];
    tb_app_take_effects(&nf_app, fx, TB_EFFECTS_MAX);
    sn_flood_effects();
    uint16_t dropped = nf_app.fx_dropped;
    fake_now.mono += TB_CONNECTED_MS + 50;
    tb_app_tick(&nf_app, &fake_now);
    TB_EQ_INT(nf_app.wifi_mode, TB_WIFI_OK);                    /* the screen moved on */
    TB_TRUE(nf_app.fx_dropped > dropped);
    int n = tb_app_take_effects(&nf_app, fx, TB_EFFECTS_MAX);
    for (int i = 0; i < n; i++) TB_TRUE(fx[i].kind != TB_FX_WIFI_DONE);  /* DONE was the one lost */
    TB_EQ_INT(net_setup_catch_up(nf_app.wifi_mode, true), NET_SETUP_FINISH);
}

TB_TEST(lost_wifi_skip_is_caught_up)
{
    sn_setup_screens();
    tb_effect_t fx[TB_EFFECTS_MAX];
    tb_app_take_effects(&nf_app, fx, TB_EFFECTS_MAX);
    sn_flood_effects();
    /* hold, then Skip */
    fake_now.mono += 100;
    tb_app_pointer(&nf_app, true, 200, 80, TB_TILE_NONE, &fake_now);
    fake_now.mono += 700;
    tb_app_pointer_poll(&nf_app, &fake_now);
    tb_app_pointer(&nf_app, false, 200, 80, TB_TILE_NONE, &fake_now);
    TB_EQ_INT(nf_app.menu.kind, TB_MENU_SETUP);
    int skip = -1;
    for (int i = 0; i < nf_app.menu.n; i++)
        if (nf_app.menu.tiles[i].action == TB_ACT_WIFI_SKIP) skip = i;
    TB_TRUE(skip >= 0);
    fake_now.mono += 100;
    tb_app_pointer(&nf_app, true, 60, 80, (int8_t)skip, &fake_now);
    fake_now.mono += 60;
    tb_app_pointer(&nf_app, false, 60, 80, (int8_t)skip, &fake_now);
    TB_EQ_INT(nf_app.wifi_mode, TB_WIFI_OFFLINE);
    int n = tb_app_take_effects(&nf_app, fx, TB_EFFECTS_MAX);
    for (int i = 0; i < n; i++) TB_TRUE(fx[i].kind != TB_FX_WIFI_SKIP);
    TB_EQ_INT(net_setup_catch_up(nf_app.wifi_mode, true), NET_SETUP_SKIP);
}
