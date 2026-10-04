/* The hold menus: tiles and copy, every Calendar tile state, Light, Wi-Fi, Power, Sync, Devices, 8 s, taps only. */
#include <string.h>

#include "core_fixture.h"

TB_TEST(menu_quick_tiles)
{
    bench_t *b = bench_new();
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);
    TB_EQ_INT(b->a.menu.n, 5);
    const tb_tile_t *t = b->a.menu.tiles;
    TB_EQ_STR(t[0].label, "Light");
    TB_EQ_STR(t[0].value, "70%");
    TB_EQ_STR(t[0].foot, "tap to change");
    TB_EQ_STR(t[1].label, "Calendar");
    TB_EQ_STR(t[1].value, "Off");
    TB_EQ_STR(t[1].foot, "add it on the Remote");
    TB_EQ_STR(t[2].label, "Wi-Fi");
    TB_EQ_STR(t[2].value, "On");
    TB_EQ_STR(t[2].foot, "Office-WiFi");
    TB_EQ_STR(t[3].label, "Power");
    TB_EQ_STR(t[3].value, "USB");
    TB_EQ_STR(t[3].foot, "off or restart");
    TB_EQ_STR(t[4].label, "Close");
    TB_EQ_STR(t[4].value, "Done");
    TB_EQ_STR(t[4].foot, "or wait 8 s");
    TB_EQ_INT(t[4].style, TB_TILE_DONE);
    tap_tile(b, 4);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
}

TB_TEST(menu_calendar_tile_states)
{
    bench_t *b = bench_new();
    cal_save(b, NULL, 0);
    bench_run(b, 120000);
    hold(b);
    const tb_tile_t *t = tile_with(b, TB_ACT_SYNC);
    TB_EQ_STR(t->value, "Sync");
    TB_EQ_STR(t->foot, "synced 2m ago");
    tb_app_set_calendar(&b->a, true, true, b->a.cal_last_sync, &b->now);
    TB_EQ_STR(tile_with(b, TB_ACT_SYNC)->value, "Sync\xE2\x80\xA6");
    TB_EQ_STR(tile_with(b, TB_ACT_SYNC)->foot, "syncing now");
    tb_app_set_calendar(&b->a, true, false, T0_WALL - 3600, &b->now);    /* 9:00 AM */
    TB_EQ_STR(tile_with(b, TB_ACT_SYNC)->foot, "synced at 9:00 AM");
    tb_app_set_calendar(&b->a, true, false, b->now.wall - 20, &b->now);
    TB_EQ_STR(tile_with(b, TB_ACT_SYNC)->foot, "synced just now");
    b->a.wifi_mode = TB_WIFI_OFFLINE;
    tb_app_tick(&b->a, &b->now);
    TB_EQ_STR(tile_with(b, TB_ACT_SYNC)->value, "Off");
    TB_EQ_STR(tile_with(b, TB_ACT_SYNC)->foot, "needs Wi-Fi");
    TB_EQ_STR(tile_with(b, TB_ACT_WIFI)->value, "Off");
    TB_EQ_STR(tile_with(b, TB_ACT_WIFI)->foot, "tap to set up");
}

TB_TEST(menu_sync_now)
{
    bench_t *b = bench_new();
    hold(b);
    tap_tile_named(b, TB_ACT_SYNC);
    TB_EQ_STR(b->a.toast, "Add your calendar on the Remote");
    TB_EQ_INT(fx_count(b, TB_FX_CAL_SYNC), 0);
    cal_save(b, NULL, 0);
    hold(b);
    tap_tile_named(b, TB_ACT_SYNC);
    TB_EQ_INT(fx_count(b, TB_FX_CAL_SYNC), 1);
    TB_TRUE(b->a.cal_checking);
    hold(b);
    TB_EQ_STR(tile_with(b, TB_ACT_SYNC)->value, "Sync\xE2\x80\xA6");
    tap_tile_named(b, TB_ACT_SYNC);
    TB_EQ_STR(b->a.toast, "Still syncing");
    tb_app_calendar_event(&b->a, TB_CALEV_SYNCED, &b->now);
    TB_EQ_STR(b->a.toast, "Calendar synced");
    TB_FALSE(b->a.cal_checking);
    hold(b);
    tap_tile_named(b, TB_ACT_SYNC);
    tb_app_calendar_event(&b->a, TB_CALEV_SYNC_FAILED, &b->now);
    TB_EQ_STR(b->a.toast, "Couldn't sync the calendar");
    /* no Wi-Fi (skipped, or the link dropped) */
    tb_app_wifi_link(&b->a, false, NULL, NULL, &b->now);
    hold(b);
    tap_tile_named(b, TB_ACT_SYNC);
    TB_EQ_STR(b->a.toast, "No Wi-Fi, can't sync");
}

TB_TEST(menu_light_steps)
{
    bench_t *b = bench_new();
    hold(b);
    tap_tile_named(b, TB_ACT_BRIGHT);
    TB_EQ_INT(b->a.set.display.brightness, 100);
    TB_EQ_INT(fx_last(b, TB_FX_BACKLIGHT), 100);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);           /* the menu stays, showing the new level */
    TB_EQ_STR(tile_with(b, TB_ACT_BRIGHT)->value, "100%");
    tap_tile_named(b, TB_ACT_BRIGHT);
    TB_EQ_INT(b->a.set.display.brightness, 40);
    tap_tile_named(b, TB_ACT_BRIGHT);
    TB_EQ_INT(b->a.set.display.brightness, 70);
    TB_TRUE(fx_count(b, TB_FX_SAVE_SETTINGS) >= 1);
    /* from a level set on the Remote, the next one above */
    tb_settings_patch_t p = {0};
    p.has_brightness = true;
    p.v.display.brightness = 55;
    tb_app_remote_settings(&b->a, &p, NULL, &b->now);
    TB_EQ_STR(b->a.toast, "Brightness 55%");
    hold(b);
    tap_tile_named(b, TB_ACT_BRIGHT);
    TB_EQ_INT(b->a.set.display.brightness, 70);
}

TB_TEST(menu_closes_after_8s_and_rearms)
{
    bench_t *b = bench_new();
    hold(b);
    bench_run(b, 7000);
    tap_tile_named(b, TB_ACT_WIFI);                     /* a submenu re-arms the 8 s */
    TB_EQ_INT(b->a.menu.kind, TB_MENU_WIFI);
    bench_run(b, 7000);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_WIFI);
    bench_run(b, 1100);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
}

TB_TEST(menu_only_taps_act)
{
    bench_t *b = bench_new();
    hold(b);
    swipe(b, -100);                                     /* ignored: can't run whichever tile it lifts on */
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
    tap(b);                                             /* a tap off the tiles closes it */
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    TB_EQ_INT(b->a.idx, TB_ST_BUSY);
}

TB_TEST(menu_hides_the_toast)
{
    bench_t *b = bench_new();
    tb_app_notify(&b->a, "Removed Mac", &b->now);
    TB_EQ_STR(b->a.toast, "Removed Mac");
    hold(b);
    TB_EQ_STR(b->a.toast, "");
}

TB_TEST(menu_wifi_and_power_submenus)
{
    bench_t *b = bench_new();
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_EQ_INT(b->a.menu.n, 3);
    TB_EQ_STR(b->a.menu.tiles[0].label, "Network");
    TB_EQ_STR(b->a.menu.tiles[0].value, "On");
    TB_EQ_STR(b->a.menu.tiles[0].foot, "Office-WiFi \xC2\xB7 10.0.4.42");
    TB_EQ_INT(b->a.menu.tiles[0].style, TB_TILE_INFO);
    TB_EQ_STR(b->a.menu.tiles[1].label, "Change");
    TB_EQ_STR(b->a.menu.tiles[1].value, "Set up");
    TB_EQ_STR(b->a.menu.tiles[1].foot, "show the QR code");
    TB_EQ_STR(b->a.menu.tiles[2].value, "Back");
    tap_tile(b, 0);                                     /* the read-only tile: the menu closes */
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    tap_tile_named(b, TB_ACT_WIFI_SETUP);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_SETUP);
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SETUP), 1);
    TB_EQ_INT(tb_app_showing(&b->a, &b->now), TB_SHOWING_SETUP);
    /* offline: "Set up" */
    b = bench_new();
    b->a.wifi_mode = TB_WIFI_OFFLINE;
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_EQ_STR(b->a.menu.tiles[0].value, "None");
    TB_EQ_STR(b->a.menu.tiles[0].foot, "not connected");
    TB_EQ_STR(b->a.menu.tiles[1].label, "Set up");
    tap_tile_named(b, TB_ACT_CLOSE);
    /* power */
    hold(b);
    tap_tile_named(b, TB_ACT_POWER);
    TB_EQ_STR(b->a.menu.tiles[0].foot, "back in a few seconds");
    TB_EQ_STR(b->a.menu.tiles[1].label, "Power off");
    TB_EQ_STR(b->a.menu.tiles[1].value, "Off");
    TB_EQ_STR(b->a.menu.tiles[1].foot, "press PWR to turn on");
    tap_tile_named(b, TB_ACT_POWER_OFF);
    TB_EQ_INT(b->a.hold, TB_HOLD_POWERING_OFF);
    bench_run(b, TB_POWERING_OFF_MS + 50);
    TB_EQ_INT(fx_count(b, TB_FX_POWER_OFF), 1);
}

TB_TEST(menu_devices_tile)
{
    bench_t *b = bench_new();
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_TRUE(tile_with(b, TB_ACT_DEVICES) == NULL);       /* nothing paired: no tile */
    tap_tile_named(b, TB_ACT_CLOSE);
    tb_app_set_paired_count(&b->a, 3);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_EQ_INT(b->a.menu.n, 4);
    TB_EQ_STR(tile_with(b, TB_ACT_DEVICES)->value, "3 paired");
    TB_EQ_STR(tile_with(b, TB_ACT_DEVICES)->foot, "tap to forget all");
    tap_tile_named(b, TB_ACT_DEVICES);
    TB_EQ_INT(fx_count(b, TB_FX_FORGET_DEVICES), 0);
    TB_EQ_STR(tile_with(b, TB_ACT_DEVICES)->value, "Forget all");
    TB_EQ_STR(tile_with(b, TB_ACT_DEVICES)->foot, "3 devices \xC2\xB7 tap again");
    tap_tile_named(b, TB_ACT_DEVICES);
    TB_EQ_INT(fx_count(b, TB_FX_FORGET_DEVICES), 1);
    TB_EQ_STR(b->a.toast, "Forgot 3 devices");
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    /* the confirmation doesn't survive closing the menu */
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    tap_tile_named(b, TB_ACT_DEVICES);
    tap_tile_named(b, TB_ACT_CLOSE);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_EQ_STR(tile_with(b, TB_ACT_DEVICES)->value, "3 paired");
}

TB_TEST(menu_setup_screens)
{
    bench_t *b = bench_new_opts(false, true);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_SETUP);
    hold(b);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_SETUP);
    TB_EQ_STR(b->a.menu.tiles[0].value, "Skip");
    TB_EQ_STR(b->a.menu.tiles[0].foot, "use without Wi-Fi");
    TB_EQ_STR(b->a.menu.tiles[1].label, "Start over");
    TB_EQ_STR(b->a.menu.tiles[1].value, "QR code");
    TB_EQ_STR(b->a.menu.tiles[1].foot, "show it again");
    TB_EQ_STR(b->a.menu.tiles[2].value, "Done");
    tap_tile_named(b, TB_ACT_WIFI_SKIP);
    TB_EQ_INT(b->a.wifi_mode, TB_WIFI_OFFLINE);
    TB_EQ_STR(b->a.toast, "Offline \xC2\xB7 statuses still work");
    TB_EQ_INT(fx_count(b, TB_FX_WIFI_SKIP), 1);
}
