/*
 * ui.c: the LVGL object tree and its updates (see ui.h). Owner: ui builder.
 * Skeleton: a status field, an info column and two labels, enough to prove the build and the app task's loop.
 * TODO(ui): every screen and overlay in Bold Signal, the fitting ladders, the toast rule, the tile hit test, the QR.
 */
#include <string.h>

#include "ui_assets.h"
#include "ui.h"
#include "ui_fonts.h"
#include "ui_theme.h"
#include "ui_view.h"

static lv_obj_t *s_field, *s_side, *s_kicker, *s_head;
static ui_pointer_fn s_pointer_fn;
static void *s_pointer_ctx;
static uint64_t s_last_key = UINT64_MAX;

static void touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev || !s_pointer_fn) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    bool pressed = code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING;
    s_pointer_fn(s_pointer_ctx, pressed, (int16_t)p.x, (int16_t)p.y, -1);  /* TODO(ui): tile hit test */
}

void ui_init(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_DARK), 0);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESS_LOST, NULL);

    s_field = lv_obj_create(scr);
    lv_obj_remove_style_all(s_field);
    lv_obj_set_pos(s_field, UI_MAIN_X0, 0);
    lv_obj_set_size(s_field, UI_MAIN_X1 - UI_MAIN_X0, UI_H);
    lv_obj_set_style_bg_opa(s_field, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_field, LV_OBJ_FLAG_CLICKABLE);

    s_side = lv_obj_create(scr);
    lv_obj_remove_style_all(s_side);
    lv_obj_set_pos(s_side, UI_SIDE_X0, 0);
    lv_obj_set_size(s_side, UI_W - UI_SIDE_X0, UI_H);
    lv_obj_set_style_bg_opa(s_side, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_side, LV_OBJ_FLAG_CLICKABLE);

    s_kicker = lv_label_create(s_field);
    lv_obj_set_pos(s_kicker, UI_MAIN_TEXT_X, 14);
    lv_obj_set_style_text_color(s_kicker, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_kicker, ui_font(UI_FONT_KICK_15), 0);

    s_head = lv_label_create(s_field);
    lv_obj_set_pos(s_head, UI_MAIN_TEXT_X, 70);
    lv_obj_set_style_text_color(s_head, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_head, ui_font(UI_FONT_HEAD_112), 0);

    (void)ui_asset_tomato_ripe();   /* proves the generated images link */
}

void ui_set_pointer_cb(ui_pointer_fn fn, void *ctx)
{
    s_pointer_fn = fn;
    s_pointer_ctx = ctx;
}

void ui_update(const tb_app_t *a, const tb_clock_t *now)
{
    uint64_t key = ui_view_key(a, now);
    if (key == s_last_key) return;
    s_last_key = key;
    ui_view_t v;
    ui_view_build(a, now, &v);
    lv_obj_set_style_bg_color(s_field, lv_color_hex(UI_STATUS_COLOR[v.key]), 0);
    lv_obj_set_style_bg_color(s_side, lv_color_hex(UI_TINT_COLOR[v.key]), 0);
    if (v.side) lv_obj_remove_flag(s_side, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_side, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_kicker, v.kicker);
    lv_label_set_text(s_head, v.head);
}
