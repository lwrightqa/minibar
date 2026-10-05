/*
 * snapshot.c: render MiniBar's screens on Linux and write one PNG per scene. Owner: ui builder.
 *
 *   tinybar_snapshot <out-dir> [scene ...]     every scene in host/ui_scenes.c, or the ones named
 *   tinybar_snapshot --list                    the scenes and what each shows
 *
 * Each scene is a tb_app_t put into a state through core's API (host/ui_scenes.c), drawn by the real ui code with
 * LVGL's software renderer into a 640 x 172 RGB565 frame, like the device's. A flipped scene is rendered with the
 * display turned 180 degrees and the frame turned in the flush, as board_display does on the device, so its PNG is
 * what the panel shows (upside down from the desk's point of view, and the right way up for someone reading the bar
 * flipped). Compare the PNGs with the mock-up (tools/ref_scenes.js renders the same scenes from docs/mockup.html).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include "png_write.h"
#include "tb_app.h"
#include "ui.h"
#include "ui_scenes.h"

#define W 640
#define H 172

static uint16_t s_fb[W * H];
static uint8_t s_draw[W * H * 2];
static bool s_flipped;

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) {
            uint16_t c = src[(y - a->y1) * w + (x - a->x1)];
            if (s_flipped) s_fb[(H - 1 - y) * W + (W - 1 - x)] = c;
            else s_fb[y * W + x] = c;
        }
    lv_display_flush_ready(d);
}

static uint32_t s_tick;
static uint32_t tick_cb(void)
{
    return s_tick;
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

static void draw(lv_display_t *d, const tb_app_t *a, const tb_clock_t *now)
{
    s_tick = (uint32_t)now->mono;
    ui_update(a, now);
    lv_obj_invalidate(lv_display_get_screen_active(d));
    lv_refr_now(d);
}

static void render(lv_display_t *d, const ui_scene_t *sc, const char *dir)
{
    static tb_app_t a;
    tb_clock_t now;
    sc->build(&a, &now);
    s_flipped = sc->flipped;
    lv_display_set_rotation(d, sc->flipped ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0);
    ui_refresh();
    draw(d, &a, &now);
    if (sc->anim_ms) {
        now.mono += sc->anim_ms;
        draw(d, &a, &now);
    }
    save(dir, sc->name);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--list")) {
        for (int i = 0; i < ui_scene_count; i++) printf("%-24s %s\n", ui_scenes[i].name, ui_scenes[i].about);
        return 0;
    }
    const char *dir = argc > 1 ? argv[1] : ".";
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, s_draw, NULL, sizeof s_draw, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, flush_cb);
    ui_init(d);

    int bad = 0;
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            const ui_scene_t *sc = ui_scene_find(argv[i]);
            if (!sc) {
                fprintf(stderr, "no scene \"%s\" (--list shows them)\n", argv[i]);
                bad = 1;
                continue;
            }
            render(d, sc, dir);
        }
    } else {
        for (int i = 0; i < ui_scene_count; i++) render(d, &ui_scenes[i], dir);
    }
    return bad;
}
