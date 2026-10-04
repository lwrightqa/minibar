/*
 * tb_gesture.h: turns raw touch samples into tap, swipe and hold, with the mock-up's thresholds
 * (the #screen pointerdown / pointermove / pointerup handlers).
 *
 * Owner: core builder. Pure C.
 *
 * Coordinates are LVGL's logical screen coordinates (640 x 172, already rotated by LVGL for the 180-degree flip),
 * so a swipe's direction is the same whichever way up the bar stands; there is no "if flipped, dx = -dx" here
 * (the mock-up needs that only because its DOM is rotated with CSS).
 */
#pragma once

#include "tb_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TB_GESTURE_HOLD_MS    550   /* holdTimer */
#define TB_GESTURE_SLOP_PX    10    /* moving more than this cancels the hold; 10 or more counts as "moved" */
#define TB_GESTURE_SWIPE_PX   40    /* |dx| > 40 is a swipe */

typedef enum {
    TB_GEST_NONE = 0,
    TB_GEST_DOWN,       /* finger down (used only to know a press started) */
    TB_GEST_TAP,        /* released without moving 10 px and before the hold fired */
    TB_GEST_MOVED_TAP,  /* released after moving 10..40 px: still a tap on a status screen, ignored on a menu */
    TB_GEST_SWIPE_NEXT, /* dx < -40: next status */
    TB_GEST_SWIPE_PREV, /* dx > 40: previous status */
    TB_GEST_HOLD,       /* 550 ms without moving more than 10 px; fires while the finger is still down */
    TB_GEST_CANCEL,     /* press lost */
} tb_gesture_t;

typedef struct {
    bool down;
    bool held;          /* the hold fired; the release does nothing */
    bool hold_armed;    /* cleared by moving more than the slop */
    int16_t x0, y0;     /* where the finger went down */
    int16_t x, y;       /* last position */
    tb_ms_t t0;
} tb_gesture_state_t;

void tb_gesture_reset(tb_gesture_state_t *g);
/* Feed one sample (pressed or released, position, time). Returns the gesture recognized by this sample, if any.
 * A release carries the point where the finger lifted (LVGL's last point). If the hold was due but no poll fired it
 * yet (a late sample), the release reports TB_GEST_HOLD instead of a tap, as the mock-up's 550 ms timer would have. */
tb_gesture_t tb_gesture_feed(tb_gesture_state_t *g, bool pressed, int16_t x, int16_t y, tb_ms_t now);
/* Call regularly while a finger is down (the app task's loop) so the hold fires on time without new samples. */
tb_gesture_t tb_gesture_poll(tb_gesture_state_t *g, tb_ms_t now);
/* The press was lost (LVGL's LV_EVENT_PRESS_LOST, the mock-up's pointercancel): forget it, recognize nothing.
 * Returns TB_GEST_CANCEL if a press was in progress, else TB_GEST_NONE. */
tb_gesture_t tb_gesture_cancel(tb_gesture_state_t *g);

#ifdef __cplusplus
}
#endif
