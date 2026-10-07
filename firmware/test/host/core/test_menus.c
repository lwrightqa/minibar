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
    TB_EQ_STR(t[0].label, "Display");
    TB_EQ_STR(t[0].value, "70%");
    TB_EQ_STR(t[0].foot, "light, time, chime");
    TB_EQ_INT(t[0].action, TB_ACT_DISPLAY);
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
    tap_tile_named(b, TB_ACT_DISPLAY);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_DISPLAY);
    tap_tile_named(b, TB_ACT_BRIGHT);
    TB_EQ_INT(b->a.set.display.brightness, 100);
    TB_EQ_INT(fx_last(b, TB_FX_BACKLIGHT), 100);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_DISPLAY);         /* the Display menu stays, showing the new level */
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
    TB_EQ_STR(b->a.toast, "Light 55%");
    hold(b);
    tap_tile_named(b, TB_ACT_DISPLAY);
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
    TB_EQ_INT(b->a.menu.n, 4);
    TB_EQ_STR(b->a.menu.tiles[0].label, "Network");
    TB_EQ_STR(b->a.menu.tiles[0].value, "Office-WiFi");
    TB_EQ_STR(b->a.menu.tiles[0].foot, "MiniBar 2A1C\nminibar.local \xC2\xB7 10.0.4.42");
    TB_EQ_INT(b->a.menu.tiles[0].style, TB_TILE_INFO);
    TB_EQ_STR(b->a.menu.tiles[2].label, "Change");
    TB_EQ_STR(b->a.menu.tiles[2].value, "Set up");
    TB_EQ_STR(b->a.menu.tiles[2].foot, "show the\nQR code");
    TB_EQ_STR(b->a.menu.tiles[3].value, "Back");
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
    TB_EQ_STR(b->a.menu.tiles[0].foot, "MiniBar 2A1C\nnot connected");
    TB_EQ_STR(b->a.menu.tiles[2].label, "Set up");
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

/* showWifiMenu() (pairing round, proposed): Network (two columns, read-only), Devices, Set up / Change, Back, on the
 * quick menu's five columns. Network's value is the office network; its foot is the bar's name and its address. */
TB_TEST(menu_wifi_layout)
{
    bench_t *b = bench_new();
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_TRUE(b->a.menu.five);
    TB_EQ_INT(b->a.menu.n, 4);
    const tb_tile_t *t = b->a.menu.tiles;
    TB_EQ_STR(t[0].label, "Network");
    TB_EQ_STR(t[0].value, "Office-WiFi");
    TB_EQ_STR(t[0].foot, "MiniBar 2A1C\nminibar.local \xC2\xB7 10.0.4.42");
    TB_TRUE(t[0].wide && t[0].foot_lines);
    TB_EQ_INT(t[0].action, TB_ACT_NONE);
    TB_EQ_STR(t[1].label, "Devices");
    TB_EQ_STR(t[1].value, "None");                       /* nothing paired: the tile stays, read-only */
    TB_EQ_STR(t[1].foot, "pair at\nminibar.local");          /* where to pair; ui falls back when it doesn't fit */
    TB_EQ_INT(t[1].n_foot_alt, 2);
    TB_EQ_STR(t[1].foot_alt[0], "pair at\n10.0.4.42");
    TB_EQ_STR(t[1].foot_alt[1], "pair at its\nIP address");
    TB_EQ_INT(t[1].action, TB_ACT_NONE);
    TB_EQ_INT(t[1].style, TB_TILE_INFO);
    TB_EQ_STR(t[2].label, "Change");
    TB_EQ_STR(t[2].value, "Set up");
    TB_EQ_STR(t[2].foot, "show the\nQR code");
    TB_EQ_INT(t[3].action, TB_ACT_CLOSE);
    tap_tile(b, 1);                                      /* a read-only tile closes the menu, as Network does */
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    /* paired: a count, up to Full at 10 */
    tb_app_set_paired(&b->a, 3, "iPhone, Desk script, Mac");
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    TB_EQ_STR(tile_with(b, TB_ACT_DEVICES)->value, "3 paired");
    TB_EQ_STR(tile_with(b, TB_ACT_DEVICES)->foot, "tap to\nforget all");
    TB_EQ_INT(tile_index(b, TB_ACT_DEVICES), 1);
    tb_app_set_paired(&b->a, 10, "a, b");
    bench_run(b, 50);
    TB_EQ_STR(tile_with(b, TB_ACT_DEVICES)->value, "Full");
}

TB_TEST(menu_wifi_offline_and_link_down)
{
    bench_t *b = bench_new_opts(false, true);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI_SKIP);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    const tb_tile_t *t = b->a.menu.tiles;
    TB_EQ_STR(t[0].value, "None");
    TB_EQ_STR(t[0].foot, "MiniBar 2A1C\nnot connected");
    TB_EQ_STR(t[1].foot, "set up Wi-Fi\nto pair");
    TB_EQ_STR(t[2].label, "Set up");
    /* the link dropped after setup: no address to show */
    b = bench_new();
    tb_app_wifi_link(&b->a, false, NULL, NULL, &b->now);
    hold(b);
    TB_EQ_STR(tile_with(b, TB_ACT_WIFI)->foot, "reconnecting");
    tap_tile_named(b, TB_ACT_WIFI);
    TB_EQ_STR(b->a.menu.tiles[0].value, "Office-WiFi");
    TB_EQ_STR(b->a.menu.tiles[0].foot, "MiniBar 2A1C\nnot connected");
}

/* showForgetMenu(): Paired (names, most recently used first), Forget all (danger, 600 ms guard), Keep. */
TB_TEST(menu_forget_all)
{
    bench_t *b = bench_new();
    tb_app_set_paired(&b->a, 3, "iPhone, Desk script, Mac");
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    tap_tile_named(b, TB_ACT_DEVICES);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_FORGET);
    TB_EQ_INT(b->a.menu.n, 3);
    const tb_tile_t *t = b->a.menu.tiles;
    TB_EQ_STR(t[0].label, "Paired");
    TB_EQ_STR(t[0].value, "3 devices");
    TB_EQ_STR(t[0].foot, "iPhone, Desk script, Mac");
    TB_TRUE(t[0].foot_clamp2);
    TB_EQ_INT(t[0].style, TB_TILE_INFO);
    TB_EQ_STR(t[1].label, "Tap again");
    TB_EQ_STR(t[1].value, "Forget all");
    TB_EQ_STR(t[1].foot, "each needs a new code");
    TB_EQ_INT(t[1].style, TB_TILE_DANGER);
    TB_EQ_STR(t[2].label, "Cancel");
    TB_EQ_STR(t[2].value, "Keep");
    TB_EQ_STR(t[2].foot, "back to Wi-Fi");
    TB_EQ_INT(t[2].style, TB_TILE_DONE);
    /* a quick second tap is ignored */
    tap_tile_named(b, TB_ACT_FORGET_ALL);
    TB_EQ_INT(fx_count(b, TB_FX_FORGET_DEVICES), 0);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_FORGET);
    /* Keep goes back to Wi-Fi and forgets nothing */
    tap_tile_named(b, TB_ACT_KEEP_DEVICES);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_WIFI);
    TB_EQ_INT(fx_count(b, TB_FX_FORGET_DEVICES), 0);
    /* a deliberate second tap forgets them all */
    tap_tile_named(b, TB_ACT_DEVICES);
    bench_run(b, 700);
    tap_tile_named(b, TB_ACT_FORGET_ALL);
    TB_EQ_INT(fx_count(b, TB_FX_FORGET_DEVICES), 1);
    TB_EQ_STR(b->a.toast, "Forgot 3 devices");
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    /* the 8 s close forgets nothing */
    bench_clear_log(b);
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    tap_tile_named(b, TB_ACT_DEVICES);
    bench_run(b, 8100);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
    TB_EQ_INT(fx_count(b, TB_FX_FORGET_DEVICES), 0);
    /* one device: singular; the last one removed meanwhile turns the confirmation back into the Wi-Fi menu */
    tb_app_set_paired(&b->a, 1, "Mac");
    hold(b);
    tap_tile_named(b, TB_ACT_WIFI);
    tap_tile_named(b, TB_ACT_DEVICES);
    TB_EQ_STR(b->a.menu.tiles[0].value, "1 device");
    tb_app_set_paired(&b->a, 0, NULL);
    bench_run(b, 50);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_WIFI);
    TB_EQ_STR(b->a.menu.tiles[1].value, "None");
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

TB_TEST(menu_display_time_tile)
{
    bench_t *b = bench_new();
    hold(b);
    tap_tile_named(b, TB_ACT_DISPLAY);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_DISPLAY);
    TB_EQ_INT(b->a.menu.n, 4);
    const tb_tile_t *t = b->a.menu.tiles;
    TB_EQ_STR(t[0].label, "Light");
    TB_EQ_STR(t[1].label, "Chime");                     /* beside where Tap sound will sit (decisions.md) */
    TB_EQ_STR(t[2].label, "Time");
    TB_EQ_STR(t[2].value, "12-hr");
    TB_EQ_STR(t[2].foot, "tap to switch");
    TB_EQ_STR(t[3].label, "Display");
    TB_EQ_STR(t[3].value, "Back");
    TB_EQ_INT(t[3].style, TB_TILE_DONE);
    TB_FALSE(b->a.set.more.time_24h);
    /* a tap switches at once, the menu stays open, a toast says the full name, and it's saved */
    tap_tile_named(b, TB_ACT_TIME_FMT);
    TB_TRUE(b->a.set.more.time_24h);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_DISPLAY);
    TB_EQ_STR(tile_with(b, TB_ACT_TIME_FMT)->value, "24-hr");
    TB_EQ_STR(b->a.toast, "Time format \xC2\xB7 24-hour");
    TB_TRUE(fx_count(b, TB_FX_SAVE_SETTINGS) >= 1);
    tap_tile_named(b, TB_ACT_TIME_FMT);
    TB_FALSE(b->a.set.more.time_24h);
    TB_EQ_STR(tile_with(b, TB_ACT_TIME_FMT)->value, "12-hr");
    TB_EQ_STR(b->a.toast, "Time format \xC2\xB7 12-hour");
    /* Back is the way out: the quick menu, then Done closes it; the 8 s close also works from the Display menu */
    tap_tile_named(b, TB_ACT_QUICK_MENU);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_QUICK);
    tap_tile_named(b, TB_ACT_DISPLAY);
    bench_run(b, 8100);
    TB_EQ_INT(b->a.menu.kind, TB_MENU_NONE);
}

TB_TEST(time_format_changes_every_clock_text_rev)
{
    bench_t *b = bench_new();
    uint32_t rev = b->a.rev;
    hold(b);
    tap_tile_named(b, TB_ACT_DISPLAY);
    tap_tile_named(b, TB_ACT_TIME_FMT);
    TB_TRUE(b->a.rev != rev);       /* what the screen shows changed, so ui redraws and the API's rev moves */
    /* the Calendar tile's "synced at 9:05" foot follows the setting */
    tb_app_set_calendar(&b->a, true, false, b->now.wall - 2 * 3600, &b->now);
    tap_tile_named(b, TB_ACT_QUICK_MENU);
    TB_EQ_STR(tile_with(b, TB_ACT_SYNC)->foot, "synced at 08:00");
    tap_tile_named(b, TB_ACT_DISPLAY);
    tap_tile_named(b, TB_ACT_TIME_FMT);
    tap_tile_named(b, TB_ACT_QUICK_MENU);
    TB_EQ_STR(tile_with(b, TB_ACT_SYNC)->foot, "synced at 8:00 AM");
}
