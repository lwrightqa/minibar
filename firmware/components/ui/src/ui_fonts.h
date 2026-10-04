/*
 * ui_fonts.h: the generated Barlow fonts by role (ui_theme.h ui_font_role_t). Owner: ui builder.
 * Until fonts/ has the converted files, every role falls back to LVGL's Montserrat 14.
 */
#pragma once

#include "lvgl.h"
#include "ui_theme.h"

const lv_font_t *ui_font(ui_font_role_t role);
