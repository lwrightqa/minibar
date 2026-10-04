/*
 * brd_inputs.c: pure logic for the backlight curve, the button debouncer, flip detection and the touch report.
 * Owner: board builder. No ESP-IDF headers (see brd_logic.h).
 */
#include <math.h>
#include <stdlib.h>

#include "brd_logic.h"

/* ---------- Backlight ---------- */

float brd_bl_level(uint8_t percent)
{
    if (percent == 0) return 0.0f;
    if (percent > 100) percent = 100;
    float b = 0.45f + 0.55f * (float)percent / 100.0f;   /* the mock-up's CSS brightness factor */
    return powf(b, 2.2f);                                /* as luminance */
}

uint8_t brd_bl_duty(uint8_t percent, uint8_t zero_duty)
{
    if (percent == 0) return BRD_BL_DUTY_MAX;           /* pin always high: no current at all */
    float high = (float)zero_duty * (1.0f - brd_bl_level(percent));
    long d = lroundf(high);
    if (d < 0) d = 0;
    if (d > BRD_BL_DUTY_MAX) d = BRD_BL_DUTY_MAX;
    return (uint8_t)d;
}

/* ---------- Buttons ---------- */

void brd_debounce_init(brd_debounce_t *d, bool pressed_now)
{
    d->pressed = pressed_now;
    d->suppress = pressed_now;
    d->run = 0;
}

brd_edge_t brd_debounce_feed(brd_debounce_t *d, bool raw_pressed)
{
    if (raw_pressed == d->pressed) {
        d->run = 0;
        return BRD_EDGE_NONE;
    }
    if (++d->run < BRD_DEBOUNCE_SAMPLES) return BRD_EDGE_NONE;
    d->run = 0;
    d->pressed = raw_pressed;
    if (d->suppress) {
        /* Held since init: its release ends the suppression and is not reported. */
        if (!raw_pressed) d->suppress = false;
        return BRD_EDGE_NONE;
    }
    return raw_pressed ? BRD_EDGE_DOWN : BRD_EDGE_UP;
}

/* ---------- Orientation ---------- */

int32_t brd_orient_along(brd_up_axis_t up, int32_t ax_mg, int32_t ay_mg)
{
    switch (up) {
    case BRD_UP_X_POS: return ax_mg;
    case BRD_UP_X_NEG: return -ax_mg;
    case BRD_UP_Y_POS: return ay_mg;
    case BRD_UP_Y_NEG: return -ay_mg;
    }
    return 0;
}

static bool about_one_g(int32_t ax, int32_t ay, int32_t az)
{
    int64_t m2 = (int64_t)ax * ax + (int64_t)ay * ay + (int64_t)az * az;
    return m2 >= (int64_t)BRD_OR_MAG_MIN_MG * BRD_OR_MAG_MIN_MG && m2 <= (int64_t)BRD_OR_MAG_MAX_MG * BRD_OR_MAG_MAX_MG;
}

int brd_orient_classify(brd_up_axis_t up, int32_t ax_mg, int32_t ay_mg, int32_t az_mg, int32_t threshold_mg)
{
    if (!about_one_g(ax_mg, ay_mg, az_mg)) return -1;
    int32_t along = brd_orient_along(up, ax_mg, ay_mg);
    if (along >= threshold_mg) return 0;
    if (along <= -threshold_mg) return 1;
    return -1;
}

void brd_orient_init(brd_orient_t *o, brd_up_axis_t up, bool flipped_now, int64_t now_ms)
{
    o->up = up;
    o->flipped = flipped_now;
    o->initial_sent = false;
    o->start_ms = now_ms;
    o->vote = -1;
    o->vote_since_ms = now_ms;
    o->last_along = 0;
    o->have_last = false;
}

brd_or_event_t brd_orient_feed(brd_orient_t *o, int32_t ax_mg, int32_t ay_mg, int32_t az_mg, int64_t now_ms,
                               bool *flipped)
{
    int32_t along = brd_orient_along(o->up, ax_mg, ay_mg);
    int vote = brd_orient_classify(o->up, ax_mg, ay_mg, az_mg, BRD_OR_ENTER_MG);
    bool jumped = o->have_last && labs((long)along - (long)o->last_along) > BRD_OR_JITTER_MG;
    o->last_along = along;
    o->have_last = true;

    if (vote < 0) {
        o->vote = -1;
    } else if (vote != o->vote || jumped) {
        o->vote = (int8_t)vote;
        o->vote_since_ms = now_ms;      /* a new vote, or a jolt: start the steadiness window again */
    }
    bool steady = o->vote >= 0 && now_ms - o->vote_since_ms >= BRD_OR_STEADY_MS;

    if (!o->initial_sent) {
        if (steady) {
            o->flipped = o->vote == 1;
        } else if (now_ms - o->start_ms < BRD_OR_INITIAL_MS) {
            return BRD_OR_NONE;
        }
        o->initial_sent = true;
        *flipped = o->flipped;
        return BRD_OR_INITIAL;
    }
    if (steady && (o->vote == 1) != o->flipped) {
        o->flipped = o->vote == 1;
        *flipped = o->flipped;
        return BRD_OR_CHANGED;
    }
    return BRD_OR_NONE;
}

int32_t brd_qmi_raw_to_mg(int16_t raw)
{
    /* +-2 g full scale: 16384 LSB per g. Round to nearest. */
    int32_t v = (int32_t)raw * 1000;
    return v >= 0 ? (v + 8192) / 16384 : -((-v + 8192) / 16384);
}

/* ---------- Touch ---------- */

bool brd_touch_decode(const uint8_t *buf, size_t len, int16_t *x, int16_t *y)
{
    if (!buf || len < 6) return false;
    uint8_t points = buf[1];
    if (points == 0 || points > 4) return false;
    uint16_t raw_x = (uint16_t)(((buf[2] & 0x0f) << 8) | buf[3]);  /* along the 640 px side */
    uint16_t raw_y = (uint16_t)(((buf[4] & 0x0f) << 8) | buf[5]);  /* along the 172 px side */
    if (raw_x > BRD_PANEL_H - 1) raw_x = BRD_PANEL_H - 1;
    if (raw_y > BRD_PANEL_W - 1) raw_y = BRD_PANEL_W - 1;
    *x = (int16_t)raw_y;
    *y = (int16_t)(BRD_PANEL_H - 1 - raw_x);
    return true;
}
