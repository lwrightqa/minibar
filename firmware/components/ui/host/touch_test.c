/*
 * touch_test.c: the ui's touch forwarding with LVGL's real input path. Owner: ui builder.
 *
 *   tinybar_touch_test        (exit status 0 when every check passes; also run by ctest in build-host-ui)
 *
 * A virtual pointer stands in for the AXS15231B. It reports panel coordinates, LVGL turns them by the display's
 * rotation (as board_display sets it: 0 here for the landscape panel, 180 for the flip), and the ui must hand
 * tb_app_pointer() logical screen coordinates with the menu tile under the point: pressed while the finger is down,
 * released with the tile it lifted on, and TB_TILE_LOST when LVGL loses the press. Also checks that a redraw sends
 * the panel only what changed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "tb_app.h"
#include "ui.h"
#include "ui_scenes.h"

#define W 640
#define H 172

static uint8_t s_draw[W * H * 2];
static uint32_t s_tick;
static int s_fail;

static struct {
    bool down;
    int32_t x, y;   /* panel coordinates */
} s_touch;

static struct {
    int n;
    bool pressed[64];
    int16_t x[64], y[64];
    int8_t tile[64];
} s_got;

static uint32_t tick_cb(void)
{
    return s_tick;
}

/* What LVGL sent to the panel since the last reset: the number of pixels and their bounding box. */
static struct {
    long px;
    lv_area_t box;
} s_flushed;

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)px;
    if (!s_flushed.px) s_flushed.box = *a;
    else {
        s_flushed.box.x1 = LV_MIN(s_flushed.box.x1, a->x1);
        s_flushed.box.y1 = LV_MIN(s_flushed.box.y1, a->y1);
        s_flushed.box.x2 = LV_MAX(s_flushed.box.x2, a->x2);
        s_flushed.box.y2 = LV_MAX(s_flushed.box.y2, a->y2);
    }
    s_flushed.px += (long)lv_area_get_width(a) * lv_area_get_height(a);
    lv_display_flush_ready(d);
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = s_touch.x;
    data->point.y = s_touch.y;
    data->state = s_touch.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void on_pointer(void *ctx, bool pressed, int16_t x, int16_t y, int8_t tile)
{
    (void)ctx;
    if (s_got.n < 64) {
        s_got.pressed[s_got.n] = pressed;
        s_got.x[s_got.n] = x;
        s_got.y[s_got.n] = y;
        s_got.tile[s_got.n] = tile;
        s_got.n++;
    }
}

#define CHECK(c, ...)                                                   \
    do {                                                                \
        if (!(c)) {                                                     \
            s_fail++;                                                   \
            printf("FAIL line %d: ", __LINE__);                         \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)

static void step(lv_indev_t *indev, int ms)
{
    for (int t = 0; t < ms; t += 10) {
        s_tick += 10;
        lv_indev_read(indev);
    }
}

/* A tap at logical (x, y): the panel reports it turned by the display's rotation. */
static void tap(lv_indev_t *indev, bool flipped, int32_t x, int32_t y)
{
    s_got.n = 0;
    s_touch.x = flipped ? W - 1 - x : x;
    s_touch.y = flipped ? H - 1 - y : y;
    s_touch.down = true;
    step(indev, 60);
    s_touch.down = false;
    step(indev, 30);
}

static void show(lv_display_t *d, const char *name, tb_app_t *a, tb_clock_t *now, bool flipped)
{
    ui_scene_find(name)->build(a, now);
    lv_display_set_rotation(d, flipped ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0);
    ui_refresh();
    s_tick = (uint32_t)now->mono;
    ui_update(a, now);
    lv_refr_now(d);
}

/* The tile centers, as the menu lays out n tiles (19.2 px around, 10.24 px between). */
static int32_t tile_cx(int i, int n)
{
    float w = (W - 2 * 19.2f - (n - 1) * 10.24f) / n;
    return (int32_t)(19.2f + i * (w + 10.24f) + w / 2);
}

int main(void)
{
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, s_draw, NULL, sizeof s_draw, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
    lv_indev_set_display(indev, d);
    lv_indev_set_mode(indev, LV_INDEV_MODE_NONE);   /* read only when the test says so */
    ui_init(d);
    ui_set_pointer_cb(on_pointer, NULL);
    static tb_app_t a;
    tb_clock_t now;

    for (int flip = 0; flip < 2; flip++) {
        /* A status screen: no tiles. Pressed samples while down, then the release, all at the logical point. */
        show(d, "busy", &a, &now, flip);
        tap(indev, flip, 200, 80);
        CHECK(s_got.n >= 2, "flip %d: %d samples", flip, s_got.n);
        CHECK(s_got.pressed[0] && !s_got.pressed[s_got.n - 1], "flip %d: press then release", flip);
        for (int i = 0; i < s_got.n; i++) {
            CHECK(s_got.x[i] == 200 && s_got.y[i] == 80, "flip %d: sample at %d,%d, want 200,80", flip, s_got.x[i], s_got.y[i]);
            CHECK(s_got.tile[i] == TB_TILE_NONE, "flip %d: tile %d without a menu", flip, s_got.tile[i]);
        }
        /* The quick menu's five tiles, each hit at its center; the gaps and the margin are no tile. */
        show(d, "menu_quick", &a, &now, flip);
        for (int t = 0; t < 5; t++) {
            tap(indev, flip, tile_cx(t, 5), 86);
            CHECK(s_got.n >= 2 && s_got.tile[s_got.n - 1] == t, "flip %d: tile %d released on tile %d", flip, t,
                  s_got.n ? s_got.tile[s_got.n - 1] : -9);
        }
        tap(indev, flip, 5, 5);
        CHECK(s_got.n >= 2 && s_got.tile[s_got.n - 1] == TB_TILE_NONE, "flip %d: corner is no tile", flip);
        tap(indev, flip, (tile_cx(0, 5) + tile_cx(1, 5)) / 2, 86);
        CHECK(s_got.n >= 2 && s_got.tile[s_got.n - 1] == TB_TILE_NONE, "flip %d: gap is no tile", flip);
        /* Three tiles (the power menu) */
        show(d, "menu_power", &a, &now, flip);
        tap(indev, flip, tile_cx(2, 3), 30);
        CHECK(s_got.n >= 2 && s_got.tile[s_got.n - 1] == 2, "flip %d: Back tile of three", flip);
    }

    /* A press LVGL abandons: TB_TILE_LOST with pressed = false. */
    show(d, "busy", &a, &now, false);
    s_got.n = 0;
    s_touch = (typeof(s_touch)){true, 100, 100};
    step(indev, 40);
    lv_indev_wait_release(indev);       /* LVGL abandons the press (as it does when something takes it over) */
    step(indev, 20);
    s_touch.down = false;
    step(indev, 20);
    bool lost = false;
    for (int i = 0; i < s_got.n; i++) lost |= !s_got.pressed[i] && s_got.tile[i] == TB_TILE_LOST;
    CHECK(lost, "press lost reported (%d samples)", s_got.n);

    /* Redraws touch only what changed: the same state again sends nothing to the panel, and the timer's next second
     * redraws the digits, not the info column. */
    show(d, "pomo_focus", &a, &now, false);
    s_flushed.px = 0;
    ui_update(&a, &now);
    lv_refr_now(d);
    CHECK(s_flushed.px == 0, "an unchanged screen sent %ld px", s_flushed.px);
    a.pomo.remaining_ms -= 1000;
    now.mono += 1000;
    s_tick += 1000;
    ui_update(&a, &now);
    lv_refr_now(d);
    CHECK(s_flushed.px > 0 && s_flushed.box.x2 < 448 && s_flushed.px < 404 * 120,
          "the next second sent %ld px in %d,%d..%d,%d", s_flushed.px, (int)s_flushed.box.x1, (int)s_flushed.box.y1,
          (int)s_flushed.box.x2, (int)s_flushed.box.y2);

    printf("touch_test: one timer second redraws %ld px (%d,%d to %d,%d)\n", s_flushed.px, (int)s_flushed.box.x1,
           (int)s_flushed.box.y1, (int)s_flushed.box.x2, (int)s_flushed.box.y2);
    printf("touch_test: %s\n", s_fail ? "FAILED" : "all checks passed");
    return s_fail ? 1 : 0;
}
