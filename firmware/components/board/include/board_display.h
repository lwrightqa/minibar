/*
 * board_display.h: the AXS15231B panel and touch as an LVGL 9 display and pointer, and the backlight.
 * Owner: board builder.
 *
 * The panel is 172 x 640 portrait; TinyBar uses it as 640 x 172. The LVGL display is created at the native size with
 * LV_DISPLAY_ROTATION_90 (or 270 when flipped); the flush callback turns each full frame with lv_draw_sw_rotate(), as
 * 10_LVGL_V9_Test does under USER_DISP_ROT_90. LVGL 9 turns pointer input by the display's rotation itself
 * (lv_display_rotate_point), so the touch read callback reports native panel coordinates and stays right after a
 * flip (checked on the host against Waveshare's landscape touch mapping; see test/host/board).
 *
 * LVGL is driven only by the app task (main/app_task.c): it calls lv_timer_handler(), and the flush callback sends the
 * frame in that task (about 12 ms of QSPI transfers plus the rotation). There is no LVGL task in board.
 * board_display_lock() exists for the rare call from another task (none planned).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up QSPI (SPI3, 40 MHz, mode 3), reset the panel (EXIO5: high 30 ms, low 250 ms, high 30 ms), send the init
 * commands (0x11, 0x29), create the LVGL display with one full-frame RGB565 buffer and a rotation buffer in PSRAM
 * (2 x 215 KB) and a 64-line DMA buffer in internal RAM (21.5 KB), set the tick source, and set up the backlight PWM
 * (dark). flipped picks the rotation (90 upright, 270 flipped; CONFIG_TINYBAR_LCD_TURN_180 swaps them). Call lv_init() first (main does). If the panel itself fails, the display is
 * still created (it draws nowhere, and the log says why) so the rest of the bar keeps working. Returns NULL only
 * when there's no memory. Takes about 0.6 s. */
lv_display_t *board_display_init(bool flipped);

/* Turn the layout 180 degrees (the flip): rotation 90 or 270. LVGL redraws the whole screen. App task only. */
void board_display_set_flipped(bool flipped);

/* Frames sent to the panel so far, their average rotate + send time, and the slowest since the last call (bring-up
 * figures for the health log). App task only. */
void board_display_stats(uint32_t *frames, uint32_t *avg_flush_ms, uint32_t *max_flush_ms);
/* Frames sent so far (to tell whether an lv_timer_handler() call flushed one). */
uint32_t board_display_frame_count(void);

/* Backlight in percent, 0 = off (dark screen). Follows the mock-up's Light levels: percent -> its brightness factor
 * 0.45 + 0.55 p -> luminance -> LED current -> the inverted PWM on GPIO 42 (see brd_logic.h), with BL_EN (EXIO1) on
 * whenever percent > 0. Nothing lights until the first frame has reached the panel; then the last level asked for
 * applies (70% if none was). While it's 0, frames aren't sent to the panel; the first one after it comes back on is
 * sent before the light. App task only. */
void board_backlight_set(uint8_t percent);

/* The touch controller on I2C_NUM_1 (GPIO 17/18, 0x3B) as an LVGL pointer input device for disp, read by polling.
 * Returns NULL on failure. */
lv_indev_t *board_touch_init(lv_display_t *disp);

bool board_display_lock(uint32_t timeout_ms);
void board_display_unlock(void);

#ifdef __cplusplus
}
#endif
