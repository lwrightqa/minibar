/* ui_theme.c: the two looks' palettes (see ui_theme.h). Owner: ui builder.
 * Bold Signal is the default; Low Glare Pixel is the alternate (decisions.md "Look"), from the mock-up's LOW_GLARE_PIXEL. */
#include "ui_theme.h"

/* Bold Signal: status fields, white text (4.91:1 or better), info columns a solid darker tint. */
const ui_palette_t UI_PALETTE_BOLD = {
    .status = {
        [TB_KEY_AVAILABLE] = 0x0F7F3C, [TB_KEY_BUSY] = 0xD01B3A, [TB_KEY_MEETING] = 0x8B3AE5, [TB_KEY_CALL] = 0x1450B4,
        [TB_KEY_FOCUS] = 0xB0590D,     [TB_KEY_SHORT] = 0x0B7A70, [TB_KEY_LONG] = 0x12708F,    [TB_KEY_AWAY] = 0x545C65,
        [TB_KEY_MESSAGE] = 0xC0198C,   [TB_KEY_CLOCK] = 0x0E1013, [TB_KEY_SETUP] = 0x1D3557,
        [TB_KEY_JIRA] = 0x1D3557,      /* the info navy of the Wi-Fi setup screens; white text 12.4:1 */
    },
    /* The status color about 24% toward black (lv_color_darken(status, 61)). The clock's column is a raised panel. */
    .tint = {
        [TB_KEY_AVAILABLE] = 0x0B612E, [TB_KEY_BUSY] = 0x9E152C, [TB_KEY_MEETING] = 0x6A2CAE, [TB_KEY_CALL] = 0x0F3D89,
        [TB_KEY_FOCUS] = 0x86440A,     [TB_KEY_SHORT] = 0x085D55, [TB_KEY_LONG] = 0x0E556D,    [TB_KEY_AWAY] = 0x40464D,
        [TB_KEY_MESSAGE] = 0x92136B,   [TB_KEY_CLOCK] = 0x1B1E23, [TB_KEY_SETUP] = 0x1D3557,
        [TB_KEY_JIRA] = 0x162842,
    },
    .text = 0xFFFFFF,
    .muted = 0xDFE5EA,
    .dark = 0x0E1013,
    .tile = 0x1C1F24,
    .tile_done = 0xE8EBEE,
    .tile_done_text = 0x4A525C,
    .toast = 0x1C1F24,
    .toast_edge = 0x4A515C,
    .field_is_status = true,
};

/* Low Glare Pixel: a warm near-black screen, the status color in the words and a thin edge, so it gives off less light.
 * Colors from the mock-up's LOW_GLARE (base, colors, tints and fills). The mock has no tint for clock, setup or Jira;
 * they take the tile fill, a neutral dark. */
const ui_palette_t UI_PALETTE_PIXEL = {
    .status = {
        [TB_KEY_AVAILABLE] = 0x5CCF72, [TB_KEY_BUSY] = 0xFF5F57, [TB_KEY_MEETING] = 0xA588FF, [TB_KEY_CALL] = 0xF2D04F,
        [TB_KEY_FOCUS] = 0xFF9945,     [TB_KEY_SHORT] = 0x36C6C0, [TB_KEY_LONG] = 0x6CBCFA,    [TB_KEY_AWAY] = 0xB4AB9E,
        [TB_KEY_MESSAGE] = 0xF27AB8,   [TB_KEY_CLOCK] = 0x4A463F, [TB_KEY_SETUP] = 0x6CBCFA,
        [TB_KEY_JIRA] = 0x6CBCFA,
    },
    .tint = {
        [TB_KEY_AVAILABLE] = 0x172018, [TB_KEY_BUSY] = 0x241716, [TB_KEY_MEETING] = 0x1D1B23, [TB_KEY_CALL] = 0x232015,
        [TB_KEY_FOCUS] = 0x241C14,     [TB_KEY_SHORT] = 0x141F1E, [TB_KEY_LONG] = 0x181F23,    [TB_KEY_AWAY] = 0x1E1D1B,
        [TB_KEY_MESSAGE] = 0x23191D,   [TB_KEY_CLOCK] = 0x1C1B19, [TB_KEY_SETUP] = 0x1C1B19,
        [TB_KEY_JIRA] = 0x1C1B19,
    },
    .text = 0xE2DDD3,
    .muted = 0x9A948A,
    .dark = 0x111110,
    .tile = 0x1C1B19,
    .tile_done = 0x2B2A26,
    .tile_done_text = 0x9A948A,
    .toast = 0x23221F,
    .toast_edge = 0x3A3833,
    .field_is_status = false,
};

const ui_palette_t *ui_pal = &UI_PALETTE_BOLD;
