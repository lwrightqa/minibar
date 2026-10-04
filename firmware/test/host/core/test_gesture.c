/* Gesture recognizer: the mock-up's #screen pointer handlers (hold 550 ms, slop 10 px, swipe |dx| > 40). */
#include "tb_gesture.h"
#include "tb_test.h"

static tb_gesture_state_t g;

static tb_gesture_t press_release(int dx, int dy, tb_ms_t dur)
{
    tb_gesture_reset(&g);
    TB_EQ_INT(tb_gesture_feed(&g, true, 100, 50, 1000), TB_GEST_DOWN);
    tb_gesture_feed(&g, true, (int16_t)(100 + dx / 2), (int16_t)(50 + dy / 2), 1000 + dur / 2);
    return tb_gesture_feed(&g, false, (int16_t)(100 + dx), (int16_t)(50 + dy), 1000 + dur);
}

TB_TEST(gesture_tap_and_moved_tap)
{
    TB_EQ_INT(press_release(0, 0, 80), TB_GEST_TAP);
    TB_EQ_INT(press_release(9, 9, 80), TB_GEST_TAP);
    TB_EQ_INT(press_release(10, 0, 80), TB_GEST_MOVED_TAP);     /* moved = |dx| >= 10 */
    TB_EQ_INT(press_release(0, -10, 80), TB_GEST_MOVED_TAP);
    TB_EQ_INT(press_release(0, 60, 80), TB_GEST_MOVED_TAP);     /* vertical is never a swipe */
    TB_EQ_INT(press_release(40, 0, 80), TB_GEST_MOVED_TAP);     /* |dx| > 40 to swipe */
    TB_EQ_INT(press_release(-40, 0, 80), TB_GEST_MOVED_TAP);
}

TB_TEST(gesture_swipes)
{
    TB_EQ_INT(press_release(-41, 0, 120), TB_GEST_SWIPE_NEXT);   /* dx < -40: next status */
    TB_EQ_INT(press_release(41, 0, 120), TB_GEST_SWIPE_PREV);    /* dx > 40: previous status */
    TB_EQ_INT(press_release(-200, 30, 300), TB_GEST_SWIPE_NEXT);
}

TB_TEST(gesture_hold_fires_at_550_from_poll)
{
    tb_gesture_reset(&g);
    tb_gesture_feed(&g, true, 100, 50, 1000);
    TB_EQ_INT(tb_gesture_poll(&g, 1549), TB_GEST_NONE);
    TB_EQ_INT(tb_gesture_poll(&g, 1550), TB_GEST_HOLD);
    TB_EQ_INT(tb_gesture_poll(&g, 1600), TB_GEST_NONE);          /* once */
    TB_EQ_INT(tb_gesture_feed(&g, false, 100, 50, 1700), TB_GEST_NONE);   /* the release after a hold does nothing */
}

TB_TEST(gesture_slop_cancels_hold)
{
    tb_gesture_reset(&g);
    tb_gesture_feed(&g, true, 100, 50, 1000);
    tb_gesture_feed(&g, true, 110, 60, 1100);                    /* exactly 10: the hold stays armed */
    TB_EQ_INT(tb_gesture_poll(&g, 1560), TB_GEST_HOLD);

    tb_gesture_reset(&g);
    tb_gesture_feed(&g, true, 100, 50, 1000);
    tb_gesture_feed(&g, true, 111, 50, 1100);                    /* 11 px: canceled, even after moving back */
    tb_gesture_feed(&g, true, 100, 50, 1200);
    TB_EQ_INT(tb_gesture_poll(&g, 1700), TB_GEST_NONE);
    TB_EQ_INT(tb_gesture_feed(&g, false, 100, 50, 1800), TB_GEST_TAP);   /* released where it started: a tap */
}

TB_TEST(gesture_late_release_is_the_hold)
{
    /* No poll ran, but the 550 ms timer would have fired before the finger lifted. */
    tb_gesture_reset(&g);
    tb_gesture_feed(&g, true, 100, 50, 1000);
    TB_EQ_INT(tb_gesture_feed(&g, false, 100, 50, 1600), TB_GEST_HOLD);
    /* And a pressed sample after 550 ms reports it too. */
    tb_gesture_reset(&g);
    tb_gesture_feed(&g, true, 100, 50, 1000);
    TB_EQ_INT(tb_gesture_feed(&g, true, 102, 50, 1560), TB_GEST_HOLD);
    TB_EQ_INT(tb_gesture_feed(&g, false, 102, 50, 1600), TB_GEST_NONE);
}

TB_TEST(gesture_cancel)
{
    tb_gesture_reset(&g);
    TB_EQ_INT(tb_gesture_cancel(&g), TB_GEST_NONE);
    tb_gesture_feed(&g, true, 100, 50, 1000);
    TB_EQ_INT(tb_gesture_cancel(&g), TB_GEST_CANCEL);
    TB_EQ_INT(tb_gesture_poll(&g, 2000), TB_GEST_NONE);
    TB_EQ_INT(tb_gesture_feed(&g, false, 100, 50, 2100), TB_GEST_NONE);  /* nothing to release */
}
