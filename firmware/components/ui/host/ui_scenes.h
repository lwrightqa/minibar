/*
 * ui_scenes.h: named states of the bar, for the snapshot tool and the view tests. Owner: ui builder.
 *
 * Each scene starts a fresh tb_app_t (defaults, Wi-Fi joined, past the splash) at the reference time, Sunday
 * 2026-10-04 2:04:00 PM in US Pacific time (the caller sets TZ), and puts it in a state through core's API, setting
 * fields directly only where no input reaches them (the Pomodoro's exact remaining time, how long ago you picked a
 * status). The mock-up side of the comparison (tools/ref_scenes.js) builds the same scenes in docs/mockup.html.
 *
 * Pure C (core only), so it builds into the host view library and the test runner can use it.
 */
#pragma once

#include "tb_app.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_SCENE_WALL 1791147840    /* 2026-10-04 14:04:00 PDT */

typedef struct {
    const char *name;
    const char *about;
    void (*build)(tb_app_t *a, tb_clock_t *now);
    tb_ms_t anim_ms;        /* render once more this long after the first frame (marquee position) */
    bool flipped;           /* render with the display turned 180 degrees */
} ui_scene_t;

extern const ui_scene_t ui_scenes[];
extern const int ui_scene_count;

const ui_scene_t *ui_scene_find(const char *name);

#ifdef __cplusplus
}
#endif
