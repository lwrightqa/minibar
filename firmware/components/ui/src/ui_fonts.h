/*
 * ui_fonts.h: the Bold Signal fonts by role (ui_theme.h ui_font_role_t). Owner: ui builder.
 * The fonts are generated into ../fonts/ by ../tools/build_fonts.sh (see ../fonts/README.md).
 */
#pragma once

#include "lvgl.h"
#include "ui_theme.h"

const lv_font_t *ui_font(ui_font_role_t role);
