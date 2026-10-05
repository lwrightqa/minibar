/*
 * tb_menu.c: the hold menus' tiles. Owner: core builder.
 *
 * A port of the mock-up's showMenu() (quick and timer menus, and the setup screens' menu), calTile(),
 * showTimerSettings(), showWifiMenu(), showForgetMenu() and showPowerMenu(). Which menu opens is decided by the caller
 * (tb_app.c's show_menu, the mock-up's showMenu); this file says what each tile reads. Labels are written as in the
 * mock-up's <b> ("Light"); the ui draws them in capitals. A foot's "\n" is the mock-up's <br>.
 */
#include <stdio.h>
#include <string.h>

#include "tb_fmt.h"
#include "tb_internal.h"
#include "tb_menu.h"

static tb_tile_t *add(tb_menu_t *m, tb_action_t act, tb_tile_style_t style, const char *label, const char *value,
                      const char *foot)
{
    if (m->n >= TB_MENU_MAX_TILES) return &m->tiles[TB_MENU_MAX_TILES - 1];   /* never happens: 5 is the most */
    tb_tile_t *t = &m->tiles[m->n++];
    memset(t, 0, sizeof(*t));
    t->action = act;
    t->style = style;
    tb_strlcpy(t->label, label, sizeof(t->label));
    tb_strlcpy(t->value, value, sizeof(t->value));
    tb_strlcpy(t->foot, foot, sizeof(t->foot));
    return t;
}

static const char *kind_word(tb_auto_t k)
{
    return k == TB_AUTO_CALL ? "call" : "meeting";
}

static void show_again_tile(tb_menu_t *m, tb_auto_t k)
{
    char foot[24];
    snprintf(foot, sizeof foot, "the %s", kind_word(k));
    add(m, TB_ACT_SHOW_AGAIN, TB_TILE_NORMAL, "Set aside", "Show again", foot)->value_two_lines = true;
}

static void done_tile(tb_menu_t *m)
{
    add(m, TB_ACT_CLOSE, TB_TILE_DONE, "Close", "Done", "or wait 8 s");
}

/* The office Wi-Fi was joined but the link is down now (the bar reconnects by itself). */
static bool link_down(const tb_app_t *a)
{
    return a->wifi_mode == TB_WIFI_OK && !a->wifi_link_up;
}

/* calTile(): a state for every situation, so it's never a dead end; Show again while a call or meeting is aside. */
static void cal_tile(tb_app_t *a, const tb_clock_t *now)
{
    tb_menu_t *m = &a->menu;
    tb_auto_t k = tb_app_aside_kind(a, now);
    if (k != TB_AUTO_NONE) {
        show_again_tile(m, k);
        return;
    }
    if (a->wifi_mode == TB_WIFI_OFFLINE) {
        add(m, TB_ACT_SYNC, TB_TILE_NORMAL, "Calendar", "Off", "needs Wi-Fi");
        return;
    }
    if (!a->cal_saved) {
        add(m, TB_ACT_SYNC, TB_TILE_NORMAL, "Calendar", "Off", "add it on the Remote");
        return;
    }
    if (a->cal_checking) {
        add(m, TB_ACT_SYNC, TB_TILE_NORMAL, "Calendar", "Sync\xE2\x80\xA6", "syncing now");
        return;
    }
    char foot[48], ago[32];
    if (!a->cal_last_sync) snprintf(foot, sizeof foot, "not synced yet");          /* Firmware: no sync time saved */
    else if (!now->valid) snprintf(foot, sizeof foot, "tap to sync");              /* Firmware: clock unknown */
    else snprintf(foot, sizeof foot, "synced %s", tb_fmt_ago(ago, sizeof ago, a->cal_last_sync, now->wall, true));
    /* Firmware: with the link down it can't sync (Sync now says "No Wi-Fi, can't sync"), but it keeps following its
     * last copy, so the tile says it's offline and when it last synced (decisions.md, offline). */
    add(m, TB_ACT_SYNC, TB_TILE_NORMAL, "Calendar", link_down(a) ? "Offline" : "Sync", foot);
}

static void quick_menu(tb_app_t *a, const tb_clock_t *now)
{
    tb_menu_t *m = &a->menu;
    char v[24];
    snprintf(v, sizeof v, "%d%%", a->set.display.brightness);
    add(m, TB_ACT_BRIGHT, TB_TILE_NORMAL, "Light", v, "tap to change");
    cal_tile(a, now);
    bool off = a->wifi_mode == TB_WIFI_OFFLINE;
    /* Firmware: a dropped link says so instead of the network's name. */
    add(m, TB_ACT_WIFI, TB_TILE_NORMAL, "Wi-Fi", off ? "Off" : "On", off ? "tap to set up" : link_down(a) ? "reconnecting" : a->wifi_ssid);
    /* Bold Signal's Power tile says USB: the bar runs on USB with no battery (decisions.md, Product). */
    add(m, TB_ACT_POWER, TB_TILE_NORMAL, "Power", "USB", "off or restart");
    done_tile(m);
}

static void timer_menu(tb_app_t *a, const tb_clock_t *now)
{
    tb_menu_t *m = &a->menu;
    const tb_pomo_t *p = &a->pomo;
    char foot[32];
    const char *next = p->phase == TB_PH_FOCUS ? (p->round % a->set.pomodoro.long_every == 0 ? "long break" : "short break")
                                               : "focus";
    snprintf(foot, sizeof foot, "to %s", next);
    add(m, TB_ACT_SKIP, TB_TILE_NORMAL, "Skip", "Next", foot);
    if (tb_pomo_mid_phase(p, &a->set))
        add(m, TB_ACT_ADD, TB_TILE_NORMAL, "Add time", "+5", p->phase == TB_PH_FOCUS ? "min to this session" : "min to this break");
    add(m, TB_ACT_STOP, TB_TILE_NORMAL, "Stop", "End", "keeps tomatoes");
    /* While a call or meeting is set aside, Show again takes the Settings tile's place, so the menu stays at five. */
    tb_auto_t k = tb_app_aside_kind(a, now);
    if (k != TB_AUTO_NONE) show_again_tile(m, k);
    else add(m, TB_ACT_TIMER_SETTINGS, TB_TILE_NORMAL, "Settings", "More", "auto-start, ticking");
    done_tile(m);
}

static void timer_settings(tb_app_t *a)
{
    tb_menu_t *m = &a->menu;
    add(m, TB_ACT_AUTO, TB_TILE_NORMAL, "Auto-start", a->set.pomodoro.auto_start ? "On" : "Off", "next phase");
    const char *tick = !a->set.pomodoro.ticking ? "Off" : a->set.pomodoro.tick_volume == TB_TICK_MEDIUM ? "Medium" : "Soft";
    add(m, TB_ACT_TICK, TB_TILE_NORMAL, "Ticking", tick, "during focus");
    add(m, TB_ACT_TIMER_MENU, TB_TILE_DONE, "Settings", "Back", "to the timer menu");
}

/* With nothing paired, the Devices tile says where to pair: this bar's real address, where the Remote is. "pair at"
 * over the host ("minibar.local" is 80 px of the tile's 86.5 in Barlow 500 14 px), or, when the host doesn't fit on
 * one line (a renamed host after a name clash, "minibar-2.local", is 93 px), the IP address the bar also answers on,
 * or, when that's too long too ("192.168.100.200"), "pair at its" over "IP address", pointing at the Network tile
 * beside it. ui measures and picks (tb_tile_t.foot_alt), as the mock-up's showWifiMenu() measures in the browser. */
static void devices_none_tile(tb_app_t *a)
{
    tb_tile_t *t = add(&a->menu, TB_ACT_NONE, TB_TILE_INFO, "Devices", "None", "");
    snprintf(t->foot, sizeof t->foot, "pair at\n%s", a->wifi_host[0] ? a->wifi_host : "minibar.local");
    if (a->wifi_ip[0]) snprintf(t->foot_alt[t->n_foot_alt++], sizeof t->foot_alt[0], "pair at\n%s", a->wifi_ip);
    snprintf(t->foot_alt[t->n_foot_alt++], sizeof t->foot_alt[0], "pair at its\nIP address");
}

/* showWifiMenu(): on the quick menu's five columns. Network is read-only and two columns wide: the office network as
 * its value, and under it this bar's name and its real address (two lines, each cut with "…"), so bars in one office
 * can be told apart. Devices counts the paired devices and opens Forget all; with none it's read-only and says where
 * to pair (offline: "set up Wi-Fi / to pair"), so a tap there closes the menu like a tap on Network. Then Set up
 * (Change) and Back. */
static void wifi_menu(tb_app_t *a)
{
    tb_menu_t *m = &a->menu;
    bool off = a->wifi_mode == TB_WIFI_OFFLINE;
    m->five = true;
    char foot[sizeof m->tiles[0].foot];
    /* Firmware: with the link down there's no address to show, so it reads like offline. */
    if (off || !a->wifi_link_up || !a->wifi_ip[0]) snprintf(foot, sizeof foot, "%s\nnot connected", a->set.device.name);
    else snprintf(foot, sizeof foot, "%s\n%s \xC2\xB7 %s", a->set.device.name, a->wifi_host, a->wifi_ip);
    tb_tile_t *t = add(m, TB_ACT_NONE, TB_TILE_INFO, "Network", off ? "None" : a->wifi_ssid, foot);
    t->wide = true;
    t->foot_lines = true;
    if (a->paired_count) {
        char v[24];
        if (a->paired_count >= TB_PAIRED_MAX) snprintf(v, sizeof v, "Full");
        else snprintf(v, sizeof v, "%u paired", (unsigned)a->paired_count);
        add(m, TB_ACT_DEVICES, TB_TILE_NORMAL, "Devices", v, "tap to\nforget all");
    } else if (off) {
        add(m, TB_ACT_NONE, TB_TILE_INFO, "Devices", "None", "set up Wi-Fi\nto pair");
    } else {
        devices_none_tile(a);
    }
    add(m, TB_ACT_WIFI_SETUP, TB_TILE_NORMAL, off ? "Set up" : "Change", "Set up", "show the\nQR code");
    add(m, TB_ACT_CLOSE, TB_TILE_DONE, "Close", "Back", "or wait 8 s");
}

/* showForgetMenu(): what Forget all forgets, the button (a tap in its first 600 ms is ignored, so a quick double tap
 * on Devices can't forget everything), and a way back that forgets nothing. The 8 s close forgets nothing either. */
static void forget_menu(tb_app_t *a)
{
    tb_menu_t *m = &a->menu;
    unsigned n = a->paired_count;
    char v[24];
    snprintf(v, sizeof v, "%u device%s", n, n == 1 ? "" : "s");
    add(m, TB_ACT_NONE, TB_TILE_INFO, "Paired", v, a->paired_names)->foot_clamp2 = true;
    add(m, TB_ACT_FORGET_ALL, TB_TILE_DANGER, "Tap again", "Forget all", "each needs a new code");
    add(m, TB_ACT_KEEP_DEVICES, TB_TILE_DONE, "Cancel", "Keep", "back to Wi-Fi");
}

static void power_menu(tb_app_t *a)
{
    tb_menu_t *m = &a->menu;
    add(m, TB_ACT_RESTART, TB_TILE_NORMAL, "Restart", "Restart", "back in a few seconds");
    add(m, TB_ACT_POWER_OFF, TB_TILE_NORMAL, "Power off", "Off", "press PWR to turn on");
    add(m, TB_ACT_CLOSE, TB_TILE_DONE, "Close", "Back", "or wait 8 s");
}

static void setup_menu(tb_app_t *a)
{
    tb_menu_t *m = &a->menu;
    add(m, TB_ACT_WIFI_SKIP, TB_TILE_NORMAL, "Skip", "Skip", "use without Wi-Fi");
    add(m, TB_ACT_WIFI_SETUP, TB_TILE_NORMAL, "Start over", "QR code", "show it again");
    done_tile(m);
}

void tb_menu_fill(tb_app_t *a, const tb_clock_t *now)
{
    tb_menu_t *m = &a->menu;
    /* Firmware: with nothing left to forget (the last device was removed meanwhile), Forget all goes back to Wi-Fi. */
    if (m->kind == TB_MENU_FORGET && !a->paired_count) m->kind = TB_MENU_WIFI;
    m->n = 0;
    m->five = false;
    memset(m->tiles, 0, sizeof(m->tiles));
    switch (m->kind) {
    case TB_MENU_QUICK: quick_menu(a, now); break;
    case TB_MENU_TIMER: timer_menu(a, now); break;
    case TB_MENU_TIMER_SETTINGS: timer_settings(a); break;
    case TB_MENU_WIFI: wifi_menu(a); break;
    case TB_MENU_POWER: power_menu(a); break;
    case TB_MENU_SETUP: setup_menu(a); break;
    case TB_MENU_FORGET: forget_menu(a); break;
    case TB_MENU_NONE: break;
    }
}

void tb_menu_build(tb_app_t *a, tb_menu_kind_t kind, const tb_clock_t *now)
{
    tb_menu_t *m = &a->menu;
    bool same = m->kind == kind;
    tb_ms_t opened = m->opened_at;
    memset(m, 0, sizeof(*m));
    m->kind = kind;
    if (kind == TB_MENU_NONE) return;
    m->opened_at = same ? opened : now->mono;               /* rebuilt in place (Light, Ticking): still the same menu */
    m->closes_at = now->mono + TB_MENU_CLOSE_MS;            /* armMenuTimer() */
    tb_menu_fill(a, now);
}
