/*
 * ui.c: MiniBar's screens in Bold Signal, with LVGL 9 (see ui.h). Owner: ui builder.
 *
 * The object tree is built once (ui_init) and every screen is a matter of showing, hiding, filling and placing those
 * objects (ui_update). What each screen says comes from the view model (ui_view.h); this file is only the look: the
 * mock-up's "Signal layout" CSS in docs/mockup.html, in screen pixels, with the numbers in ui_theme.h.
 *
 * Text sits on fixed baselines: a label's y is its baseline minus its font's ascent (line_height - base_line), so
 * lines don't move when the headline changes size. Widths are measured with the real fonts (lv_text_get_size), so
 * the headline ladder, the ellipses and the centering match what's drawn.
 *
 * Layers, bottom to top, as in the mock-up's DOM: the screen (.lcd: field, info column, setup and splash), the alarm
 * flash (.lcd::after), the progress bar, the menu, the toast, the dark screen, the hold screen.
 *
 * The 180-degree flip is the display's rotation (board_display_set_flipped: LVGL rotation 90 or 270), which also
 * turns the touch points, so nothing here depends on which way up the bar stands.
 *
 * Redraws: ui_update() rebuilds the view only when ui_view_key() changes, and every setter below compares before it
 * writes, so LVGL invalidates only what really changed. Animations (the message marquee, the alarm flash, the
 * Connecting dots, the hold track) are driven from ui_update() by the monotonic clock.
 */
#include <stdio.h>
#include <string.h>

#include "tb_text.h"
#include "ui.h"
#include "ui_assets.h"
#include "ui_fonts.h"
#include "ui_theme.h"
#include "ui_view.h"

/* ---------- geometry not in ui_theme.h (from the mock-up's Signal layout, measured at 640 x 172) ---------- */
#define CHIP_PAD_X       6      /* .chip padding 0 .9375cqw */
#define CHIP_RADIUS      4      /* .625cqw */
#define CHIP_Y           17     /* its box hangs from y 17.3 to 32.9 around the kicker's baseline */
#define CHIP_H           16
#define CHIP_BASE        29     /* the chip's words sit 1 px above the kicker's baseline, as the mock-up draws them */
#define CHIP_GAP         4      /* margin-right .3em at 12 px (3.6 px) */
#define PILL_BORDER      2
#define PILL_PAD_X       5
#define PILL_Y           13
#define PILL_H           22
#define ICON_PX          16
#define ICON_GAP         8
#define ICON_Y           16
#define SYS_GAP          8      /* the status row's flex gap */
#define VALUE_AMPM_GAP   2      /* .v-ampm margin-left */
#define CLOCK_AMPM_GAP   5      /* .ampm margin-left .15em at 112 px (5.4 px) */
#define CLOCK_AMPM_LS    2      /* letter-spacing .06em at 36 px (2.15 px) */
#define END_TOMATO_X     24
#define END_TOMATO_Y     54     /* 64 px, centered on the screen's height */
#define END_GAP          16     /* .endrow gap 2.5cqw */
#define MARQUEE_X0       24
#define MARQUEE_W        404
#define SETUP_STEP_X     199    /* the list's text starts at 180 + 19.2 (padding-left 3cqw) */
#define SETUP_TITLE_W    440    /* x 180 to 620 */
#define SETUP_TEXT_W     596    /* x 24 to 620 without the QR code */
#define QR_BOX           132
#define QR_PAD           8
#define SPLASH_GAP       16
#define SPLASH_BASE      117
#define MENU_PAD         19.2f  /* 3cqw */
#define MENU_GAP         10.24f /* 1.6cqw */
#define TILE_PAD         12.8f  /* 2cqw */
#define TILE_BORDER      2      /* the inset .25cqw (1.6 px) #3a3f48 edge, drawn 2 px at 80% */
#define TILE_BASE_LABEL  46
#define TILE_BASE_VALUE  88
#define TILE_VALUE_PITCH 29     /* "Show again" on two lines (line-height 1.05 at 28 px: 29.4) */
#define TILE_BASE_FOOT   137
#define TILE_FOOT_PITCH  18     /* line-height 1.25 at 14 px (17.5 px) */
#define TOAST_PAD_X      12
#define TOAST_PAD_Y      6
#define TOAST_H          35     /* 15 px at line-height 1.55, plus padding */
#define TOAST_Y          129    /* 8 px above the bottom edge */
#define TOAST_BASE       152
#define TOAST_FIELD_X    224    /* centered on the status field while the info column shows */
#define TOAST_SCREEN_MAX 601    /* 94% of the screen */
#define HIDE_NEAR        8      /* text whose ink comes within 8 px of the toast hides (clearUnderToast) */
#define HOLD_BASE_TITLE  81
#define HOLD_TRACK_X     192
#define HOLD_TRACK_Y     107
#define HOLD_TRACK_W     256
#define HOLD_TRACK_H     8
#define HOLD_BASE_SUB    140
#define HOLD_TITLE_LS    2      /* letter-spacing .04em at 46 px (1.84 px) */
#define DOTS_MS          300    /* "Connecting", ".", "..", "..." */

#define C_TRACK_HOLD     0x2A2E36
#define C_TILE_EDGE_INK  0x3A3F48   /* the tiles' inset edge (.tile[data-action] box-shadow) */
#define C_DONE_TEXT      0x111316
#define C_QR_LIGHT       0xFFFFFF

/* ---------- the object tree ---------- */
typedef struct {
    lv_obj_t *box, *label, *value, *foot;
} tile_t;

static struct {
    lv_obj_t *scr;
    /* .lcd */
    lv_obj_t *field, *side;
    lv_obj_t *chip, *chip_label, *kicker, *head, *head_ampm, *sub;
    lv_obj_t *marquee, *marquee_label;
    lv_obj_t *end_tomato;
    lv_obj_t *sys_time, *pill, *pill_label, *icons[UI_SYS_ICONS_MAX];
    lv_obj_t *label, *value, *value_ampm, *foot;
    lv_obj_t *tomatoes[TB_POMO_MAX_ROUNDS];
    lv_obj_t *qr_box, *qr, *step_num[2], *step1_pre, *step1_bold, *step1_post, *step2;
    lv_obj_t *splash_tomato;
    /* above .lcd */
    lv_obj_t *flash;
    lv_obj_t *bar, *bar_fill;
    lv_obj_t *menu;
    tile_t tiles[TB_MENU_MAX_TILES];
    lv_area_t tile_area[TB_MENU_MAX_TILES];
    uint8_t n_tiles;
    lv_obj_t *toast, *toast_label;
    lv_obj_t *dark;
    lv_obj_t *hold, *hold_title, *hold_track, *hold_fill, *hold_sub;

    ui_pointer_fn pointer_fn;
    void *pointer_ctx;
    uint64_t last_key;
    bool have_key;
    ui_view_t view;
    ui_overlay_t ov;
    char qr_data[64];
    /* animations */
    tb_ms_t marquee_t0;
    bool marquee_on;
    int32_t marquee_text_w;
    tb_ms_t dots_t0;
    int dots_shown;
    char head_text[200];        /* what the headline says before the Connecting dots */
} U;

/* ---------- small helpers ---------- */

/* The panel shows RGB565, and LVGL turns a 24-bit color into it by dropping the low bits, which darkens every color
 * and tints the dark ones (#0E1013 would land on #081010). Each palette color is moved to the 565 value nearest to it
 * first, so dropping the low bits gives the nearest color the panel can show.
 *
 * Rounding each channel on its own can move a neutral or dark color's hue, though. Four palette colors land visibly
 * off that way (CIEDE2000 2.7 to 4.8): tiles and toasts would turn teal, muted text cyan. For those, the 565 value
 * that's perceptually nearest (among the 8 floor/ceil neighbors, chosen offline) is used instead; each is within 1.6. */
static const struct { uint32_t in, out; } NEAREST_565[] = {
    {0x1C1F24, 0x181C21},   /* tiles and toasts: 565 (3,7,4) = 0x18E4, not the teal (3,8,4) */
    {0xDFE5EA, 0xDEE3E7},   /* muted text: (27,56,28) = 0xDF1C, not the cyan (27,57,28) */
    {0x545C65, 0x525963},   /* the Away field: (10,22,12) = 0x52CC */
    {0x2A2E36, 0x293039},   /* the hold screen's track: (5,12,7) = 0x2987 */
};

static lv_color_t rgb(uint32_t hex)
{
    for (size_t i = 0; i < sizeof NEAREST_565 / sizeof NEAREST_565[0]; i++)
        if (NEAREST_565[i].in == hex) {
            uint32_t o = NEAREST_565[i].out;   /* already on the 565 grid: dropping the low bits keeps it */
            return lv_color_make((uint8_t)(o >> 16), (uint8_t)(o >> 8), (uint8_t)o);
        }
    uint32_t r = (hex >> 16) & 0xFF, g = (hex >> 8) & 0xFF, b = hex & 0xFF;
    uint32_t r5 = (r * 31 + 127) / 255, g6 = (g * 63 + 127) / 255, b5 = (b * 31 + 127) / 255;
    return lv_color_make((uint8_t)(r5 << 3 | r5 >> 2), (uint8_t)(g6 << 2 | g6 >> 4), (uint8_t)(b5 << 3 | b5 >> 2));
}

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
    return o;
}

static lv_obj_t *fill(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    lv_obj_t *o = plain(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, rgb(color), 0);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, ui_font_role_t role, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(l, ui_font(role), 0);
    lv_obj_set_style_text_color(l, rgb(color), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_size(l, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_label_set_text_static(l, "");
    return l;
}

static lv_obj_t *image(lv_obj_t *parent, const lv_image_dsc_t *src)
{
    lv_obj_t *i = lv_image_create(parent);
    lv_obj_remove_flag(i, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_src(i, src);
    return i;
}

static const lv_font_t *font_of(lv_obj_t *l)
{
    return lv_obj_get_style_text_font(l, LV_PART_MAIN);
}

static int32_t ascent(const lv_font_t *f)
{
    return f->line_height - f->base_line;
}

static int32_t text_w(const lv_font_t *f, const char *s, int32_t letter_space)
{
    lv_point_t p;
    lv_text_get_size(&p, s, f, letter_space, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

/* Every character of s is in f (the big headline fonts carry only their fixed copy's characters). */
static bool font_has_all(const lv_font_t *f, const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        uint32_t cp;
        int n;
        if (*p < 0x80) cp = *p, n = 1;
        else if ((*p & 0xE0) == 0xC0) cp = (uint32_t)(p[0] & 0x1F) << 6 | (p[1] & 0x3F), n = 2;
        else if ((*p & 0xF0) == 0xE0) cp = (uint32_t)(p[0] & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F), n = 3;
        else cp = 0xFFFD, n = 4;
        lv_font_glyph_dsc_t g;
        if (!lv_font_get_glyph_dsc(f, &g, cp, 0)) return false;
        for (int k = 0; k < n && *p; k++) p++;
    }
    return true;
}

/* Copy s into out, cut with "…" so it's at most max_w wide in f (CSS text-overflow: ellipsis). */
static void ellipsize(char *out, size_t cap, const char *s, const lv_font_t *f, int32_t ls, int32_t max_w)
{
    tb_strlcpy(out, s, cap);
    if (text_w(f, out, ls) <= max_w) return;
    size_t n = strlen(out);
    while (n > 0) {
        do n--;
        while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80);
        if (n + 4 > cap) continue;
        memcpy(out + n, "\xE2\x80\xA6", 4);
        if (text_w(f, out, ls) <= max_w) return;
    }
    tb_strlcpy(out, "\xE2\x80\xA6", cap);
}

static void set_text(lv_obj_t *l, const char *s)
{
    const char *cur = lv_label_get_text(l);
    if (!cur || strcmp(cur, s)) lv_label_set_text(l, s);
}

static void set_font(lv_obj_t *l, const lv_font_t *f)
{
    if (font_of(l) != f) lv_obj_set_style_text_font(l, f, 0);
}

static void set_pos(lv_obj_t *o, int32_t x, int32_t y)
{
    if (lv_obj_get_style_x(o, 0) != x || lv_obj_get_style_y(o, 0) != y) lv_obj_set_pos(o, x, y);
}

static void set_size(lv_obj_t *o, int32_t w, int32_t h)
{
    if (lv_obj_get_style_width(o, 0) != w || lv_obj_get_style_height(o, 0) != h) lv_obj_set_size(o, w, h);
}

static void set_x(lv_obj_t *o, int32_t x)
{
    if (lv_obj_get_style_x(o, 0) != x) lv_obj_set_x(o, x);
}

/* A label's baseline at y (the label's top is the baseline minus its font's ascent). */
static void at_base(lv_obj_t *l, int32_t x, int32_t baseline)
{
    set_pos(l, x, baseline - ascent(font_of(l)));
}

static void set_bg(lv_obj_t *o, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, 0), c)) lv_obj_set_style_bg_color(o, c, 0);
}

static void set_color(lv_obj_t *l, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_text_color(l, 0), c)) lv_obj_set_style_text_color(l, c, 0);
}

static void set_long(lv_obj_t *l, lv_label_long_mode_t mode, int32_t w)
{
    if (lv_label_get_long_mode(l) != mode) lv_label_set_long_mode(l, mode);
    if (lv_obj_get_style_width(l, 0) != w) lv_obj_set_width(l, w);
}

static void show(lv_obj_t *o, bool on)
{
    if (on == lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_image(lv_obj_t *i, const void *src)
{
    if (lv_image_get_src(i) != src) lv_image_set_src(i, src);
}

/* Fit text to max_w with an ellipsis and put it on its baseline. */
static void put_line(lv_obj_t *l, const char *s, int32_t x, int32_t baseline, int32_t max_w)
{
    char buf[256];
    ellipsize(buf, sizeof buf, s, font_of(l), 0, max_w);
    set_text(l, buf);
    at_base(l, x, baseline);
}

static void put_caps(lv_obj_t *l, const char *s, int32_t x, int32_t baseline, int32_t max_w)
{
    char up[256];
    ui_text_upper(up, sizeof up, s);
    put_line(l, up, x, baseline, max_w);
}

/* lv_color_darken() on the 24-bit spec color, rounded (the spec's track colors), before the panel's 565 rounding.
 * Darkening the already-rounded color and letting LVGL truncate lands one red step low (Focus #6A3808, not #723A08). */
static uint32_t darken24(uint32_t c, uint32_t lvl)
{
    uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    r = (r * (255 - lvl) + 127) / 255;
    g = (g * (255 - lvl) + 127) / 255;
    b = (b * (255 - lvl) + 127) / 255;
    return r << 16 | g << 8 | b;
}

static lv_color_t status_color(tb_color_key_t k)
{
    return rgb(UI_STATUS_COLOR[k < TB_KEY_COUNT ? k : TB_KEY_CLOCK]);
}

static tb_color_key_t phase_key(tb_phase_t ph)
{
    return ph == TB_PH_FOCUS ? TB_KEY_FOCUS : ph == TB_PH_SHORT ? TB_KEY_SHORT : TB_KEY_LONG;
}

/* ---------- touch ---------- */

static int8_t tile_at(int32_t x, int32_t y)
{
    if (lv_obj_has_flag(U.menu, LV_OBJ_FLAG_HIDDEN)) return TB_TILE_NONE;
    for (uint8_t i = 0; i < U.n_tiles; i++) {
        const lv_area_t *a = &U.tile_area[i];
        if (x >= a->x1 && x <= a->x2 && y >= a->y1 && y <= a->y2) return (int8_t)i;
    }
    return TB_TILE_NONE;
}

static void touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev || !U.pointer_fn) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (code == LV_EVENT_PRESS_LOST) {
        U.pointer_fn(U.pointer_ctx, false, (int16_t)p.x, (int16_t)p.y, TB_TILE_LOST);
        return;
    }
    bool pressed = code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING;
    U.pointer_fn(U.pointer_ctx, pressed, (int16_t)p.x, (int16_t)p.y, tile_at(p.x, p.y));
}

/* ---------- building the tree ---------- */

static void build_tile(tile_t *t, lv_obj_t *parent)
{
    t->box = plain(parent);
    lv_obj_set_style_bg_opa(t->box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t->box, UI_RADIUS, 0);
    lv_obj_set_style_border_width(t->box, TILE_BORDER, 0);
    /* Children are placed in screen coordinates relative to the tile's corner, not inside its border. */
    t->label = text(parent, UI_FONT_LABEL_12, UI_COLOR_MUTED);
    t->value = text(parent, UI_FONT_VALUE_28, UI_COLOR_TEXT);
    t->foot = text(parent, UI_FONT_FOOT_14, UI_COLOR_MUTED);
    lv_obj_set_style_text_line_space(t->foot, TILE_FOOT_PITCH - ui_font(UI_FONT_FOOT_14)->line_height, 0);
    lv_obj_set_style_text_line_space(t->value, TILE_VALUE_PITCH - ui_font(UI_FONT_VALUE_28)->line_height, 0);
}

void ui_init(lv_display_t *disp)
{
    memset(&U, 0, sizeof U);
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    U.scr = scr;
    lv_obj_remove_style_all(scr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scr, rgb(UI_COLOR_DARK), 0);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, touch_cb, LV_EVENT_PRESS_LOST, NULL);

    /* .lcd: the status field and the info column */
    U.field = fill(scr, 0, 0, UI_MAIN_X1, UI_H, UI_COLOR_DARK);
    U.side = fill(scr, UI_SIDE_X0, 0, UI_W - UI_SIDE_X0, UI_H, UI_COLOR_DARK);

    U.chip = plain(scr);
    lv_obj_set_style_bg_opa(U.chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(U.chip, CHIP_RADIUS, 0);
    U.chip_label = text(U.chip, UI_FONT_LABEL_12, UI_COLOR_TEXT);
    U.kicker = text(scr, UI_FONT_KICK_15, UI_COLOR_TEXT);
    U.head = text(scr, UI_FONT_HEAD_112, UI_COLOR_TEXT);
    U.head_ampm = text(scr, UI_FONT_AMPM_36, UI_COLOR_TEXT);
    lv_obj_set_style_text_letter_space(U.head_ampm, CLOCK_AMPM_LS, 0);
    U.sub = text(scr, UI_FONT_SUB_19, UI_COLOR_TEXT);

    U.marquee = plain(scr);     /* clips the message to x 24..428 */
    lv_obj_set_pos(U.marquee, MARQUEE_X0, 0);
    lv_obj_set_size(U.marquee, MARQUEE_W, UI_H);
    U.marquee_label = text(U.marquee, UI_FONT_HEAD_62, UI_COLOR_TEXT);

    U.end_tomato = image(scr, ui_asset_tomato_ripe_64());
    lv_obj_set_pos(U.end_tomato, END_TOMATO_X, END_TOMATO_Y);

    U.sys_time = text(scr, UI_FONT_SYS_15, UI_COLOR_TEXT);
    U.pill = plain(scr);
    lv_obj_set_style_bg_opa(U.pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(U.pill, UI_RADIUS, 0);
    lv_obj_set_style_border_width(U.pill, PILL_BORDER, 0);
    lv_obj_set_style_border_color(U.pill, lv_color_white(), 0);
    U.pill_label = text(U.pill, UI_FONT_SYS_15, UI_COLOR_TEXT);
    for (int i = 0; i < UI_SYS_ICONS_MAX; i++) U.icons[i] = image(scr, &ui_icon_wifi);
    U.label = text(scr, UI_FONT_LABEL_12, UI_COLOR_MUTED);
    U.value = text(scr, UI_FONT_VALUE_46, UI_COLOR_TEXT);
    U.value_ampm = text(scr, UI_FONT_AMPM_17, UI_COLOR_TEXT);
    U.foot = text(scr, UI_FONT_FOOT_14, UI_COLOR_MUTED);
    for (int i = 0; i < TB_POMO_MAX_ROUNDS; i++) U.tomatoes[i] = image(scr, ui_asset_tomato_pale());

    /* Wi-Fi setup: the QR code on a white rounded square, the two numbered steps */
    U.qr_box = fill(scr, UI_QR_X, (UI_H - QR_BOX) / 2, QR_BOX, QR_BOX, C_QR_LIGHT);
    lv_obj_set_style_radius(U.qr_box, UI_RADIUS, 0);
    U.qr = lv_qrcode_create(U.qr_box);
    lv_obj_remove_flag(U.qr, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_qrcode_set_size(U.qr, QR_BOX - 2 * QR_PAD);
    lv_qrcode_set_dark_color(U.qr, lv_color_black());
    lv_qrcode_set_light_color(U.qr, lv_color_white());
    lv_obj_set_pos(U.qr, QR_PAD, QR_PAD);
    for (int i = 0; i < 2; i++) U.step_num[i] = text(scr, UI_FONT_STEP_16, UI_COLOR_TEXT);
    U.step1_pre = text(scr, UI_FONT_STEP_16, UI_COLOR_TEXT);
    U.step1_bold = text(scr, UI_FONT_STEP_16_BOLD, UI_COLOR_TEXT);
    U.step1_post = text(scr, UI_FONT_STEP_16, UI_COLOR_TEXT);
    U.step2 = text(scr, UI_FONT_STEP_16, UI_COLOR_TEXT);

    U.splash_tomato = image(scr, ui_asset_tomato_ripe_64());

    /* above .lcd */
    U.flash = fill(scr, 0, 0, UI_W, UI_H, 0xFFFFFF);
    lv_obj_set_style_bg_opa(U.flash, LV_OPA_TRANSP, 0);
    U.bar = fill(scr, 0, UI_PROGRESS_Y, UI_W, UI_PROGRESS_H, UI_COLOR_DARK);
    U.bar_fill = fill(U.bar, 0, 0, 0, UI_PROGRESS_H, 0xFFFFFF);

    U.menu = fill(scr, 0, 0, UI_W, UI_H, UI_COLOR_DARK);
    for (int i = 0; i < TB_MENU_MAX_TILES; i++) build_tile(&U.tiles[i], U.menu);

    U.toast = plain(scr);
    lv_obj_set_style_bg_opa(U.toast, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(U.toast, rgb(UI_COLOR_TOAST), 0);
    lv_obj_set_style_border_color(U.toast, rgb(UI_COLOR_TOAST_EDGE), 0);
    lv_obj_set_style_border_width(U.toast, 1, 0);
    lv_obj_set_style_radius(U.toast, UI_RADIUS, 0);
    U.toast_label = text(U.toast, UI_FONT_SYS_15, UI_COLOR_TEXT);

    U.dark = fill(scr, 0, 0, UI_W, UI_H, 0x000000);

    U.hold = fill(scr, 0, 0, UI_W, UI_H, UI_COLOR_DARK);
    U.hold_title = text(U.hold, UI_FONT_VALUE_46, UI_COLOR_TEXT);
    lv_obj_set_style_text_letter_space(U.hold_title, HOLD_TITLE_LS, 0);
    U.hold_track = fill(U.hold, HOLD_TRACK_X, HOLD_TRACK_Y, HOLD_TRACK_W, HOLD_TRACK_H, C_TRACK_HOLD);
    lv_obj_set_style_radius(U.hold_track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(U.hold_track, true, 0);
    U.hold_fill = fill(U.hold_track, 0, 0, 0, HOLD_TRACK_H, 0xFFFFFF);
    U.hold_sub = text(U.hold, UI_FONT_FOOT_14, UI_COLOR_MUTED);

    U.have_key = false;
}

void ui_set_pointer_cb(ui_pointer_fn fn, void *ctx)
{
    U.pointer_fn = fn;
    U.pointer_ctx = ctx;
}

/* ---------- the screen ---------- */

/* The main column's objects are shown or hidden once per redraw, after the layout has said which it needs, so an
 * object that stays on screen is never hidden and shown again (which would make LVGL redraw it). */
static lv_obj_t *s_need[24];
static int s_n_need;

static void need(lv_obj_t *o)
{
    if (s_n_need < (int)(sizeof s_need / sizeof s_need[0])) s_need[s_n_need++] = o;
}

static void apply_main(void)
{
    lv_obj_t *const all[] = {U.chip, U.kicker, U.head, U.head_ampm, U.sub, U.marquee, U.end_tomato, U.qr_box,
                             U.step_num[0], U.step_num[1], U.step1_pre, U.step1_bold, U.step1_post, U.step2,
                             U.splash_tomato};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        bool on = false;
        for (int k = 0; k < s_n_need && !on; k++) on = s_need[k] == all[i];
        show(all[i], on);
    }
    s_n_need = 0;
}

typedef struct {
    ui_font_role_t role;
    int32_t base;
} rung_t;
/* Bold Signal's ladder: the capitals' centers all sit near y 86, on the baselines the mock-up draws (ui_theme.h). */
static const rung_t LADDER[] = {
    {UI_FONT_HEAD_112, UI_BASE_HEAD_112},
    {UI_FONT_HEAD_100, UI_BASE_HEAD_100},
    {UI_FONT_HEAD_78, UI_BASE_HEAD_78},
    {UI_FONT_HEAD_62, UI_BASE_HEAD_62},
};

/* fitWords() with Bold Signal's ladder: the first rung the text fits (and the font has), else 62 px cut with "…". */
static void put_headline(const ui_view_t *v, int32_t x, int32_t max_w, int32_t base_62)
{
    char txt[256], cut[256];
    if (v->head_caps) ui_text_upper(txt, sizeof txt, v->head);
    else tb_strlcpy(txt, v->head, sizeof txt);
    int first = v->fit == UI_FIT_WORD || v->fit == UI_FIT_TIME ? 0 : 3;
    int last = v->fit == UI_FIT_TIME ? 0 : 3;
    int pick = 3;
    int32_t ampm_w = 0;
    if (v->head_ampm[0]) ampm_w = CLOCK_AMPM_GAP + text_w(ui_font(UI_FONT_AMPM_36), v->head_ampm, CLOCK_AMPM_LS) + CLOCK_AMPM_LS;
    for (int i = first; i <= last; i++) {
        const lv_font_t *f = ui_font(LADDER[i].role);
        if (!font_has_all(f, txt)) continue;
        pick = i;
        if (text_w(f, txt, 0) + ampm_w <= max_w) break;
        if (i == last) break;
    }
    const lv_font_t *f = ui_font(LADDER[pick].role);
    /* the big headline fonts carry no "…": text too wide for them goes down to 62 px, which does */
    if (!font_has_all(f, txt) || (text_w(f, txt, 0) + ampm_w > max_w && !font_has_all(f, "\xE2\x80\xA6")))
        f = ui_font(UI_FONT_HEAD_62), pick = 3;
    int32_t base = pick == 3 ? base_62 : LADDER[pick].base;
    set_font(U.head, f);
    ellipsize(cut, sizeof cut, txt, f, 0, max_w - ampm_w);
    tb_strlcpy(U.head_text, cut, sizeof U.head_text);
    set_text(U.head, cut);
    at_base(U.head, x, base);
    need(U.head);
    if (v->head_ampm[0]) {
        need(U.head_ampm);
        set_text(U.head_ampm, v->head_ampm);
        at_base(U.head_ampm, x + text_w(f, cut, 0) + CLOCK_AMPM_GAP, base);
    }
}

/* The kicker, after the source chip when there is one. */
static void put_kicker(const ui_view_t *v, int32_t x, int32_t max_w)
{
    int32_t kx = x;
    if (v->chip != UI_CHIP_NONE) {
        need(U.chip);
        const char *word = v->chip == UI_CHIP_MAC ? "MAC" : "CALENDAR";
        set_text(U.chip_label, word);
        int32_t w = CHIP_PAD_X * 2 + text_w(ui_font(UI_FONT_LABEL_12), word, 0);
        set_pos(U.chip, x, CHIP_Y);
        set_size(U.chip, w, CHIP_H);
        at_base(U.chip_label, CHIP_PAD_X, CHIP_BASE - CHIP_Y);
        set_bg(U.chip, rgb(UI_TINT_COLOR[v->key]));
        kx = x + w + CHIP_GAP;
        char up[200], line[204];
        ui_text_upper(up, sizeof up, v->kicker);
        snprintf(line, sizeof line, " %s", up);     /* the space between the chip and the words, in the kicker's font */
        put_line(U.kicker, line, kx, UI_BASE_KICKER, x + max_w - kx);
    } else {
        put_caps(U.kicker, v->kicker, kx, UI_BASE_KICKER, max_w);
    }
    need(U.kicker);
}

static void put_sys_row(const ui_view_t *v)
{
    const int32_t x0 = UI_SIDE_X0 + UI_SIDE_INSET, x1 = UI_W - UI_SIDE_INSET;
    /* icons, right-aligned */
    int32_t icons_w = v->n_icons ? v->n_icons * ICON_PX + (v->n_icons - 1) * ICON_GAP : 0;
    int32_t ix = x1 - icons_w;
    for (int i = 0; i < UI_SYS_ICONS_MAX; i++) {
        bool on = i < v->n_icons;
        show(U.icons[i], on);
        if (!on) continue;
        static const lv_image_dsc_t *const SRC[] = {&ui_icon_wifi, &ui_icon_wifi_off, &ui_icon_mac, &ui_icon_headset,
                                                   &ui_icon_calendar};
        set_image(U.icons[i], SRC[v->icons[i]]);
        set_pos(U.icons[i], ix + i * (ICON_PX + ICON_GAP), ICON_Y);
    }
    int32_t time_w = 0;
    show(U.sys_time, v->sys_time[0] != '\0');
    if (v->sys_time[0]) {
        put_line(U.sys_time, v->sys_time, x0, UI_BASE_SYS, x1 - x0);
        time_w = text_w(font_of(U.sys_time), v->sys_time, 0);
    }
    show(U.pill, v->pill);
    if (v->pill) {
        set_text(U.pill_label, v->pill_text);
        int32_t w = text_w(font_of(U.pill_label), v->pill_text, 0) + 2 * (PILL_PAD_X + PILL_BORDER);
        /* justify-content: space-between: the pill sits in the middle of the room between the time and the icons. */
        int32_t left = x0 + time_w, right = ix;
        int32_t px = left + (right - left - w) / 2;
        if (px < left + SYS_GAP) px = left + SYS_GAP;
        set_pos(U.pill, px, PILL_Y);
        set_size(U.pill, w, PILL_H);
        at_base(U.pill_label, PILL_PAD_X, UI_BASE_SYS - PILL_Y - PILL_BORDER);
        set_bg(U.pill, status_color(phase_key(v->pill_phase)));
    }
}

static void put_side(const ui_view_t *v)
{
    const int32_t x0 = UI_SIDE_X0 + UI_SIDE_INSET, w = UI_SIDE_TEXT_W;
    put_sys_row(v);
    show(U.label, true);
    put_caps(U.label, v->label, x0, UI_BASE_LABEL, w);
    show(U.foot, v->foot[0] != '\0');
    put_line(U.foot, v->foot, x0, UI_BASE_FOOT, w);
    show(U.value, !v->tomatoes);
    show(U.value_ampm, !v->tomatoes && v->value_ampm[0]);
    if (!v->tomatoes) {
        set_font(U.value, ui_font(v->value_small ? UI_FONT_VALUE_28 : UI_FONT_VALUE_46));
        int32_t ampm_w = v->value_ampm[0] ? VALUE_AMPM_GAP + text_w(ui_font(UI_FONT_AMPM_17), v->value_ampm, 0) : 0;
        /* word values (28 px) sit a pixel higher, as the mock-up draws them (.ctx-value.small) */
        int32_t base = v->value_small ? UI_BASE_VALUE_WORD : UI_BASE_VALUE;
        put_line(U.value, v->value, x0, base, w - ampm_w);
        if (v->value_ampm[0]) {
            set_text(U.value_ampm, v->value_ampm);
            at_base(U.value_ampm, x0 + text_w(font_of(U.value), lv_label_get_text(U.value), 0) + VALUE_AMPM_GAP, base);
        }
    }
    for (int i = 0; i < TB_POMO_MAX_ROUNDS; i++) {
        /* Four fit the column (32 px with 8 px gaps); a longer set than four shows its first four. */
        bool on = v->tomatoes && i < v->n_tomatoes && i < 4;
        show(U.tomatoes[i], on);
        if (!on) continue;
        const tb_tomato_t *t = &v->tomato[i];
        set_image(U.tomatoes[i], t->kind == TB_TOMATO_RIPE ? ui_asset_tomato_ripe()
                                 : t->kind == TB_TOMATO_GHOST ? ui_asset_tomato_pale()
                                                              : ui_asset_tomato_ripening(t->frame));
        set_pos(U.tomatoes[i], x0 + i * (UI_TOMATO_PX + UI_TOMATO_GAP), UI_TOMATO_Y);
    }
}

static void hide_side(void)
{
    show(U.side, false);
    show(U.sys_time, false);
    show(U.pill, false);
    for (int i = 0; i < UI_SYS_ICONS_MAX; i++) show(U.icons[i], false);
    show(U.label, false);
    show(U.value, false);
    show(U.value_ampm, false);
    show(U.foot, false);
    for (int i = 0; i < TB_POMO_MAX_ROUNDS; i++) show(U.tomatoes[i], false);
}

static void put_marquee(const ui_view_t *v, tb_ms_t now)
{
    const lv_font_t *f = ui_font(UI_FONT_HEAD_62);
    bool changed = strcmp(lv_label_get_text(U.marquee_label), v->head) != 0;
    set_text(U.marquee_label, v->head);
    U.marquee_text_w = text_w(f, v->head, 0);
    /* A message too wide for the column, or over 22 characters, scrolls; a short one stands still. */
    bool scroll = v->marquee_long || U.marquee_text_w > MARQUEE_W;
    if (changed || scroll != U.marquee_on) U.marquee_t0 = now;
    U.marquee_on = scroll;
    need(U.marquee);
    set_pos(U.marquee_label, scroll ? MARQUEE_W : 0, UI_BASE_HEAD_62 - ascent(f));
}

/* The marquee: the words enter at the right edge and leave at the left, one pass per marquee_ms, over and over
 * (translateX(0) to translateX(-100%) of a box padded on the left by the column's width). */
static void animate_marquee(tb_ms_t now)
{
    if (!U.marquee_on || lv_obj_has_flag(U.marquee, LV_OBJ_FLAG_HIDDEN) || !U.view.marquee_ms) return;
    int32_t run = MARQUEE_W + U.marquee_text_w;
    tb_ms_t t = (now - U.marquee_t0) % U.view.marquee_ms;
    if (t < 0) t += U.view.marquee_ms;
    set_x(U.marquee_label, MARQUEE_W - (int32_t)(t * run / U.view.marquee_ms));
}

static void animate_dots(tb_ms_t now)
{
    if (!U.view.head_dots || lv_obj_has_flag(U.head, LV_OBJ_FLAG_HIDDEN)) return;
    int n = (int)(((now - U.dots_t0) / DOTS_MS) % 4);
    if (n < 0) n += 4;
    if (n == U.dots_shown) return;
    U.dots_shown = n;
    char buf[220];
    snprintf(buf, sizeof buf, "%s%.*s", U.head_text, n, "...");
    set_text(U.head, buf);
}

static void put_view(const ui_view_t *v, tb_ms_t now)
{
    bool has_side = v->layout == UI_LAYOUT_STATUS || v->layout == UI_LAYOUT_ALARM || v->layout == UI_LAYOUT_MESSAGE;
    lv_color_t sc = status_color(v->key);
    set_bg(U.field, sc);
    set_size(U.field, has_side ? UI_MAIN_X1 : UI_W, UI_H);
    s_n_need = 0;
    if (has_side) {
        show(U.side, true);
        set_bg(U.side, rgb(UI_TINT_COLOR[v->key]));
        put_side(v);
    } else {
        hide_side();
    }

    const int32_t x = UI_MAIN_TEXT_X;
    /* The sub line's font first: put_line() measures and places it with the font it holds, and the QR screen leaves
     * the 14 px foot font on it (the QR case sets that again below). */
    if (v->layout != UI_LAYOUT_SETUP_QR) set_font(U.sub, ui_font(UI_FONT_SUB_19));
    switch (v->layout) {
    case UI_LAYOUT_STATUS:
        put_kicker(v, x, UI_MAIN_TEXT_W);
        put_headline(v, x, UI_MAIN_TEXT_W, UI_BASE_HEAD_62);
        need(U.sub);
        put_line(U.sub, v->sub, x, UI_BASE_SUB, UI_MAIN_TEXT_W);
        break;
    case UI_LAYOUT_ALARM: {
        put_kicker(v, x, UI_MAIN_TEXT_W);
        need(U.end_tomato);
        int32_t wx = END_TOMATO_X + UI_TOMATO_BIG_PX + END_GAP;
        put_headline(v, wx, x + UI_MAIN_TEXT_W - wx, UI_BASE_HEAD_62);
        need(U.sub);
        put_line(U.sub, v->sub, x, UI_BASE_SUB, UI_MAIN_TEXT_W);
        break;
    }
    case UI_LAYOUT_MESSAGE:
        put_kicker(v, x, UI_MAIN_TEXT_W);
        put_marquee(v, now);
        need(U.sub);
        put_line(U.sub, v->sub, x, UI_BASE_SUB, UI_MAIN_TEXT_W);
        break;
    case UI_LAYOUT_SETUP_QR: {
        need(U.qr_box);
        if (strcmp(U.qr_data, v->qr_payload)) {
            tb_strlcpy(U.qr_data, v->qr_payload, sizeof U.qr_data);
            lv_qrcode_update(U.qr, U.qr_data, (uint32_t)strlen(U.qr_data));
        }
        const int32_t tx = UI_SETUP_TEXT_X;
        put_kicker(v, tx, SETUP_TITLE_W);
        put_headline(v, tx, SETUP_TITLE_W, UI_SETUP_TITLE_BASE);
        /* The numbered steps: "1." and "2." hang in the list's padding, ending a space before the text. */
        const lv_font_t *sf = ui_font(UI_FONT_STEP_16);
        static const char *const NUM[] = {"1.", "2."};
        for (int i = 0; i < 2; i++) {
            need(U.step_num[i]);
            set_text(U.step_num[i], NUM[i]);
            at_base(U.step_num[i], SETUP_STEP_X - text_w(sf, " ", 0) - text_w(sf, NUM[i], 0),
                    i ? UI_SETUP_STEP2_BASE : UI_SETUP_STEP1_BASE);
        }
        int32_t sx = SETUP_STEP_X;
        need(U.step1_pre);
        put_line(U.step1_pre, v->step1_pre, sx, UI_SETUP_STEP1_BASE, UI_W - sx);
        sx += text_w(sf, v->step1_pre, 0);
        need(U.step1_bold);
        put_line(U.step1_bold, v->step1_bold, sx, UI_SETUP_STEP1_BASE, UI_W - sx);
        sx += text_w(ui_font(UI_FONT_STEP_16_BOLD), v->step1_bold, 0);
        need(U.step1_post);
        put_line(U.step1_post, v->step1_post, sx, UI_SETUP_STEP1_BASE, UI_W - sx);
        need(U.step2);
        put_line(U.step2, v->step2, SETUP_STEP_X, UI_SETUP_STEP2_BASE, UI_W - 20 - SETUP_STEP_X);
        /* the foot (.wfoot): Barlow 500 at 14 px, white on the setup color */
        need(U.sub);
        set_font(U.sub, ui_font(UI_FONT_FOOT_14));
        put_line(U.sub, v->sub, tx, UI_BASE_SUB, SETUP_TITLE_W);
        break;
    }
    case UI_LAYOUT_SETUP_TEXT:
        put_kicker(v, x, SETUP_TEXT_W);
        put_headline(v, x, SETUP_TEXT_W, UI_BASE_HEAD_62);
        if (v->head_dots) {
            U.dots_t0 = now;
            U.dots_shown = -1;
        }
        need(U.sub);
        put_line(U.sub, v->sub, x, UI_BASE_SUB, SETUP_TEXT_W);
        break;
    case UI_LAYOUT_SPLASH: {
        char up[32];
        ui_text_upper(up, sizeof up, v->head);
        const lv_font_t *f = ui_font(UI_FONT_HEAD_78);
        int32_t w = UI_TOMATO_BIG_PX + SPLASH_GAP + text_w(f, up, 0);
        int32_t sx = (UI_W - w) / 2;
        need(U.splash_tomato);
        set_pos(U.splash_tomato, sx, (UI_H - UI_TOMATO_BIG_PX) / 2);
        need(U.head);
        set_font(U.head, f);
        set_text(U.head, up);
        at_base(U.head, sx + UI_TOMATO_BIG_PX + SPLASH_GAP, SPLASH_BASE);
        break;
    }
    }
    apply_main();

    /* progress: 6 px along the bottom, a darker shade of the status color with a white fill */
    show(U.bar, v->bar_permille >= 0);
    if (v->bar_permille >= 0) {
        set_bg(U.bar, rgb(darken24(UI_STATUS_COLOR[v->key < TB_KEY_COUNT ? v->key : TB_KEY_CLOCK], UI_PROGRESS_DARKEN)));
        set_size(U.bar_fill, (int32_t)v->bar_permille * UI_W / 1000, UI_PROGRESS_H);
    }
}

/* ---------- overlays ---------- */

static void put_menu(const tb_menu_t *m)
{
    U.n_tiles = m->n;
    /* Columns: the tiles share the row equally, or (the Wi-Fi menu) sit on the quick menu's five columns, where a wide
     * tile spans two (#menu.five, .tile.wide). */
    int cols = 0;
    for (int i = 0; i < m->n; i++) cols += m->tiles[i].wide ? 2 : 1;
    if (m->five) cols = TB_MENU_MAX_TILES;
    float w = (UI_W - 2 * MENU_PAD - (cols - 1) * MENU_GAP) / (cols ? cols : 1);
    const int32_t y0 = (int32_t)(MENU_PAD + .5f), y1 = (int32_t)(UI_H - MENU_PAD + .5f);
    int col = 0;
    for (int i = 0; i < TB_MENU_MAX_TILES; i++) {
        tile_t *t = &U.tiles[i];
        bool on = i < m->n;
        show(t->box, on);
        show(t->label, on);
        show(t->value, on);
        show(t->foot, on);
        if (!on) continue;
        const tb_tile_t *d = &m->tiles[i];
        int span = d->wide ? 2 : 1;
        float fx = MENU_PAD + col * (w + MENU_GAP);
        float tile_w = span * w + (span - 1) * MENU_GAP;
        col += span;
        int32_t x0 = (int32_t)(fx + .5f), x1 = (int32_t)(fx + tile_w + .5f);
        set_pos(t->box, x0, y0);
        set_size(t->box, x1 - x0, y1 - y0);
        U.tile_area[i] = (lv_area_t){x0, y0, x1 - 1, y1 - 1};
        bool done = d->style == TB_TILE_DONE, info = d->style == TB_TILE_INFO, danger = d->style == TB_TILE_DANGER;
        /* A read-only tile (Network) has the tile's fill but no edge, and so does Forget all's red: only tiles that do
         * something on the tile's own fill have one. */
        int32_t border = info || danger ? 0 : TILE_BORDER;
        if (lv_obj_get_style_border_width(t->box, 0) != border) lv_obj_set_style_border_width(t->box, border, 0);
        lv_color_t bg = danger ? status_color(TB_KEY_BUSY) : rgb(done ? UI_COLOR_TILE_DONE : UI_COLOR_TILE);
        set_bg(t->box, bg);
        /* the mock-up's 1.6 px inset edge, drawn as 2 px at 80% over the tile's own color */
        lv_color_t edge = lv_color_mix(rgb(C_TILE_EDGE_INK), bg, 204);
        if (!lv_color_eq(lv_obj_get_style_border_color(t->box, 0), edge)) lv_obj_set_style_border_color(t->box, edge, 0);
        /* Forget all: white label, value and foot on Busy red (5.38:1) */
        lv_color_t small = rgb(danger ? UI_COLOR_TEXT : done ? UI_COLOR_TILE_DONE_TEXT : UI_COLOR_MUTED);
        set_color(t->label, small);
        set_color(t->foot, small);
        set_color(t->value, rgb(done ? C_DONE_TEXT : UI_COLOR_TEXT));

        int32_t tx = (int32_t)(fx + TILE_PAD + .5f), tw = (int32_t)(tile_w - 2 * TILE_PAD);
        put_caps(t->label, d->label, tx, TILE_BASE_LABEL, tw);
        /* The value: one line, cut with "…"; "Show again" wraps onto two (span.two). */
        if (d->value_two_lines) {
            set_long(t->value, LV_LABEL_LONG_MODE_WRAP, tw);
            set_text(t->value, d->value);
            at_base(t->value, tx, TILE_BASE_VALUE);
        } else {
            set_long(t->value, LV_LABEL_LONG_MODE_CLIP, LV_SIZE_CONTENT);
            put_line(t->value, d->value, tx, TILE_BASE_VALUE, tw);
        }
        /* The foot sits at the bottom of the tile, so "synced 2m ago" takes two lines upward. It wraps (a "\n" is the
         * mock-up's <br>); foot_lines: each line is its own, cut with "…" (Network's name and address); foot_clamp2:
         * at most two lines, the second cut with "…" (Forget all's device names). */
        const lv_font_t *ff = font_of(t->foot);
        const int32_t line_space = TILE_FOOT_PITCH - ff->line_height;
        /* foot_alt (the Devices tile with nothing paired): the first foot whose every line fits the tile's content
         * width on one line, else the last. "pair at" over the host, then the IP address, then "pair at its" over "IP
         * address". Measured with lv_text_get_size (EXPAND: no wrapping, x is the widest line) against the content
         * width as a float (86.53 px on the Wi-Fi menu's five columns), with the mock-up's half-pixel allowance. */
        const char *src = d->foot;
        int32_t fw = tw;        /* the foot's width: a line within the half-pixel allowance mustn't wrap */
        if (d->n_foot_alt) {
            const char *cand[3] = {d->foot, d->foot_alt[0], d->foot_alt[1]};
            int nc = 1 + (d->n_foot_alt < 2 ? d->n_foot_alt : 2);
            src = cand[nc - 1];
            for (int k = 0; k < nc; k++) {
                lv_point_t one;
                lv_text_get_size(&one, cand[k], ff, 0, line_space, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
                if (one.x <= tile_w - 2 * TILE_PAD + .5f) {
                    src = cand[k];
                    if (one.x > fw) fw = one.x;
                    break;
                }
            }
        }
        char foot[sizeof d->foot + 16];
        if (d->foot_lines) {
            char line[sizeof d->foot], cut[sizeof d->foot + 4];
            foot[0] = '\0';
            const char *p = src;
            while (*p) {
                const char *nl = strchr(p, '\n');
                size_t n = nl ? (size_t)(nl - p) : strlen(p);
                if (n >= sizeof line) n = sizeof line - 1;
                memcpy(line, p, n);
                line[n] = '\0';
                ellipsize(cut, sizeof cut, line, ff, 0, tw);
                if (foot[0]) strncat(foot, "\n", sizeof foot - strlen(foot) - 1);
                strncat(foot, cut, sizeof foot - strlen(foot) - 1);
                p = nl ? nl + 1 : p + n;
            }
        } else {
            tb_strlcpy(foot, src, sizeof foot);
        }
        lv_point_t sz;
        lv_text_get_size(&sz, foot, ff, 0, line_space, fw, LV_TEXT_FLAG_NONE);
        int lines = (sz.y + line_space) / TILE_FOOT_PITCH;
        if (lines < 1) lines = 1;
        if (d->foot_clamp2 && lines > 2) {
            /* -webkit-line-clamp: 2, as Chrome draws it: what the wrap puts on the first two lines (the longest start
             * that ends at a word and still takes two lines), then "…"; characters come off only if the "…" doesn't
             * fit ("iPad, Android phone,…"). LVGL's DOTS mode would end it in three periods. At most the names of 10
             * devices, measured once per redraw. */
            char cut[sizeof foot];
            size_t best = 0, len = strlen(foot);
            for (size_t k = 1; k <= len; k++) {
                if (foot[k] != ' ' && foot[k] != '\0') continue;         /* a word ends at k */
                memcpy(cut, foot, k);
                cut[k] = '\0';
                lv_text_get_size(&sz, cut, ff, 0, line_space, fw, LV_TEXT_FLAG_NONE);
                if ((sz.y + line_space) / TILE_FOOT_PITCH > 2) break;
                best = k;
            }
            for (;;) {
                while (best > 0 && foot[best - 1] == ' ') best--;     /* trimEnd() before the ellipsis */
                memcpy(cut, foot, best);
                memcpy(cut + best, "\xE2\x80\xA6", 4);
                lv_text_get_size(&sz, cut, ff, 0, line_space, fw, LV_TEXT_FLAG_NONE);
                if (best == 0 || (sz.y + line_space) / TILE_FOOT_PITCH <= 2) break;
                do best--;                                            /* one character off, on a UTF-8 boundary */
                while (best > 0 && (foot[best] & 0xC0) == 0x80);
            }
            tb_strlcpy(foot, cut, sizeof foot);
            lines = 2;
        }
        set_long(t->foot, LV_LABEL_LONG_MODE_WRAP, fw);
        if (lv_obj_get_style_height(t->foot, 0) != LV_SIZE_CONTENT) lv_obj_set_height(t->foot, LV_SIZE_CONTENT);
        set_text(t->foot, foot);
        at_base(t->foot, tx, TILE_BASE_FOOT - (lines - 1) * TILE_FOOT_PITCH);
    }
}

/* toast(), with withTitle()'s cut: a meeting title is shortened (keeping at least 8 characters) while the toast is
 * wider than its room, then the whole line is cut with "…" if it still is. */
static void put_toast(const ui_overlay_t *o)
{
    const lv_font_t *f = ui_font(UI_FONT_SYS_15);
    int32_t center = o->toast_on_field ? TOAST_FIELD_X : UI_W / 2;
    int32_t max_w = (o->toast_on_field ? UI_TOAST_MAX_W : TOAST_SCREEN_MAX) - 2 * TOAST_PAD_X;
    char line[TB_TOAST_BYTES + 8];
    tb_strlcpy(line, o->toast_text, sizeof line);
    size_t off = o->toast_title_off, len = o->toast_title_len;
    if (len && off + len <= strlen(o->toast_text) && text_w(f, line, 0) > max_w) {
        char title[TB_TOAST_BYTES];
        memcpy(title, o->toast_text + off, len);
        title[len] = '\0';
        /* the title without its own trailing "…" */
        size_t tl = len;
        if (tl >= 3 && !memcmp(title + tl - 3, "\xE2\x80\xA6", 3)) tl -= 3;
        title[tl] = '\0';
        size_t n = tb_utf8_len(title);
        while (n > 8) {
            n--;
            char cut[TB_TOAST_BYTES];
            tb_strlcpy(cut, title, sizeof cut);
            tb_text_ellipsize(cut, sizeof cut, n);       /* n - 1 characters and "…" */
            size_t cl = strlen(cut);                     /* trimEnd() before the ellipsis */
            if (cl > 3) {
                size_t k = cl - 3;
                while (k > 0 && cut[k - 1] == ' ') k--;
                memmove(cut + k, cut + cl - 3, 4);
            }
            snprintf(line, sizeof line, "%.*s%s%s", (int)off, o->toast_text, cut, o->toast_text + off + len);
            if (text_w(f, line, 0) <= max_w) break;
        }
    }
    char shown[TB_TOAST_BYTES + 8];
    ellipsize(shown, sizeof shown, line, f, 0, max_w);
    set_text(U.toast_label, shown);
    int32_t w = text_w(f, shown, 0) + 2 * TOAST_PAD_X;
    set_pos(U.toast, center - w / 2, TOAST_Y);
    set_size(U.toast, w, TOAST_H);
    at_base(U.toast_label, TOAST_PAD_X - 1, TOAST_BASE - TOAST_Y - 1);   /* inside the 1 px border */
}

static void put_hold(const ui_overlay_t *o)
{
    char up[32];
    ui_text_upper(up, sizeof up, o->hold_title);
    set_text(U.hold_title, up);
    int32_t w = text_w(font_of(U.hold_title), up, HOLD_TITLE_LS) + HOLD_TITLE_LS;
    at_base(U.hold_title, (UI_W - w) / 2, HOLD_BASE_TITLE);
    set_text(U.hold_sub, o->hold_sub);
    at_base(U.hold_sub, (UI_W - text_w(font_of(U.hold_sub), o->hold_sub, 0)) / 2, HOLD_BASE_SUB);
}

/* clearUnderToast(): while a toast is up, the small text it crosses hides (the sub line, the info column's foot, the
 * setup screen's lines, the menu tiles' feet), so no fragments show around it. Text whose box comes within 8 px of the
 * toast goes; it's back when the toast goes. */
static void hide_under_toast(bool up)
{
    lv_obj_t *cands[4 + 2 * TB_MENU_MAX_TILES];
    int n = 0;
    cands[n++] = U.sub;
    cands[n++] = U.foot;
    cands[n++] = U.step2;
    cands[n++] = U.step1_pre;
    for (int i = 0; i < TB_MENU_MAX_TILES; i++) cands[n++] = U.tiles[i].foot;
    lv_area_t r = {0};
    if (up) {
        lv_obj_update_layout(U.scr);
        lv_obj_get_coords(U.toast, &r);
    }
    for (int i = 0; i < n; i++) {
        lv_obj_t *l = cands[i];
        bool hit = false;
        if (up) {
            /* the text's own extent, as the mock-up measures it (a wrapping label's box is wider than its words) */
            lv_area_t q;
            lv_obj_get_coords(l, &q);
            lv_point_t sz;
            lv_text_get_size(&sz, lv_label_get_text(l), font_of(l), lv_obj_get_style_text_letter_space(l, 0),
                             lv_obj_get_style_text_line_space(l, 0), lv_area_get_width(&q), LV_TEXT_FLAG_NONE);
            q.x2 = q.x1 + sz.x - 1;
            hit = q.x1 < r.x2 + 1 + HIDE_NEAR && q.x2 + 1 > r.x1 - HIDE_NEAR && q.y1 < r.y2 + 1 && q.y2 + 1 > r.y1;
        }
        /* LV_OPA_TRANSP rather than hidden, so the label keeps its place and the next check sees it */
        lv_opa_t want = hit ? LV_OPA_TRANSP : LV_OPA_COVER;
        if (lv_obj_get_style_opa(l, 0) != want) lv_obj_set_style_opa(l, want, 0);
        if (l == U.step1_pre) {     /* the first step is three labels and its number */
            lv_obj_t *more[] = {U.step1_bold, U.step1_post, U.step_num[0]};
            for (int k = 0; k < 3; k++)
                if (lv_obj_get_style_opa(more[k], 0) != want) lv_obj_set_style_opa(more[k], want, 0);
        }
        if (l == U.step2 && lv_obj_get_style_opa(U.step_num[1], 0) != want) lv_obj_set_style_opa(U.step_num[1], want, 0);
    }
}

void ui_refresh(void)
{
    U.have_key = false;
}

void ui_update(const tb_app_t *a, const tb_clock_t *now)
{
    uint64_t key = ui_view_key(a, now);
    ui_overlay_t ov;
    ui_overlay_build(a, now, &ov);
    if (!U.have_key || key != U.last_key) {
        U.have_key = true;
        U.last_key = key;
        ui_view_build(a, now, &U.view);
        put_view(&U.view, now->mono);

        show(U.menu, ov.menu);
        if (ov.menu) put_menu(&a->menu);
        else U.n_tiles = 0;
        show(U.toast, ov.toast);
        if (ov.toast) put_toast(&ov);
        hide_under_toast(ov.toast);
        show(U.hold, ov.hold);
        if (ov.hold) put_hold(&ov);
        show(U.dark, ov.dark);
    }
    /* every call: the animations */
    show(U.flash, ov.flash);
    if (ov.flash && lv_obj_get_style_bg_opa(U.flash, 0) != ov.flash_opa) lv_obj_set_style_bg_opa(U.flash, (lv_opa_t)ov.flash_opa, 0);
    if (ov.hold) set_size(U.hold_fill, (int32_t)ov.hold_permille * HOLD_TRACK_W / 1000, HOLD_TRACK_H);
    animate_marquee(now->mono);
    animate_dots(now->mono);
    U.ov = ov;
}
