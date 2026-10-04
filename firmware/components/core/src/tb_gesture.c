/*
 * tb_gesture.c: tap, swipe and hold from raw touch samples. Owner: core builder.
 *
 * A port of the mock-up's #screen pointer handlers:
 *   pointerdown   remember the point, arm a 550 ms hold timer
 *   pointermove   moving more than 10 px in x or y cancels the hold (strictly more than 10)
 *   pointerup     the hold already fired: nothing. Else dx = x - x0; moved = |dx| >= 10 or |dy| >= 10 (10 or more);
 *                 dx < -40 next status, dx > 40 previous status, otherwise a tap (a "moved" tap is ignored on a menu)
 *   pointercancel forget the press
 * Coordinates are logical (LVGL applies the 180-degree rotation), so there is no "if flipped, dx = -dx" here.
 */
#include <stdlib.h>
#include <string.h>

#include "tb_gesture.h"

void tb_gesture_reset(tb_gesture_state_t *g)
{
    memset(g, 0, sizeof(*g));
}

static bool beyond_slop(const tb_gesture_state_t *g, int16_t x, int16_t y)
{
    return abs(x - g->x0) > TB_GESTURE_SLOP_PX || abs(y - g->y0) > TB_GESTURE_SLOP_PX;
}

/* The hold timer: 550 ms after the press, unless the finger moved past the slop first. */
static bool hold_due(const tb_gesture_state_t *g, tb_ms_t now)
{
    return g->down && g->hold_armed && !g->held && now - g->t0 >= TB_GESTURE_HOLD_MS;
}

tb_gesture_t tb_gesture_feed(tb_gesture_state_t *g, bool pressed, int16_t x, int16_t y, tb_ms_t now)
{
    if (pressed) {
        if (!g->down) {
            /* pointerdown */
            g->down = true;
            g->held = false;
            g->hold_armed = true;
            g->x0 = g->x = x;
            g->y0 = g->y = y;
            g->t0 = now;
            return TB_GEST_DOWN;
        }
        /* The timer would have fired before this sample if it was due, so check it before the move. */
        if (hold_due(g, now)) {
            g->held = true;
            g->x = x;
            g->y = y;
            return TB_GEST_HOLD;
        }
        /* pointermove */
        if (beyond_slop(g, x, y)) g->hold_armed = false;
        g->x = x;
        g->y = y;
        return TB_GEST_NONE;
    }

    if (!g->down) return TB_GEST_NONE;
    /* pointerup. A hold that was due fires first (late sample); the release after a hold does nothing. */
    if (hold_due(g, now)) {
        g->held = true;
        g->down = false;
        return TB_GEST_HOLD;
    }
    g->down = false;
    g->x = x;
    g->y = y;
    if (g->held) return TB_GEST_NONE;
    int dx = x - g->x0, dy = y - g->y0;
    if (dx < -TB_GESTURE_SWIPE_PX) return TB_GEST_SWIPE_NEXT;
    if (dx > TB_GESTURE_SWIPE_PX) return TB_GEST_SWIPE_PREV;
    if (abs(dx) >= TB_GESTURE_SLOP_PX || abs(dy) >= TB_GESTURE_SLOP_PX) return TB_GEST_MOVED_TAP;
    return TB_GEST_TAP;
}

tb_gesture_t tb_gesture_poll(tb_gesture_state_t *g, tb_ms_t now)
{
    if (!hold_due(g, now)) return TB_GEST_NONE;
    g->held = true;
    return TB_GEST_HOLD;
}

tb_gesture_t tb_gesture_cancel(tb_gesture_state_t *g)
{
    bool was = g->down;
    g->down = false;
    g->hold_armed = false;
    return was ? TB_GEST_CANCEL : TB_GEST_NONE;
}
