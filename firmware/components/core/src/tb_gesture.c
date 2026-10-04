/*
 * tb_gesture.c: tap, swipe and hold from raw touch samples. Owner: core builder.
 * Skeleton stub: recognizes nothing yet. Port the mock-up's #screen pointer handlers (see tb_gesture.h).
 */
#include <string.h>

#include "tb_gesture.h"

void tb_gesture_reset(tb_gesture_state_t *g)
{
    memset(g, 0, sizeof(*g));
}

tb_gesture_t tb_gesture_feed(tb_gesture_state_t *g, bool pressed, int16_t x, int16_t y, tb_ms_t now)
{
    (void)g;
    (void)pressed;
    (void)x;
    (void)y;
    (void)now;
    return TB_GEST_NONE;    /* TODO(core) */
}

tb_gesture_t tb_gesture_poll(tb_gesture_state_t *g, tb_ms_t now)
{
    (void)g;
    (void)now;
    return TB_GEST_NONE;    /* TODO(core) */
}
