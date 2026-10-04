/*
 * board_display.h: the AXS15231B panel and touch as an LVGL 9 display and pointer. Owner: board builder.
 *
 * The panel is 172 x 640 portrait; TinyBar uses it as 640 x 172. Create the LVGL display at the native size and set
 * LV_DISPLAY_ROTATION_90 (or 270 when flipped), rotating in the flush callback with lv_draw_sw_rotate() as
 * 10_LVGL_V9_Test does under USER_DISP_ROT_90. LVGL 9 rotates pointer input by the display's rotation itself, so the
 * touch read callback reports native panel coordinates (check on the board, and check that a swipe still reads the
 * right way after a flip).
 *
 * LVGL is driven only by the app task (main/app_task.c): it calls lv_timer_handler(), and the flush callback blocks on
 * the DMA semaphore in that task. There is no LVGL task in board. board_display_lock() exists for the rare call from
 * another task (none planned); the app task holds it while it runs LVGL.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up QSPI, reset the panel (EXIO5: high 30 ms, low 250 ms, high 30 ms), send the init commands (0x11, 0x29),
 * create the LVGL display with full-frame buffers in PSRAM (2 x 640 x 172 x 2 bytes, plus the rotation buffer) and a
 * 64-line DMA bounce buffer in internal RAM, and set the tick source. flipped picks the rotation. Call lv_init()
 * first (main does). Returns the display, or NULL on failure. */
lv_display_t *board_display_init(bool flipped);

/* Turn the layout 180 degrees (the flip). Invalidates the screen. App task only. */
void board_display_set_flipped(bool flipped);

/* Backlight in percent, 0 = off (dark screen). Maps through a perceptual curve onto the inverted PWM on GPIO 42 and
 * switches BL_EN (EXIO1). The first call after board_display_init() turns it on once a frame has been flushed. */
void board_backlight_set(uint8_t percent);

/* The touch controller on I2C_NUM_1 as an LVGL pointer input device for disp. Returns NULL on failure. */
lv_indev_t *board_touch_init(lv_display_t *disp);

bool board_display_lock(uint32_t timeout_ms);
void board_display_unlock(void);

#ifdef __cplusplus
}
#endif
