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
    TB_MENU_QUICK,          /* Display, Calendar (or Show again), Wi-Fi, Power, Done */
    TB_MENU_TIMER,          /* Skip, +5 (mid-phase only), Stop, Settings (or Show again), Done */
    TB_MENU_TIMER_SETTINGS, /* Auto-start, Ticking, Back */
    TB_MENU_WIFI,           /* Network (read-only, two columns), Devices, Set up / Change, Back: the five-column grid */
    TB_MENU_POWER,          /* Restart, Power off, Back */
    TB_MENU_SETUP,          /* on the Wi-Fi setup screens: Skip, QR code, Done */
    TB_MENU_FORGET,         /* Devices' confirmation: Paired (read-only), Forget all (danger), Keep (proposed) */
    TB_MENU_DISPLAY,        /* the quick menu's Display tile: Light, Chime, Time, Back (decisions.md; Theme and Tap sound
                             * join it when the bar has them) */
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
    TB_ACT_DEVICES,         /* the Devices tile: opens the Forget all confirmation (api.md 4.8, proposed) */
    TB_ACT_FORGET_ALL,      /* forget every paired device (a tap in the first 600 ms is ignored) */
    TB_ACT_KEEP_DEVICES,    /* back to the Wi-Fi menu without forgetting */
    TB_ACT_DISPLAY,         /* the quick menu's Display tile: opens the Display menu */
    TB_ACT_QUICK_MENU,      /* the Display menu's Back: the quick menu again */
    TB_ACT_TIME_FMT,        /* the Time tile: 12-hour <-> 24-hour, the menu stays open */
    TB_ACT_MEET_CHIME,      /* the Chime tile: the meeting chime on <-> off, the menu stays open */
} tb_action_t;
/* A tap on a read-only tile (TB_ACT_NONE) closes the menu, as in the mock-up (the Network tile has no data-action). */

#define TB_FORGET_GUARD_MS 600      /* showForgetMenu(): taps on Forget all this soon after it opens are ignored */

typedef enum {
    TB_TILE_NORMAL = 0,     /* #1C1F24, muted label and foot, white value */
    TB_TILE_DONE,           /* #E8EBEE with dark text (Done, Back) */
    TB_TILE_INFO,           /* read-only (Network, Devices "None", Paired): the tile's fill, no edge */
    TB_TILE_DANGER,         /* Forget all: Busy red #D01B3A, white label, value and foot, no edge */
} tb_tile_style_t;

typedef struct {
    tb_action_t action;
    tb_tile_style_t style;
    char label[24];         /* <b>: capitals in the UI ("LIGHT") */
    char value[40];         /* <span>: "70%", "Sync", "Show again", a network name */
    bool value_two_lines;   /* <span class="two">: "Show again" wraps onto two lines */
    bool wide;              /* spans two of the five columns (the Wi-Fi menu's Network tile) */
    bool foot_lines;        /* the foot is "line\nline": two separate lines, each cut with "…" (never wraps) */
    bool foot_clamp2;       /* the foot wraps onto at most two lines, the second cut with "…" (the device names) */
    char foot[160];         /* <small>: "tap to change", "synced 2m ago", "MiniBar 2A1C\nminibar.local · 10.0.4.42" */
    /* Feet to fall back on, in order, when a line of foot doesn't fit the tile on one line: ui measures each line
     * against the tile's content width (lv_text_get_size) and draws the first that fits, else the last. The Devices
     * tile with nothing paired: "pair at\nminibar.local", then "pair at\n10.0.4.42", then "pair at its\nIP address". */
    char foot_alt[2][32];
    uint8_t n_foot_alt;
} tb_tile_t;

typedef struct {
    tb_menu_kind_t kind;
    uint8_t n;
    bool five;              /* laid out on the quick menu's five columns whatever the tile count (the Wi-Fi menu) */
    tb_tile_t tiles[TB_MENU_MAX_TILES];
    tb_ms_t closes_at;      /* monotonic; 0 when closed. Set when a menu opens or an action rebuilds it in place. */
    tb_ms_t opened_at;      /* when this menu opened (the Forget all guard) */
} tb_menu_t;

#ifdef __cplusplus
}
#endif
