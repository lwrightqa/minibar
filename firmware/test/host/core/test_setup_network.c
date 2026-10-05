/*
 * test_setup_network.c: core's side of "once the bar is set up it never broadcasts its setup network unless a person
 * starts setup again" (the user's request, 2026-10-05; decisions.md, Wi-Fi). Owner: lead developer (core's folder).
 *
 * net opens MiniBar-Setup only for TB_FX_WIFI_SETUP (and at a start with nothing saved), and closes it 15 s after
 * TB_FX_WIFI_DONE. So core must send DONE exactly once whichever way the Connected screen ends, and never send SETUP
 * except from the Set up tile: not when the office Wi-Fi drops, not when it can't be rejoined, not for any other
 * control. (net_setup_follow() in net catches a DONE lost to a full effect queue; net's tests cover that.)
 */
#include "core_fixture.h"

/* A bar with nothing saved, through the phone's join, on the Connected screen. */
static bench_t *on_connected(void)
{
    bench_t *b = bench_new_opts(false, true);
    tb_app_wifi_connecting(&b->a, "Office-WiFi", &b->now);
    tb_app_wifi_connected(&b->a, "Office-WiFi", "10.0.4.42", "minibar.local", &b->now);
    bench_drain(b);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_CONNECTED);
    return b;
}

/* Every way off the Connected screen sends TB_FX_WIFI_DONE once, and nothing sends it again. */
TB_TEST(setup_done_once_whichever_way_connected_ends)
{
    for (int way = 0; way < 7; way++) {
        bench_t *b = on_connected();
        switch (way) {
        case 0: bench_run(b, TB_CONNECTED_MS + 100); break;     /* left alone */
        case 1: tap(b); break;
        case 2: swipe(b, -80); break;
        case 3: swipe(b, 80); break;
        case 4: hold(b); break;
        case 5: boot_btn(b); break;
        case 6: pwr_press(b); bench_run(b, TB_CONNECTED_MS + 100); break;     /* dark screen: the 3 s still run */
        }
        bench_run(b, 60000);
        if (b->a.wifi_mode != TB_WIFI_OK) TB_FAIL_AT("way %d: wifi_mode %d", way, (int)b->a.wifi_mode);
        if (fx_count(b, TB_FX_WIFI_DONE) != 1) TB_FAIL_AT("way %d: %d DONE", way, fx_count(b, TB_FX_WIFI_DONE));
        if (fx_count(b, TB_FX_WIFI_SETUP) != 0) TB_FAIL_AT("way %d: SETUP sent", way);
    }
}

/* After setup: the office Wi-Fi dropping, coming back, and staying away for a day, with every control used on the
 * way, never asks net for the setup network. */
TB_TEST(setup_never_reopened_by_drops_or_controls)
{
    bench_t *b = on_connected();
    bench_run(b, TB_CONNECTED_MS + 100);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);
    bench_clear_log(b);
    for (int i = 0; i < 50; i++) {
        tb_app_wifi_link(&b->a, false, NULL, NULL, &b->now);
        bench_run(b, 30000);
        tb_app_wifi_link(&b->a, true, "10.0.4.42", "minibar.local", &b->now);
        bench_run(b, 1000);
    }
    tb_app_wifi_link(&b->a, false, NULL, NULL, &b->now);
    tap(b);
    swipe(b, -80);
    swipe(b, 80);
    boot_btn(b);
    hold(b);
    tap_tile_named(b, TB_ACT_CLOSE);
    pwr_press(b);
    pwr_press(b);
    flip(b);
    flip(b);
    for (int h = 0; h < 24; h++) bench_run(b, 3600 * 1000);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);
    TB_FALSE(b->a.wifi_link_up);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SETUP), 0);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_DONE), 0);
    TB_FALSE(tb_app_on_wifi_screen(&b->a));
}

/* A start with a saved network that can't be rejoined (IT changed the password): core starts on OK, never on the
 * setup screens, and sends nothing for the setup network. */
TB_TEST(setup_not_opened_at_a_start_that_cant_rejoin)
{
    bench_t *b = bench_new();           /* a saved network: TB_WIFI_OK at tb_app_init */
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);
    tb_app_wifi_link(&b->a, false, NULL, NULL, &b->now);
    for (int h = 0; h < 24; h++) bench_run(b, 3600 * 1000);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SETUP), 0);
}

/* Only a person opens it again: hold, Wi-Fi, Set up (Change). One SETUP, and its own Connected sends one DONE. */
TB_TEST(setup_reopened_only_from_the_set_up_tile)
{
    bench_t *b = bench_new();
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SETUP), 0);
    tap_tile_named(b, TB_ACT_WIFI_SETUP);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SETUP), 1);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_SETUP);
    tb_app_wifi_connecting(&b->a, "Office-WiFi", &b->now);
    tb_app_wifi_connected(&b->a, "Office-WiFi", "10.0.4.42", "minibar.local", &b->now);
    bench_run(b, TB_CONNECTED_MS + 100);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_DONE), 1);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SETUP), 1);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);
}
