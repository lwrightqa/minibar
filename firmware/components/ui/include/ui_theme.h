/*
 * ui_theme.h: Bold Signal as numbers: colors, positions, baselines and type sizes on the 640 x 172 screen.
 * Source: docs/decisions.md "Spec: Bold Signal", docs/mockup.html (BOLD_SIGNAL and the "Signal layout" CSS),
 * scratchpad directions.json id "signal". Pure C (no LVGL types), so the view model and the host tools share it.
 *
 * Owner: ui builder.
 */
#pragma once

#include <stdint.h>

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Colors (0xRRGGBB) ---------- */
/* One look's colors. The screens read the palette in use (ui_pal) at draw time, so a theme switch changes every
 * screen without a reboot. Bold Signal and Low Glare Pixel are defined in ui_theme.c. */
typedef struct {
    uint32_t status[TB_KEY_COUNT];  /* the status color, by tb_color_key_t */
    uint32_t tint[TB_KEY_COUNT];    /* the info column's color, by tb_color_key_t */
    uint32_t text;                  /* text on the status field, at full opacity */
    uint32_t muted;                 /* small text on the tint and on dark surfaces */
    uint32_t dark;                  /* the clock's field, menus, hold screen */
    uint32_t tile;
    uint32_t tile_done;
    uint32_t tile_done_text;
    uint32_t toast;
    uint32_t toast_edge;            /* 1 px inset edge, so a toast shows on dark surfaces */
    bool field_is_status;           /* Bold Signal: the status color fills the field. Low Glare Pixel: the field is dark */
} ui_palette_t;
extern const ui_palette_t UI_PALETTE_BOLD;
extern const ui_palette_t UI_PALETTE_PIXEL;
extern const ui_palette_t *ui_pal;   /* the palette in use: set by ui_update from the saved theme */

#define UI_STATUS_COLOR      (ui_pal->status)
#define UI_TINT_COLOR        (ui_pal->tint)
#define UI_COLOR_TEXT        (ui_pal->text)
#define UI_COLOR_MUTED       (ui_pal->muted)
#define UI_COLOR_DARK        (ui_pal->dark)
#define UI_COLOR_TILE        (ui_pal->tile)
#define UI_COLOR_TILE_DONE   (ui_pal->tile_done)
#define UI_COLOR_TILE_DONE_TEXT (ui_pal->tile_done_text)
#define UI_COLOR_TOAST       (ui_pal->toast)
#define UI_COLOR_TOAST_EDGE  (ui_pal->toast_edge)
#define UI_PROGRESS_DARKEN   90         /* track = lv_color_darken(status, 90); fill white */
#define UI_TINT_DARKEN       61

/* ---------- Layout (screen pixels) ---------- */
#define UI_W                 640
#define UI_H                 172
#define UI_MAIN_X0           0
#define UI_MAIN_X1           448        /* the status field */
#define UI_MAIN_TEXT_X       24         /* text inset left */
#define UI_MAIN_TEXT_W       404        /* to x 428 (20 px right inset) */
#define UI_SIDE_X0           448        /* info column x 448..640, 192 px */
#define UI_SIDE_INSET        16
#define UI_SIDE_TEXT_W       160
#define UI_RADIUS            8          /* tiles, pill, toasts */
#define UI_PROGRESS_Y        166        /* 6 px, y 166..172 */
#define UI_PROGRESS_H        6

/* Fixed baselines (a label's y = baseline - its font's ascent). These are where the mock-up draws each line, measured
 * in Chromium at 640 x 172. The spec ("Spec: Bold Signal") gives 152 for the sub line and the foot and 113 and 107 for
 * the 78 and 62 px headlines; the browser lands 1 px away from those whenever the line's half-leading is a fraction
 * (it floors it), and the firmware follows the mock-up the user approved. */
#define UI_BASE_KICKER       30
#define UI_BASE_SUB          151        /* spec 152 */
#define UI_BASE_SYS          30         /* info column status row, level with the kicker */
#define UI_BASE_LABEL        92
#define UI_BASE_VALUE        130
#define UI_BASE_VALUE_WORD   129        /* word values at 28 px ("Rest of day"): .ctx-value.small, top 102 */
#define UI_BASE_FOOT         151        /* spec 152; level with the sub line, as in the spec */
/* Headline: capitals centered on y 86 at every size. */
#define UI_HEAD_CAP_CENTER_Y 86
#define UI_BASE_HEAD_112     125
#define UI_BASE_HEAD_100     121
#define UI_BASE_HEAD_78      112        /* spec 113 */
#define UI_BASE_HEAD_62      108        /* spec 107 (ARCHITECTURE.md 13.8: decided for 108, as the mock-up draws it) */
/* Tomatoes: 32 px at 1:1, 8 px gaps, y 102..134 in the info column; the alarm screen's tomato is 64 px (2x). */
#define UI_TOMATO_PX         32
#define UI_TOMATO_GAP        8
#define UI_TOMATO_Y          102
#define UI_TOMATO_BIG_PX     64
/* Wi-Fi setup: QR 132 px (4 px modules) at x 24..156 centered on y 86; text from x 180; title baseline 82; the two
 * steps (16 px on a 20 px pitch) at 110 and 130; kicker 30 and foot 151 as usual. */
#define UI_QR_X              24
#define UI_QR_PX             132
#define UI_SETUP_TEXT_X      180
#define UI_SETUP_TITLE_BASE  82
#define UI_SETUP_STEP1_BASE  110
#define UI_SETUP_STEP2_BASE  130
/* Toast: 8 px above the bottom (y 129..164), centered on the status field (x 224, max 432 px wide) while the info
 * column shows, else on the screen. Flipped, the display's rotation turns it with everything else, so it stays 8 px
 * above the bottom edge as the reader sees it (the mock-up's "top: 1.25cqw" is in the rotated device's frame). */
#define UI_TOAST_BOTTOM_GAP  8
#define UI_TOAST_MAX_W       432

/* ---------- Type: what each role uses (generated by tools/build_fonts.sh; see fonts/README.md) ---------- */
typedef enum {
    UI_FONT_HEAD_112 = 0,   /* TinyBar Condensed Bold (Barlow Condensed 700, tabular digits): timer, clock, BUSY */
    UI_FONT_HEAD_100,       /* Barlow Condensed 700: AVAILABLE, ON A CALL */
    UI_FONT_HEAD_78,        /* IN A MEETING, BACK AT 1:30; the splash */
    UI_FONT_HEAD_62,        /* titles, messages, BREAK TIME, setup titles (the full character set) */
    UI_FONT_VALUE_46,       /* info value (tabular); also the hold screen's title */
    UI_FONT_VALUE_28,       /* word values ("Rest of day"), tile values */
    UI_FONT_AMPM_36,        /* AM/PM after the clock (.32 em of 112 px in the mock-up) */
    UI_FONT_AMPM_17,        /* AM/PM after a side value */
    UI_FONT_SUB_19,         /* TinyBar Text Medium (Barlow 500) */
    UI_FONT_STEP_16,        /* Barlow 500 (setup steps), with a 700 cut for the network name */
    UI_FONT_STEP_16_BOLD,
    UI_FONT_KICK_15,        /* Barlow 700, caps, 1.5 px tracking (baked into the font) */
    UI_FONT_SYS_15,         /* Barlow 600, tabular: status-row time, the pill, toasts */
    UI_FONT_FOOT_14,        /* Barlow 500: foot, tile feet, setup foot, hold line */
    UI_FONT_LABEL_12,       /* Barlow 700, caps, 1.2 px tracking (baked in): labels, chips, tile labels */
    UI_FONT_COUNT
} ui_font_role_t;

#ifdef __cplusplus
}
#endif
