/*
 * layout_test.c: the ui's layout checked on rendered frames, in one process, the way the device draws one screen after
 * another. Owner: ui builder (added by the lead in the 2026-10-04 review round).
 *
 *   tinybar_layout_test        (exit status 0 when every check passes; also run by ctest in build-host-ui)
 *
 * The bug it guards: the sub line was laid out with whatever font the label held from the screen before, so right
 * after the Wi-Fi QR screen (which puts the 14 px foot font on it) the next screen's sub line sat 4 px low and wasn't
 * cut at the column's edge. Each check renders the QR screen first, then the screen under test.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "tb_app.h"
#include "ui.h"
#include "ui_scenes.h"
#include "ui_theme.h"

#define W 640
#define H 172

static uint16_t s_fb[W * H];
static uint8_t s_draw[W * H * 2];
static uint32_t s_tick;
static int s_fail;

static uint32_t tick_cb(void)
{
    return s_tick;
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) s_fb[y * W + x] = src[(y - a->y1) * w + (x - a->x1)];
    lv_display_flush_ready(d);
}

static void render(lv_display_t *d, const char *name)
{
    static tb_app_t a;
    tb_clock_t now;
    const ui_scene_t *sc = ui_scene_find(name);
    if (!sc) {
        printf("FAIL no scene %s\n", name);
        s_fail++;
        return;
    }
    sc->build(&a, &now);
    s_tick = (uint32_t)now.mono;
    /* ui_refresh() only forces the rebuild (two scenes can share a redraw key); the objects keep what the screen
     * before left on them, as on the device. */
    ui_refresh();
    ui_update(&a, &now);
    lv_obj_invalidate(lv_display_get_screen_active(d));
    lv_refr_now(d);
}

static void rgb_of(uint16_t c, int out[3])
{
    out[0] = ((c >> 11) & 31) * 255 / 31;
    out[1] = ((c >> 5) & 63) * 255 / 63;
    out[2] = (c & 31) * 255 / 31;
}

static bool ink(uint16_t c, uint16_t bg)
{
    int p[3], b[3];
    rgb_of(c, p);
    rgb_of(bg, b);
    for (int i = 0; i < 3; i++)
        if (abs(p[i] - b[i]) > 128) return true;
    return false;
}

/* The baseline of the text in a band: the most common bottom row of strong ink across its columns, plus one (as
 * tools' baselines.py measures the mock-up). -1 when the band is empty. */
static int baseline(int x0, int x1, int y0, int y1, uint16_t bg)
{
    int count[H] = {0};
    for (int x = x0; x < x1; x++) {
        int bottom = -1;
        for (int y = y0; y < y1; y++)
            if (ink(s_fb[y * W + x], bg)) bottom = y;
        if (bottom >= 0) count[bottom]++;
    }
    int best = -1;
    for (int y = 0; y < H; y++)
        if (count[y] && (best < 0 || count[y] > count[best])) best = y;
    return best < 0 ? -1 : best + 1;
}

static bool any_ink(int x0, int x1, int y0, int y1, uint16_t bg)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (ink(s_fb[y * W + x], bg)) return true;
    return false;
}

/* The first visible label whose text was cut with "…" (NULL if none): a label counts when it and its parents are
 * shown and it isn't faded out under the toast. */
static const char *cut_label(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) || lv_obj_get_style_opa(o, 0) == LV_OPA_TRANSP) return NULL;
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *t = lv_label_get_text(o);
        if (t && strstr(t, "\xE2\x80\xA6")) return t;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        const char *t = cut_label(lv_obj_get_child(o, (int32_t)i));
        if (t) return t;
    }
    return NULL;
}

/* The first visible label whose text starts with prefix (NULL if none). */
static lv_obj_t *label_with(lv_obj_t *o, const char *prefix)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *t = lv_label_get_text(o);
        if (t && !strncmp(t, prefix, strlen(prefix))) return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *f = label_with(lv_obj_get_child(o, (int32_t)i), prefix);
        if (f) return f;
    }
    return NULL;
}

static void check(bool ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) s_fail++;
}

int main(void)
{
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, s_draw, NULL, sizeof s_draw, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, flush_cb);
    ui_init(d);
    char what[160];

    /* Connecting right after the QR screen: "to Office-WiFi" on its baseline (no descenders in it). */
    render(d, "setup_qr");
    render(d, "setup_connecting");
    uint16_t bg = s_fb[160 * W + 600];
    int b = baseline(UI_MAIN_TEXT_X, UI_MAIN_TEXT_X + UI_MAIN_TEXT_W, 136, 166, bg);
    snprintf(what, sizeof what, "Connecting after the QR screen: sub line baseline %d (want %d)", b, UI_BASE_SUB);
    check(b == UI_BASE_SUB, what);

    /* Every screen with a sub line, right after the QR screen: on its baseline, and cut before the column's edge. */
    static const char *const STATUS[] = {"available", "busy", "meeting_manual_next", "meeting_cal_titles", "pomo_focus",
                                         "call_meeting_titles", "away_back", "clock_titles", "pairing"};
    for (size_t i = 0; i < sizeof STATUS / sizeof STATUS[0]; i++) {
        render(d, "setup_qr");
        render(d, STATUS[i]);
        bg = s_fb[150 * W + 2];
        b = baseline(UI_MAIN_TEXT_X, UI_MAIN_TEXT_X + UI_MAIN_TEXT_W, 136, 162, bg);
        bool inside = !any_ink(UI_MAIN_TEXT_X + UI_MAIN_TEXT_W + 1, UI_MAIN_X1, 136, 162, bg);
        snprintf(what, sizeof what, "%s after the QR screen: sub baseline %d (want %d or within 1 for descenders), "
                 "inside the column: %s", STATUS[i], b, UI_BASE_SUB, inside ? "yes" : "no");
        check(b >= UI_BASE_SUB && b <= UI_BASE_SUB + 1 && inside, what);
    }
    /* The longest real copy in each slot fits without "…" (Barlow's advances round up to whole pixels on the device,
     * so small text runs up to 7 px wider than in the mock-up; copy within about 2% of a slot would be cut). */
    static const char *const LONGEST[] = {"pomo_bigfoot", "pomo_paused_long", "pomo_paused_short", "call_meeting",
                                          "meeting_cal_titles", "available_free_titles", "clock_titles", "away_back",
                                          "setup_qr", "setup_connected", "pairing", "pairing_phone", "menu_quick", "menu_quick_linkdown",
                                          "menu_timer", "menu_timer_settings", "menu_wifi", "menu_wifi_none",
                                          "menu_wifi_offline", "menu_forget", "menu_power", "menu_setup", "toast_status",
                                          "menu_wifi_none_renamed", "menu_wifi_full", "pairing_script", "pairing_late",
                                          "pairing_named", "toast_pair_canceled", "toast_paired", "toast_pair_flip",
                                          "toast_forgot"};
    for (size_t i = 0; i < sizeof LONGEST / sizeof LONGEST[0]; i++) {
        render(d, LONGEST[i]);
        const char *t = cut_label(lv_display_get_screen_active(d));
        snprintf(what, sizeof what, "%s: nothing cut with an ellipsis%s%s", LONGEST[i], t ? ", but: " : "", t ? t : "");
        check(t == NULL, what);
    }
    /* The Devices tile with nothing paired says where to pair, measured against the tile (lv_text_get_size): the host
     * when it fits on one line, else the IP address, else "pair at its" over "IP address". Each line whole, on one
     * line (the label is as wide as its widest line), inside the tile. */
    static const struct { const char *scene, *foot; } WHERE[] = {
        {"menu_wifi_none", "pair at\ntinybar.local"},
        {"menu_wifi_none_renamed", "pair at\n10.0.4.42"},
        {"menu_wifi_none_longip", "pair at its\nIP address"},
    };
    for (size_t i = 0; i < sizeof WHERE / sizeof WHERE[0]; i++) {
        render(d, WHERE[i].scene);
        lv_obj_t *l = label_with(lv_display_get_screen_active(d), "pair at");
        const char *t = l ? lv_label_get_text(l) : "(none)";
        lv_point_t sz = {0, 0};
        if (l) lv_text_get_size(&sz, t, lv_obj_get_style_text_font(l, 0), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
        lv_area_t c = {0};
        if (l) lv_obj_get_coords(l, &c);
        bool one_line_each = l && sz.x <= lv_obj_get_width(l) && lv_obj_get_height(l) < 2 * 18 + 4;
        snprintf(what, sizeof what, "%s: Devices foot \"%s\" (want \"%s\"), %d px wide in a %d px label, x %d..%d", WHERE[i].scene,
                 t, WHERE[i].foot, (int)sz.x, l ? (int)lv_obj_get_width(l) : 0, (int)c.x1, (int)c.x2);
        check(l && !strcmp(t, WHERE[i].foot) && one_line_each && sz.x <= 87, what);
    }
    /* Forget all with 10 devices: the names stop at two lines, the second cut with "…" (by design). */
    render(d, "menu_forget_full");
    check(cut_label(lv_display_get_screen_active(d)) != NULL, "menu_forget_full: the ten names are cut after two lines");

    /* ...and the check sees a cut when there is one: a meeting title too long for the column is cut by design. */
    render(d, "meeting_cal_long");
    check(cut_label(lv_display_get_screen_active(d)) != NULL, "meeting_cal_long: its long title is cut with an ellipsis");
    printf(s_fail ? "%d FAILED\n" : "ALL OK\n", s_fail);
    return s_fail ? 1 : 0;
}
