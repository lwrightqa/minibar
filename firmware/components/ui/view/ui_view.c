/*
 * ui_view.c: every screen's words as data (see ui_view.h). Owner: ui builder.
 * Skeleton: fills the color, a kicker and the status name so the LVGL layer and the snapshot tool have something to
 * draw. TODO(ui): port view(), pomoView(), autoView(), wifiView(), sysRow(), side() and the overlays word for word.
 */
#include <stdio.h>
#include <string.h>

#include "tb_fmt.h"
#include "ui_view.h"

static const char *const NAMES[TB_ST_COUNT] = {"Available", "Busy", "In a meeting", "Pomodoro", "Away", "Message", "Clock"};

void ui_view_build(const tb_app_t *a, const tb_clock_t *now, ui_view_t *v)
{
    memset(v, 0, sizeof(*v));
    v->key = tb_app_color_key(a, now);
    v->bar_permille = -1;
    if (a->booting) {
        v->layout = UI_LAYOUT_SPLASH;
        snprintf(v->head, sizeof(v->head), "TinyBar");
        return;
    }
    if (tb_app_on_wifi_screen(a)) {
        v->layout = UI_LAYOUT_SETUP_QR;
        snprintf(v->kicker, sizeof(v->kicker), "Wi-Fi setup");
        snprintf(v->head, sizeof(v->head), "Scan to set up");
        snprintf(v->qr_payload, sizeof(v->qr_payload), "WIFI:T:nopass;S:TinyBar-Setup;;");
        v->fit = UI_FIT_QR;
        return;
    }
    v->layout = UI_LAYOUT_STATUS;
    v->side = true;
    snprintf(v->kicker, sizeof(v->kicker), "Status");
    snprintf(v->head, sizeof(v->head), "%s", NAMES[a->idx]);
    v->head_caps = true;
    v->fit = UI_FIT_WORD;
    if (now->valid) tb_fmt_time(v->sys_time, sizeof(v->sys_time), now->wall);
    v->wifi_off = a->wifi_mode == TB_WIFI_OFFLINE;
    v->mac_icon = a->mac_link != TB_LINK_NONE;
}

void ui_overlay_build(const tb_app_t *a, const tb_clock_t *now, ui_overlay_t *o)
{
    (void)now;
    memset(o, 0, sizeof(*o));
    o->dark = a->off;
    if (a->hold != TB_HOLD_NONE) {
        o->hold = true;
        snprintf(o->hold_title, sizeof(o->hold_title), "%s", a->hold == TB_HOLD_POWERING_OFF ? "Powering off" : "Keep holding");
        snprintf(o->hold_sub, sizeof(o->hold_sub), "%s",
                 a->hold == TB_HOLD_POWERING_OFF ? "Press PWR to turn it back on" : "to power off");
    }
}

uint64_t ui_view_key(const tb_app_t *a, const tb_clock_t *now)
{
    /* TODO(ui): the mock-up loop() key: idx, shown minute, phase, remaining second, running, waiting, Wi-Fi mode, rev. */
    return ((uint64_t)a->rev << 32) ^ (uint64_t)(now->valid ? now->wall / 60 : 0);
}
