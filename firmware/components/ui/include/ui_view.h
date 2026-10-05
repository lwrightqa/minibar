/*
 * ui_view.h: what every screen says, as data. A port of the mock-up's view(), pomoView(), autoView(), wifiView(),
 * sysRow(), side(), tomatoRow() and the splash, hold and (proposed) pairing screens: the words, which line they go on,
 * the color, the progress bar, the icons and the tomatoes. No LVGL: it builds and is tested on Linux, and the LVGL
 * layer (src/ui.c) only lays this out in Bold Signal.
 *
 * Copy is kept as the mock-up writes it ("Pomodoro · focus 2 of 4"); lines Bold Signal draws in capitals (kicker,
 * labels, chips, headlines other than titles and messages) are uppercased by ui.c with ui_text_upper().
 *
 * Owner: ui builder.
 */
#pragma once

#include <stddef.h>

#include "tb_app.h"
#include "tb_pomodoro.h"
#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_LAYOUT_STATUS = 0,   /* kicker, headline, sub + info column (most screens, the pairing screen too) */
    UI_LAYOUT_ALARM,        /* the waiting screens: 64 px tomato + BREAK TIME / BACK TO IT (.endrow) + info column */
    UI_LAYOUT_MESSAGE,      /* the message headline sits in a marquee that scrolls when too wide */
    UI_LAYOUT_SETUP_QR,     /* Wi-Fi setup: QR code, kicker, title, two steps, foot; no info column */
    UI_LAYOUT_SETUP_TEXT,   /* Connecting, Connected, Couldn't connect: kicker, title, sub; no info column */
    UI_LAYOUT_SPLASH,       /* 64 px tomato + MINIBAR on the dark surface */
} ui_layout_t;

/* How the headline is sized (the mock-up's BOLD_SIGNAL.ladder by data-fit). ui.c measures with lv_text_get_width(). */
typedef enum {
    UI_FIT_WORD = 0,    /* the largest of 112, 100, 78, 62 px that fits the column, else 62 cut with "…" */
    UI_FIT_TIME,        /* 112 px (timer, clock, pairing code: tabular digits) */
    UI_FIT_62,          /* 62 px, cut with "…" when too wide: meeting titles, Break time / Back to it, setup titles */
    UI_FIT_MARQUEE,     /* 62 px; stands still when it fits and isn't long, else scrolls (messages) */
} ui_fit_t;

typedef enum { UI_CHIP_NONE = 0, UI_CHIP_MAC, UI_CHIP_CALENDAR } ui_chip_t;
typedef enum { UI_ICON_WIFI = 0, UI_ICON_WIFI_OFF, UI_ICON_MAC, UI_ICON_HEADSET, UI_ICON_CALENDAR } ui_icon_t;

#define UI_SYS_ICONS_MAX 3

typedef struct {
    ui_layout_t layout;
    tb_color_key_t key;             /* paints the field, the tint and the progress track */

    /* main column */
    ui_chip_t chip;                 /* source chip before the kicker on automatic screens ("MAC", "CALENDAR") */
    char kicker[112];               /* drawn in capitals */
    char head[200];                 /* the headline text (UTF-8) */
    char head_ampm[3];              /* the clock's AM/PM after the digits, or "" */
    ui_fit_t fit;
    bool head_caps;                 /* draw in capitals (everything except titles, messages and setup titles) */
    bool head_dots;                 /* "Connecting" counts up dots, none to three, every 300 ms */
    bool marquee_long;              /* a message over 22 characters always scrolls (mock-up: s.message.length > 22) */
    uint32_t marquee_ms;            /* one pass: max(6 s, 0.32 s per character) */
    char sub[200];
    int16_t bar_permille;           /* progress 0..1000, or -1 for none */

    /* info column (absent on setup and splash) */
    bool side;
    char sys_time[12];              /* "2:04 PM", or "2:04" next to the pill; "" while the clock is unknown */
    bool pill;                      /* Pomodoro pill off the Pomodoro screen */
    char pill_text[12];             /* "18:42", "Paused", "Done" */
    tb_phase_t pill_phase;          /* the pill's fill color */
    uint8_t n_icons;                /* right-aligned, in this order, 8 px apart */
    ui_icon_t icons[UI_SYS_ICONS_MAX];
    char label[40];                 /* drawn in capitals */
    char value[64];                 /* "45m", "2:30", "Rest of day" */
    char value_ampm[3];             /* "PM" at 17 px after the digits, or "" */
    bool value_small;               /* word values at 28 px */
    char foot[160];
    bool tomatoes;                  /* the Today row instead of a value */
    uint8_t n_tomatoes;
    tb_tomato_t tomato[TB_POMO_MAX_ROUNDS];

    /* setup */
    char qr_payload[64];            /* "WIFI:T:nopass;S:MiniBar-Setup;;" */
    char step1_pre[32], step1_bold[32], step1_post[64];   /* "Join " "MiniBar-Setup" " with your phone" */
    char step2[96];                 /* "Pick your office Wi-Fi on the page that opens" */
} ui_view_t;

/* Overlays drawn over the screen (each from tb_app_t; built here so their copy is tested too). */
typedef struct {
    bool menu;                      /* a hold menu is open: tiles from tb_app_t.menu */
    bool toast;
    char toast_text[TB_TOAST_BYTES];
    uint8_t toast_title_off, toast_title_len;   /* a meeting title inside the toast that may be cut further */
    bool toast_on_field;            /* centered on the status field (x 224, at most 432 px) instead of the screen */
    bool hold;                      /* PWR hold screen */
    char hold_title[24];            /* "Keep holding" / "Powering off" (drawn in capitals) */
    char hold_sub[48];              /* "to power off" / "Press PWR to turn it back on" */
    uint16_t hold_permille;         /* the track: fills from 400 ms to 3 s; full while Powering off */
    bool flash;                     /* the alarm's white flash is running */
    uint16_t flash_opa;             /* 0..255 this frame (three .55 s ease-out pulses from .7) */
    bool dark;                      /* screen off (PWR press): draw nothing (the backlight is off too) */
} ui_overlay_t;

void ui_view_build(const tb_app_t *a, const tb_clock_t *now, ui_view_t *out);
void ui_overlay_build(const tb_app_t *a, const tb_clock_t *now, ui_overlay_t *out);

/* The mock-up loop()'s redraw key: it changes when anything the screen shows changes (rev), the shown minute (wall
 * clock, or the time since boot while the clock is unknown, for the durations), the timer's second or the pairing
 * countdown's second. ui.c rebuilds the view when it changes; animations (marquee, flash, dots, hold) run on their own. */
uint64_t ui_view_key(const tb_app_t *a, const tb_clock_t *now);

/* The flash's opacity at ms into it (TB_FLASH_MS): the mock-up's @keyframes flash, .7 to 0 with ease-out, three times
 * in 1.65 s, then 0. 0..255. */
uint8_t ui_flash_opa(tb_ms_t ms);

/* Capitals as CSS text-transform: uppercase draws them, for the characters the fonts have: a-z, and Latin-1's
 * à-þ except ÷ (ß becomes "SS"; ÿ and µ, whose capitals the fonts don't have, stay). Writes at most cap bytes. */
void ui_text_upper(char *dst, size_t cap, const char *src);

#ifdef __cplusplus
}
#endif
