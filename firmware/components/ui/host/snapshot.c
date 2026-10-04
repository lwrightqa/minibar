/*
 * snapshot.c: render TinyBar's screens on Linux and write one PNG per scene. Owner: ui builder.
 *
 * Each scene is a tb_app_t put into a state through core's API (or, while core is a skeleton, by setting fields),
 * then drawn by the real ui code into a 640 x 172 RGB565 frame like the device's. Compare the PNGs with the mock-up
 * rendered in headless Chromium (docs/testing.md) at 640 x 172.
 * TODO(ui): a scene for every state in the mock-up (see ARCHITECTURE.md "Screens to snapshot").
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "png_write.h"
#include "tb_app.h"
#include "ui.h"

#define W 640
#define H 172

static uint16_t s_fb[W * H];
static uint8_t s_draw[W * H * 2];

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++)
        memcpy(&s_fb[y * W + a->x1], &src[(y - a->y1) * w], (size_t)w * 2);
    lv_display_flush_ready(d);
}

static void save(const char *dir, const char *name)
{
    uint8_t *rgb = malloc(W * H * 3);
    for (int i = 0; i < W * H; i++) {
        uint16_t c = s_fb[i];
        rgb[i * 3] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
        rgb[i * 3 + 1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
        rgb[i * 3 + 2] = (uint8_t)((c & 31) * 255 / 31);
    }
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
    if (!png_write_rgb(path, rgb, W, H)) fprintf(stderr, "can't write %s\n", path);
    else printf("%s\n", path);
    free(rgb);
}

static void render(const tb_app_t *a, const tb_clock_t *now, const char *dir, const char *name)
{
    ui_update(a, now);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    save(dir, name);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();
    lv_init();
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, s_draw, NULL, sizeof s_draw, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, flush_cb);
    ui_init(d);

    tb_settings_t s;
    tb_settings_defaults(&s, "f412fa3f2a1c");
    tb_clock_t now = {.mono = 10000, .wall = 1791148920 /* 2026-10-04 14:22 PDT */, .valid = true};
    static tb_app_t a;
    tb_app_init(&a, &s, true, &now);
    a.booting = false;

    static const struct { tb_status_t st; const char *name; } scenes[] = {
        {TB_ST_AVAILABLE, "available"}, {TB_ST_BUSY, "busy"}, {TB_ST_MEETING, "meeting"},
        {TB_ST_POMODORO, "pomodoro"},   {TB_ST_AWAY, "away"}, {TB_ST_MESSAGE, "message"}, {TB_ST_CLOCK, "clock"},
    };
    for (size_t i = 0; i < sizeof scenes / sizeof scenes[0]; i++) {
        a.idx = scenes[i].st;
        a.rev++;
        render(&a, &now, dir, scenes[i].name);
    }
    return 0;
}
