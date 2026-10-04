/*
 * tb_menu.h: the hold menus as data (mock-up showMenu, showTimerSettings, showWifiMenu, showPowerMenu, calTile).
 * core decides which tiles show and what they say; ui only draws them and reports which tile was tapped.
 *
 * Owner: core builder. Pure C.
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TB_MENU_MAX_TILES 5
#define TB_MENU_CLOSE_MS  8000      /* armMenuTimer(): a menu closes itself after 8 s untouched */

typedef enum {
    TB_MENU_NONE = 0,
    TB_MENU_QUICK,          /* Light, Calendar (or Show again), Wi-Fi, Power, Done */
    TB_MENU_TIMER,          /* Skip, +5 (mid-phase only), Stop, Settings (or Show again), Done */
    TB_MENU_TIMER_SETTINGS, /* Auto-start, Ticking, Back */
    TB_MENU_WIFI,           /* Network (read-only), Set up / Change, [Devices, api.md 4.8, proposed], Back */
    TB_MENU_POWER,          /* Restart, Power off, Back */
    TB_MENU_SETUP,          /* on the Wi-Fi setup screens: Skip, QR code, Done */
} tb_menu_kind_t;

typedef enum {
    TB_ACT_NONE = 0,        /* read-only tile (the Network tile) */
    TB_ACT_CLOSE,           /* Done / Back that closes */
    TB_ACT_BRIGHT,
    TB_ACT_SYNC,
    TB_ACT_SHOW_AGAIN,
    TB_ACT_WIFI,
    TB_ACT_POWER,
    TB_ACT_WIFI_SETUP,
    TB_ACT_WIFI_SKIP,
    TB_ACT_RESTART,
    TB_ACT_POWER_OFF,
    TB_ACT_SKIP,
    TB_ACT_ADD,             /* +5 min */
    TB_ACT_STOP,
    TB_ACT_TIMER_SETTINGS,  /* "tset" */
    TB_ACT_TIMER_MENU,      /* Back to the timer menu */
    TB_ACT_AUTO,
    TB_ACT_TICK,
    TB_ACT_DEVICES,         /* forget all paired devices; first tap asks, second confirms (api.md 4.8, proposed) */
} tb_action_t;
/* A tap on a read-only tile (TB_ACT_NONE) closes the menu, as in the mock-up (the Network tile has no data-action). */

typedef enum {
    TB_TILE_NORMAL = 0,     /* #1C1F24, muted label and foot, white value */
    TB_TILE_DONE,           /* #E8EBEE with dark text (Done, Back) */
    TB_TILE_INFO,           /* read-only (Network) */
} tb_tile_style_t;

typedef struct {
    tb_action_t action;
    tb_tile_style_t style;
    char label[24];         /* <b>: capitals in the UI ("LIGHT") */
    char value[24];         /* <span>: "70%", "Sync", "Show again" */
    bool value_two_lines;   /* <span class="two">: "Show again" wraps onto two lines */
    char foot[64];          /* <small>: "tap to change", "synced 2m ago", "Office-WiFi · 10.0.4.42" */
} tb_tile_t;

typedef struct {
    tb_menu_kind_t kind;
    uint8_t n;
    tb_tile_t tiles[TB_MENU_MAX_TILES];
    tb_ms_t closes_at;      /* monotonic; 0 when closed. Set when a menu opens or an action rebuilds it in place. */
    bool devices_confirm;   /* the Devices tile is asking for its second tap */
} tb_menu_t;

#ifdef __cplusplus
}
#endif
