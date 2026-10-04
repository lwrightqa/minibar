/*
 * ui_assets.h: the ui's images. Owner: ui builder.
 *
 * Tomatoes, generated at build time by tools/gen_tomatoes.py from assets/tomato_*.png (repo root): the ripe and unripe
 * sprites (32 x 32), the pale one for upcoming sessions (ripe, 55% toward white), the ripe one at 2x (64 x 64, the
 * alarm screens and the splash), and the 24 ripening frames, identical to the mock-up's buildRipenFrames().
 *
 * Icons for the status row (16 x 16, white with alpha), rasterized from the mock-up's SVGs by tools/gen_icons.js
 * into src/ui_icons.c (committed).
 */
#pragma once

#include "lvgl.h"

#define UI_RIPEN_FRAMES 24

const lv_image_dsc_t *ui_asset_tomato_ripe(void);
const lv_image_dsc_t *ui_asset_tomato_unripe(void);
const lv_image_dsc_t *ui_asset_tomato_pale(void);
const lv_image_dsc_t *ui_asset_tomato_ripe_64(void);
const lv_image_dsc_t *ui_asset_tomato_ripening(int frame);   /* 0..UI_RIPEN_FRAMES-1 */

extern const lv_image_dsc_t ui_icon_wifi;
extern const lv_image_dsc_t ui_icon_wifi_off;   /* crossed out: offline */
extern const lv_image_dsc_t ui_icon_headset;    /* a call set aside */
extern const lv_image_dsc_t ui_icon_calendar;   /* a meeting set aside */
extern const lv_image_dsc_t ui_icon_mac;        /* a Mac is connected */
