/*
 * ui_view.h: what every screen says, as data. A port of the mock-up's view(), pomoView(), autoView(), wifiView(),
 * sysRow(), side() and the splash, hold and (proposed) pairing screens: the words, which line they go on, the color,
 * the progress bar and the tomatoes. No LVGL: it builds and is tested on Linux, and the LVGL layer (ui.c) only lays
 * this out in Bold Signal.
 *
 * Owner: ui builder.
 */
#pragma once

#include "tb_app.h"
#include "tb_pomodoro.h"
#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_LAYOUT_STATUS = 0,   /* kicker, headline, sub + info column (most screens) */
    UI_LAYOUT_ALARM,        /* the waiting screens: 64 px tomato + "BREAK TIME" / "BACK TO IT" (.endrow) */
    UI_LAYOUT_MESSAGE,      /* the message headline sits in a marquee that scrolls when too wide */
    UI_LAYOUT_SETUP_QR,     /* Wi-Fi setup: QR code, title, two steps, foot; no info column */
    UI_LAYOUT_SETUP_TEXT,   /* Connecting, Connected, Couldn't connect: kicker, title, sub; no info column */
    UI_LAYOUT_SPLASH,       /* tomato + "TinyBar" on the dark surface */
} ui_layout_t;

/* The headline's size ladder (mock-up BOLD_SIGNAL.ladder, by data-fit): the LVGL layer takes the largest size of the
 * ladder that fits 404 px, measured with lv_text_get_width() in that font. */
typedef enum {
    UI_FIT_WORD = 0,    /* 112, 100, 78, 62 */
    UI_FIT_TIME,        /* 112 (timer and clock; tabular digits) */
    UI_FIT_PAIR,        /* 62 (Break time, Back to it) */
    UI_FIT_TITLE,       /* 62, then cut with an ellipsis (meeting titles, mixed case) */
    UI_FIT_MSG,         /* 62, scrolls when too wide (messages, mixed case) */
    UI_FIT_SETUP,       /* 62 (Connecting, tinybar.local, the error) */
    UI_FIT_QR,          /* 62 ("Scan to set up") */
} ui_fit_t;

typedef enum { UI_CHIP_NONE = 0, UI_CHIP_MAC, UI_CHIP_CALENDAR } ui_chip_t;
typedef enum { UI_ASIDE_NONE = 0, UI_ASIDE_HEADSET, UI_ASIDE_CALENDAR } ui_aside_icon_t;

typedef struct {
    ui_layout_t layout;
    tb_color_key_t key;             /* paints the field, the tint and the progress track */

    /* main column */
    ui_chip_t chip;                 /* source chip before the kicker on automatic screens */
    char kicker[96];                /* drawn in capitals */
    char head[200];                 /* the headline text (UTF-8) */
    char head_ampm[3];              /* the clock's AM/PM after the digits, or "" */
    ui_fit_t fit;
    bool head_caps;                 /* draw in capitals (everything except titles and messages) */
    bool marquee;                   /* messages longer than 22 characters scroll (mock-up: s.message.length > 22) */
    uint16_t marquee_ms;            /* one pass: max(6 s, 0.32 s per character) */
    char sub[160];
    int16_t bar_permille;           /* progress 0..1000, or -1 for none */

    /* info column (absent on setup and splash) */
    bool side;
    char sys_time[12];              /* "2:04 PM", or "2:04" when the pill shows */
    bool pill;                      /* Pomodoro pill off the Pomodoro screen */
    char pill_text[12];             /* "18:42", "Paused", "Done" */
    tb_phase_t pill_phase;          /* the pill's fill color */
    ui_aside_icon_t aside_icon;
    bool mac_icon;
    bool wifi_off;                  /* crossed-out Wi-Fi icon (offline) */
    char label[32];                 /* drawn in capitals */
    char value[48];                 /* "45m", "2:30", "Rest of day" */
    char value_ampm[3];             /* "PM" at 17 px after the digits, or "" */
    bool value_small;               /* word values at 28 px */
    char foot[96];
    bool tomatoes;                  /* the Today row instead of a value */
    uint8_t n_tomatoes;
    tb_tomato_t tomato[TB_POMO_MAX_ROUNDS];

    /* setup */
    char qr_payload[64];            /* "WIFI:T:nopass;S:TinyBar-Setup;;" */
    char step1[96], step2[96];      /* "Join TinyBar-Setup with your phone" (TinyBar-Setup in the bold cut) */
    char step_bold[32];             /* the part of step1 drawn bold */
} ui_view_t;

/* Overlays drawn over the screen (each from tb_app_t; built here so their copy is tested too). */
typedef struct {
    bool hold;                      /* PWR hold screen */
    char hold_title[24];            /* "Keep holding" / "Powering off" */
    char hold_sub[48];              /* "to power off" / "Press PWR to turn it back on" */
    uint16_t hold_progress;         /* 0..1000 across the 3 s */
    bool pairing;                   /* api.md 4.8 (Proposed; needs the UX designer's drawing) */
    char pair_kicker[48];           /* "Pairing · Mac" */
    char pair_code[8];              /* "482 913" */
    char pair_sub[64];              /* "Type this code on that device · tap to cancel" */
    char pair_label[24];            /* "Code expires" */
    char pair_value[16];            /* "1:42" */
    bool flash;                     /* the alarm's white flash is running */
    uint16_t flash_phase;           /* 0..1000 through TB_FLASH_MS */
    bool dark;                      /* screen off (backlight off; draw nothing new) */
} ui_overlay_t;

void ui_view_build(const tb_app_t *a, const tb_clock_t *now, ui_view_t *out);
void ui_overlay_build(const tb_app_t *a, const tb_clock_t *now, ui_overlay_t *out);

/* The mock-up loop()'s redraw key: changes when the shown minute, the timer's second, the phase, the Wi-Fi mode or
 * rev changes. ui redraws the screen only when it changes (so a message marquee keeps scrolling). */
uint64_t ui_view_key(const tb_app_t *a, const tb_clock_t *now);

#ifdef __cplusplus
}
#endif
