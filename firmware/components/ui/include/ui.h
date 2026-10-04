/*
 * ui.h: TinyBar's LVGL screens in Bold Signal. Owner: ui builder.
 *
 * The ui draws tb_app_t (through ui_view.h) and reports touches; it never changes the model itself. Everything runs on
 * the app task (main/app_task.c), which owns LVGL. The same code builds on Linux with LVGL's software renderer for
 * the snapshot tool (host/), so screens can be compared with the mock-up without hardware: keep ESP-IDF headers out
 * of src/ and view/ (only lvgl.h, core and the generated fonts and images).
 *
 * Screens: every state in the mock-up: the seven statuses (each with its info-column variants), the Pomodoro (ready,
 * running, paused, both waiting screens, the pill off-screen), On a call (with and without a meeting), In a meeting
 * (with and without titles), the four Wi-Fi setup screens with the QR code (lv_qrcode), the splash. Overlays: the hold
 * menus (tiles from tb_app_t.menu), toasts (with the "hide the small text under the toast" rule), the PWR hold and
 * Powering off screen, the alarm flash, the pairing screen (proposed), the set-aside glyph and the Mac and Wi-Fi icons.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "tb_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Raw touch, in logical screen coordinates, with the menu tile under the point (-1 if none). */
typedef void (*ui_pointer_fn)(void *ctx, bool pressed, int16_t x, int16_t y, int8_t tile);

/* Build the object tree on disp's active screen (all screens' objects are created once and shown or hidden). */
void ui_init(lv_display_t *disp);
/* Where touches go (main passes them to tb_app_pointer()). */
void ui_set_pointer_cb(ui_pointer_fn fn, void *ctx);
/* Bring the screen up to date with the model. Cheap when nothing changed (compares ui_view_key and rev). */
void ui_update(const tb_app_t *a, const tb_clock_t *now);

#ifdef __cplusplus
}
#endif
