/* ui_theme.c: the Bold Signal palette (see ui_theme.h). Owner: ui builder. */
#include "ui_theme.h"

/* Status fields, by tb_color_key_t. White text on each is 4.91:1 or better. */
const uint32_t UI_STATUS_COLOR[TB_KEY_COUNT] = {
    [TB_KEY_AVAILABLE] = 0x0F7F3C, [TB_KEY_BUSY] = 0xD01B3A, [TB_KEY_MEETING] = 0x8B3AE5, [TB_KEY_CALL] = 0x1450B4,
    [TB_KEY_FOCUS] = 0xB0590D,     [TB_KEY_SHORT] = 0x0B7A70, [TB_KEY_LONG] = 0x12708F,    [TB_KEY_AWAY] = 0x545C65,
    [TB_KEY_MESSAGE] = 0xC0198C,   [TB_KEY_CLOCK] = 0x0E1013, [TB_KEY_SETUP] = 0x1D3557,
    [TB_KEY_JIRA] = 0x1D3557,       /* the info navy of the Wi-Fi setup screens; white text 12.4:1 */
};
/* Info column tints: the status color about 24% toward black (lv_color_darken(status, 61)). The clock's column is a
 * raised panel, lighter than its field. Setup has no column. */
const uint32_t UI_TINT_COLOR[TB_KEY_COUNT] = {
    [TB_KEY_AVAILABLE] = 0x0B612E, [TB_KEY_BUSY] = 0x9E152C, [TB_KEY_MEETING] = 0x6A2CAE, [TB_KEY_CALL] = 0x0F3D89,
    [TB_KEY_FOCUS] = 0x86440A,     [TB_KEY_SHORT] = 0x085D55, [TB_KEY_LONG] = 0x0E556D,    [TB_KEY_AWAY] = 0x40464D,
    [TB_KEY_MESSAGE] = 0x92136B,   [TB_KEY_CLOCK] = 0x1B1E23, [TB_KEY_SETUP] = 0x1D3557,
    [TB_KEY_JIRA] = 0x162842,
};
