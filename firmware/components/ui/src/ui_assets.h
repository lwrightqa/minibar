/*
 * ui_assets.h: images generated at build time by tools/gen_tomatoes.py from assets/tomato_*.png (repo root):
 * the ripe and unripe sprites, the pale one (ripe 55% toward white), and the 24 ripening frames.
 * Owner: ui builder.
 */
#pragma once

#include "lvgl.h"

#define UI_RIPEN_FRAMES 24

const lv_image_dsc_t *ui_asset_tomato_ripe(void);
const lv_image_dsc_t *ui_asset_tomato_unripe(void);
const lv_image_dsc_t *ui_asset_tomato_pale(void);
const lv_image_dsc_t *ui_asset_tomato_ripening(int frame);   /* 0..UI_RIPEN_FRAMES-1 */
