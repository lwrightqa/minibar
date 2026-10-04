/* The Wi-Fi setup screens (wifiTap, startSetup, skipWifi, the Connected screen) and the pairing screen (api.md 4.8). */
#include <string.h>

#include "core_fixture.h"

TB_TEST(wifi_first_start_shows_the_qr_code)
{
    bench_t *b = bench_new_opts(false, true);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_SETUP);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_SETUP);
    TB_EQ_INT(tb_app_color_key(&b->a, &b->now), TB_KEY_SETUP);
    TB_FALSE(tb_app_screen_free(&b->a));
    tap(b);
    TB_EQ_STR(b->a.toast, "Scan the code with your phone");
    bench_run(b, TB_TOAST_MS + 100);
    boot_btn(b);
    TB_EQ_STR(b->a.toast, "Scan the code with your phone");
    swipe(b, -80);                                      /* a swipe is a tap here */
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_SETUP);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
}

TB_TEST(wifi_connect_fail_retry_connect)
{
    bench_t *b = bench_new_opts(false, true);
    tb_app_wifi_connecting(&b->a, "Office-Corp", &b->now);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_CONNECTING);
    tap(b);
    TB_EQ_STR(b->a.toast, "Still connecting");
    tb_app_wifi_failed(&b->a, "Office-Corp", "Wrong password", &b->now);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_FAILED);
    TB_EQ_STR(b->a.wifi_error, "Wrong password");
    tap(b);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_SETUP);            /* back to the QR code */
    tb_app_wifi_connecting(&b->a, "Office-WiFi", &b->now);
    tb_app_wifi_connected(&b->a, "Office-WiFi", "10.0.4.42", "tinybar.local", &b->now);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_CONNECTED);
    TB_TRUE(b->a.wifi_link_up);
    bench_run(b, 2900);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_CONNECTED);
    bench_run(b, 200);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);               /* moves on by itself after 3 s */
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_DONE), 1);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_OWN);
}

TB_TEST(wifi_connected_tap_or_hold_finishes_setup)
{
    bench_t *b = bench_new_opts(false, true);
    tb_app_wifi_connected(&b->a, "Office-WiFi", "10.0.4.42", NULL, &b->now);
    tap(b);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_DONE), 1);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);                     /* the tap doesn't also change the status */
    b = bench_new_opts(false, true);
    tb_app_wifi_connected(&b->a, "Office-WiFi", "10.0.4.42", NULL, &b->now);
    hold(b);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);               /* so Skip can't undo it */
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_DONE), 1);
    b = bench_new_opts(false, true);
    tb_app_wifi_connected(&b->a, "Office-WiFi", "10.0.4.42", NULL, &b->now);
    boot_btn(b);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);
}

TB_TEST(wifi_setup_menu_start_over)
{
    bench_t *b = bench_new_opts(false, true);
    tb_app_wifi_failed(&b->a, "Printer-Direct", "No signal", &b->now);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI_SETUP);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_SETUP);
    TB_EQ_STR(b->a.wifi_error, "");
}

TB_TEST(wifi_link_drop_keeps_mode)
{
    bench_t *b = bench_new();
    tb_app_wifi_link(&b->a, false, NULL, NULL, &b->now);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OK);
    TB_FALSE(b->a.wifi_link_up);
    TB_EQ_STR(b->a.wifi_ip, "");
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_EQ_STR(b->a.menu.tiles[0].foot, "Office-WiFi");
    tb_app_wifi_link(&b->a, true, "10.0.4.50", "tinybar-2.local", &b->now);
    TB_EQ_STR(b->a.wifi_host, "tinybar-2.local");
    TB_EQ_STR(b->a.menu.tiles[0].foot, "Office-WiFi \xC2\xB7 10.0.4.50");
}

/* ---- pairing ---- */

static bench_t *pairing_up(void)
{
    bench_t *b = bench_new();
    tb_app_pairing_show(&b->a, "482913", "Mac", b->now.mono + 120000, &b->now);
    bench_drain(b);
    TB_TRUE(tb_app_pairing_visible(&b->a));
    return b;
}

TB_TEST(pairing_wakes_and_replaces_a_menu)
{
    bench_t *b = bench_new();
    pwr_press(b);
    tb_app_pairing_show(&b->a, "482913", "iPhone", b->now.mono + 120000, &b->now);
    bench_drain(b);
    TB_FALSE(b->a.off);
    TB_TRUE(tb_app_pairing_visible(&b->a));
    TB_EQ_STR(b->a.pairing.who, "iPhone");
    b = bench_new();
    hold(b);
    tb_app_pairing_show(&b->a, "482913", NULL, b->now.mono + 120000, &b->now);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    TB_EQ_STR(b->a.pairing.who, "Mac");
    TB_FALSE(tb_app_screen_free(&b->a));                 /* other toasts wait */
}

TB_TEST(pairing_canceled_by_tap_swipe_hold_boot_pwr)
{
    void (*inputs[])(bench_t *) = {tap, hold, boot_btn, pwr_press};
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
        bench_t *b = pairing_up();
        inputs[i](b);
        TB_FALSE(b->a.pairing.active);
        TB_EQ_INT(fx_count(b, TB_FX_PAIRING_CANCELED), 1);
        TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);         /* a hold cancels, it doesn't open the menu */
        TB_EQ_INT(b->a.idx, TB_ST_BUSY);                 /* nor change the status */
        if (inputs[i] == pwr_press) TB_TRUE(b->a.off);   /* PWR also turns the screen off as usual */
        else TB_EQ_STR(b->a.toast, "Pairing canceled");
    }
    bench_t *b = pairing_up();
    swipe(b, -80);
    TB_FALSE(b->a.pairing.active);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    /* net's later "canceled" for the same pairing says nothing twice */
    bench_run(b, TB_TOAST_MS + 100);
    tb_app_pairing_end(&b->a, TB_PAIR_END_CANCELED, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "");
}

TB_TEST(pairing_endings)
{
    bench_t *b = pairing_up();
    tb_app_pairing_end(&b->a, TB_PAIR_END_PAIRED, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Paired \xC2\xB7 Mac");
    TB_FALSE(tb_app_pairing_visible(&b->a));
    b = pairing_up();
    tb_app_pairing_end(&b->a, TB_PAIR_END_TIMEOUT, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Pairing timed out");
    b = pairing_up();
    tb_app_pairing_end(&b->a, TB_PAIR_END_WRONG_CODE, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Pairing canceled \xC2\xB7 wrong code");
    b = bench_new();                                     /* USB pairing has no code screen, but confirms */
    tb_app_pairing_end(&b->a, TB_PAIR_END_PAIRED_USB, "Mac", &b->now);
    TB_EQ_STR(b->a.toast, "Paired \xC2\xB7 Mac \xC2\xB7 over USB");
}

TB_TEST(pairing_waits_for_the_power_screens_and_skips_setup)
{
    bench_t *b = bench_new();
    tb_app_button(&b->a, TB_BTN_PWR_DOWN, &b->now);
    bench_run(b, 500);
    TB_EQ_INT(b->a.hold, TB_HOLD_KEEP_HOLDING);
    tb_app_pairing_show(&b->a, "111222", "Mac", b->now.mono + 120000, &b->now);
    TB_FALSE(tb_app_pairing_visible(&b->a));
    bench_run(b, 500);
    tb_app_button(&b->a, TB_BTN_PWR_UP, &b->now);       /* released early */
    TB_TRUE(tb_app_pairing_visible(&b->a));
    b = bench_new_opts(false, true);
    tb_app_pairing_show(&b->a, "111222", "Mac", b->now.mono + 120000, &b->now);
    TB_FALSE(tb_app_pairing_visible(&b->a));            /* never on the Wi-Fi setup screens */
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_SETUP);
}
