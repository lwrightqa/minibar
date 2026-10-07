/*
 * test_board.c: host tests for the board's pure logic (components/board/logic). Owner: board builder.
 *
 * What these prove on Linux: the arithmetic and state machines. What they can't: anything about the real board
 * (the backlight's dark duty, the IMU's axis, the touch controller's ranges, how loud the speaker is). Those are in
 * components/board/README.md's bring-up checklist.
 */
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "brd_logic.h"
#include "tb_test.h"

#define TB_NEAR(a, b, tol)                                                                  \
    do {                                                                                    \
        double _a = (double)(a), _b = (double)(b);                                          \
        if (fabs(_a - _b) > (tol)) TB_FAIL_AT("%s ~= %s: %.3f vs %.3f", #a, #b, _a, _b);    \
    } while (0)

/* ================= Backlight ================= */

TB_TEST(bl_off_is_fully_dark)
{
    TB_EQ_INT(brd_bl_duty(0, BRD_BL_ZERO_DUTY), 255);
    TB_NEAR(brd_bl_level(0), 0.0, 1e-9);
}

TB_TEST(bl_full_is_pin_low)
{
    /* 100%: the mock-up's factor is 1.0, so full current: the pin stays low (Waveshare's LCD_PWM_MODE_255). */
    TB_EQ_INT(brd_bl_duty(100, BRD_BL_ZERO_DUTY), 0);
    TB_NEAR(brd_bl_level(100), 1.0, 1e-6);
    TB_EQ_INT(brd_bl_duty(200, BRD_BL_ZERO_DUTY), 0);   /* clamped */
}

TB_TEST(bl_light_tile_levels_match_the_mockup)
{
    /* Light tile 40, 70, 100%: the mock-up draws them at brightness 0.67, 0.835 and 1.0 (0.45 + 0.55 p), which is
     * luminance 0.41, 0.67 and 1.0. */
    TB_NEAR(brd_bl_level(40), pow(0.67, 2.2), 1e-4);
    TB_NEAR(brd_bl_level(70), pow(0.835, 2.2), 1e-4);
    TB_NEAR(brd_bl_level(40), 0.414, 0.002);
    TB_NEAR(brd_bl_level(70), 0.673, 0.002);
    /* Duty: 164 * (1 - L). */
    TB_EQ_INT(brd_bl_duty(40, 164), 96);
    TB_EQ_INT(brd_bl_duty(70, 164), 54);
}

TB_TEST(bl_monotonic_and_never_dark_when_on)
{
    uint8_t prev = 255;
    for (int p = 1; p <= 100; p++) {
        uint8_t d = brd_bl_duty((uint8_t)p, BRD_BL_ZERO_DUTY);
        TB_TRUE(d <= prev);
        TB_TRUE(d < BRD_BL_ZERO_DUTY);      /* any brightness above 0 keeps some current */
        prev = d;
    }
    /* 1% still gives the mock-up's floor: 0.4555^2.2 = 18% of full current. */
    TB_NEAR(brd_bl_level(1), 0.177, 0.003);
}

TB_TEST(bl_zero_duty_scales_the_curve)
{
    /* If the board's dark point turns out to be 100% (a plain linear dimmer), the same curve still works. */
    TB_EQ_INT(brd_bl_duty(100, 255), 0);
    TB_EQ_INT(brd_bl_duty(40, 255), 149);
}

/* ================= Buttons ================= */

static int feed_n(brd_debounce_t *d, bool level, int n, brd_edge_t *last)
{
    int edges = 0;
    for (int i = 0; i < n; i++) {
        brd_edge_t e = brd_debounce_feed(d, level);
        if (e != BRD_EDGE_NONE) {
            edges++;
            *last = e;
        }
    }
    return edges;
}

TB_TEST(button_press_and_release_after_20ms)
{
    brd_debounce_t d;
    brd_debounce_init(&d, false);
    brd_edge_t e = BRD_EDGE_NONE;
    TB_EQ_INT(feed_n(&d, true, 3, &e), 0);              /* 15 ms: not yet */
    TB_EQ_INT(brd_debounce_feed(&d, true), BRD_EDGE_DOWN); /* 20 ms */
    TB_EQ_INT(feed_n(&d, true, 100, &e), 0);            /* holding: nothing more */
    TB_EQ_INT(feed_n(&d, false, 3, &e), 0);
    TB_EQ_INT(brd_debounce_feed(&d, false), BRD_EDGE_UP);
}

TB_TEST(button_bounce_is_ignored)
{
    brd_debounce_t d;
    brd_debounce_init(&d, false);
    brd_edge_t e = BRD_EDGE_NONE;
    /* Contact bounce: short pulses never reach 4 samples in a row. */
    for (int i = 0; i < 20; i++) {
        TB_EQ_INT(feed_n(&d, true, 2, &e), 0);
        TB_EQ_INT(feed_n(&d, false, 1, &e), 0);
    }
    TB_FALSE(d.pressed);
    /* A bouncy press settles into one DOWN. */
    TB_EQ_INT(feed_n(&d, true, 1, &e) + feed_n(&d, false, 1, &e) + feed_n(&d, true, 10, &e), 1);
    TB_EQ_INT(e, BRD_EDGE_DOWN);
}

TB_TEST(button_held_at_boot_is_suppressed_until_released)
{
    /* PWR pressed to wake the bar: its hold and release are not events, the next press is. */
    brd_debounce_t d;
    brd_debounce_init(&d, true);
    brd_edge_t e = BRD_EDGE_NONE;
    TB_EQ_INT(feed_n(&d, true, 600, &e), 0);            /* held 3 s: still nothing (no power off) */
    TB_EQ_INT(feed_n(&d, false, 10, &e), 0);            /* its release isn't reported */
    TB_FALSE(d.suppress);
    TB_EQ_INT(feed_n(&d, true, 10, &e), 1);
    TB_EQ_INT(e, BRD_EDGE_DOWN);
    TB_EQ_INT(feed_n(&d, false, 10, &e), 1);
    TB_EQ_INT(e, BRD_EDGE_UP);
}

TB_TEST(button_released_at_boot_works_at_once)
{
    brd_debounce_t d;
    brd_debounce_init(&d, false);
    brd_edge_t e = BRD_EDGE_NONE;
    TB_EQ_INT(feed_n(&d, true, 4, &e), 1);
    TB_EQ_INT(e, BRD_EDGE_DOWN);
}

/* ================= Orientation ================= */

/* Feed a constant sample every 40 ms for ms milliseconds; returns the first event and its time. */
static brd_or_event_t feed_for(brd_orient_t *o, int64_t *t, int ms, int32_t ax, int32_t ay, int32_t az, bool *flipped,
                               int64_t *at)
{
    brd_or_event_t first = BRD_OR_NONE;
    for (int i = 0; i < ms / BRD_OR_PERIOD_MS; i++) {
        *t += BRD_OR_PERIOD_MS;
        bool f = false;
        brd_or_event_t ev = brd_orient_feed(o, ax, ay, az, *t, &f);
        if (ev != BRD_OR_NONE && first == BRD_OR_NONE) {
            first = ev;
            *flipped = f;
            if (at) *at = *t;
        }
    }
    return first;
}

TB_TEST(orient_axis_and_sign)
{
    TB_EQ_INT(brd_orient_along(BRD_UP_X_POS, 900, 10), 900);
    TB_EQ_INT(brd_orient_along(BRD_UP_X_NEG, 900, 10), -900);
    TB_EQ_INT(brd_orient_along(BRD_UP_Y_POS, 10, -950), -950);
    TB_EQ_INT(brd_orient_along(BRD_UP_Y_NEG, 10, -950), 950);
    /* Leaning back 20 degrees on the desk: 0.94 g along the axis, 0.34 g through the screen. */
    TB_EQ_INT(brd_orient_classify(BRD_UP_Y_POS, 0, 940, 342, BRD_OR_ENTER_MG), 0);
    TB_EQ_INT(brd_orient_classify(BRD_UP_Y_POS, 0, -940, 342, BRD_OR_ENTER_MG), 1);
    /* Lying flat: can't tell. */
    TB_EQ_INT(brd_orient_classify(BRD_UP_Y_POS, 20, 30, 1000, BRD_OR_ENTER_MG), -1);
    /* Standing on a short edge: gravity along the other axis: can't tell. */
    TB_EQ_INT(brd_orient_classify(BRD_UP_Y_POS, 1000, 0, 0, BRD_OR_ENTER_MG), -1);
    /* Being swung (1.6 g): can't tell. */
    TB_EQ_INT(brd_orient_classify(BRD_UP_Y_POS, 0, 1600, 0, BRD_OR_ENTER_MG), -1);
    /* The boot reading takes a weaker tilt (350 mg) than a flip does (600 mg). */
    TB_EQ_INT(brd_orient_classify(BRD_UP_Y_POS, 0, -450, 890, BRD_OR_BOOT_MG), 1);
    TB_EQ_INT(brd_orient_classify(BRD_UP_Y_POS, 0, -450, 890, BRD_OR_ENTER_MG), -1);
}

TB_TEST(orient_raw_to_mg)
{
    TB_EQ_INT(brd_qmi_raw_to_mg(16384), 1000);
    TB_EQ_INT(brd_qmi_raw_to_mg(-16384), -1000);
    TB_EQ_INT(brd_qmi_raw_to_mg(0), 0);
    TB_EQ_INT(brd_qmi_raw_to_mg(8192), 500);
    TB_EQ_INT(brd_qmi_raw_to_mg(32767), 2000);
    TB_EQ_INT(brd_qmi_raw_to_mg(-32768), -2000);
}

TB_TEST(orient_initial_after_half_a_second_steady)
{
    brd_orient_t o;
    int64_t t = 1000, at = 0;
    brd_orient_init(&o, BRD_UP_Y_POS, false, t);
    bool f = true;
    TB_EQ_INT(feed_for(&o, &t, 400, 0, 980, 150, &f, &at), BRD_OR_NONE);
    TB_EQ_INT(feed_for(&o, &t, 400, 0, 980, 150, &f, &at), BRD_OR_INITIAL);
    TB_FALSE(f);
    TB_TRUE(at - 1000 >= 500 && at - 1000 <= 600);
    /* Nothing more while it stays put. */
    TB_EQ_INT(feed_for(&o, &t, 5000, 0, 980, 150, &f, NULL), BRD_OR_NONE);
}

TB_TEST(orient_initial_corrects_a_wrong_boot_guess)
{
    /* main guessed upright (say the bar was being set down), but it settles upside down: INITIAL says flipped. */
    brd_orient_t o;
    int64_t t = 0;
    brd_orient_init(&o, BRD_UP_Y_POS, false, t);
    bool f = false;
    TB_EQ_INT(feed_for(&o, &t, 800, 0, -990, 100, &f, NULL), BRD_OR_INITIAL);
    TB_TRUE(f);
}

TB_TEST(orient_initial_falls_back_after_two_seconds)
{
    /* Lying flat at boot: no steady vote, so after 2 s INITIAL confirms what main started with. */
    brd_orient_t o;
    int64_t t = 0, at = 0;
    brd_orient_init(&o, BRD_UP_Y_POS, true, t);
    bool f = false;
    TB_EQ_INT(feed_for(&o, &t, 3000, 10, 20, 1000, &f, &at), BRD_OR_INITIAL);
    TB_TRUE(f);
    TB_TRUE(at >= 2000 && at <= 2040);
}

TB_TEST(orient_flip_needs_half_a_second_and_reports_once)
{
    brd_orient_t o;
    int64_t t = 0, at = 0;
    brd_orient_init(&o, BRD_UP_Y_POS, false, t);
    bool f = false;
    TB_EQ_INT(feed_for(&o, &t, 800, 0, 980, 150, &f, NULL), BRD_OR_INITIAL);
    int64_t turned = t;
    TB_EQ_INT(feed_for(&o, &t, 1000, 0, -980, 150, &f, &at), BRD_OR_CHANGED);
    TB_TRUE(f);
    TB_TRUE(at - turned >= 500 && at - turned <= 560);
    TB_EQ_INT(feed_for(&o, &t, 3000, 0, -980, 150, &f, NULL), BRD_OR_NONE);
    /* And back. */
    TB_EQ_INT(feed_for(&o, &t, 1000, 0, 970, 200, &f, NULL), BRD_OR_CHANGED);
    TB_FALSE(f);
}

TB_TEST(orient_brief_turn_does_not_flip)
{
    brd_orient_t o;
    int64_t t = 0;
    brd_orient_init(&o, BRD_UP_Y_POS, false, t);
    bool f = false;
    feed_for(&o, &t, 800, 0, 980, 150, &f, NULL);
    /* Upside down for 0.4 s, then back: nothing. */
    TB_EQ_INT(feed_for(&o, &t, 400, 0, -980, 150, &f, NULL), BRD_OR_NONE);
    TB_EQ_INT(feed_for(&o, &t, 2000, 0, 980, 150, &f, NULL), BRD_OR_NONE);
}

TB_TEST(orient_hysteresis_dead_band)
{
    brd_orient_t o;
    int64_t t = 0;
    brd_orient_init(&o, BRD_UP_Y_POS, false, t);
    bool f = false;
    feed_for(&o, &t, 800, 0, 980, 150, &f, NULL);
    /* Tipped just past level (-0.5 g along the axis): still inside the dead band, so no flip however long. */
    TB_EQ_INT(feed_for(&o, &t, 5000, 0, -500, 866, &f, NULL), BRD_OR_NONE);
    /* Lying flat on its back: no flip. */
    TB_EQ_INT(feed_for(&o, &t, 5000, 0, 0, 1000, &f, NULL), BRD_OR_NONE);
    /* Past -0.6 g: flips. */
    TB_EQ_INT(feed_for(&o, &t, 1000, 0, -700, 714, &f, NULL), BRD_OR_CHANGED);
    TB_TRUE(f);
}

TB_TEST(orient_carrying_does_not_flip)
{
    /* Upside down but shaken (total swinging 0.6..1.4 g every other sample): never steady. */
    brd_orient_t o;
    int64_t t = 0;
    brd_orient_init(&o, BRD_UP_Y_POS, false, t);
    bool f = false;
    feed_for(&o, &t, 800, 0, 980, 150, &f, NULL);
    int events = 0;
    for (int i = 0; i < 100; i++) {
        t += BRD_OR_PERIOD_MS;
        int32_t ay = (i % 2) ? -1400 : -600;
        if (brd_orient_feed(&o, 0, ay, 100, t, &f) != BRD_OR_NONE) events++;
    }
    TB_EQ_INT(events, 0);
    /* Upside down with jolts of 200 mg along the axis every 280 ms: the window keeps restarting. */
    for (int i = 0; i < 100; i++) {
        t += BRD_OR_PERIOD_MS;
        int32_t ay = (i % 7 == 0) ? -760 : -960;
        if (brd_orient_feed(&o, 0, ay, 100, t, &f) != BRD_OR_NONE) events++;
    }
    TB_EQ_INT(events, 0);
}

TB_TEST(orient_other_axes)
{
    /* Every axis setting works the same way. */
    const brd_up_axis_t axes[4] = {BRD_UP_X_POS, BRD_UP_X_NEG, BRD_UP_Y_POS, BRD_UP_Y_NEG};
    for (int k = 0; k < 4; k++) {
        int32_t ux = k == 0 ? 990 : k == 1 ? -990 : 0;
        int32_t uy = k == 2 ? 990 : k == 3 ? -990 : 0;
        brd_orient_t o;
        int64_t t = 0;
        brd_orient_init(&o, axes[k], false, t);
        bool f = true;
        TB_EQ_INT(feed_for(&o, &t, 800, ux, uy, 100, &f, NULL), BRD_OR_INITIAL);
        TB_FALSE(f);
        TB_EQ_INT(feed_for(&o, &t, 800, -ux, -uy, 100, &f, NULL), BRD_OR_CHANGED);
        TB_TRUE(f);
    }
}

/* ================= Sound ================= */

TB_TEST(chime_levels_match_the_mockup)
{
    size_t n = brd_chime_samples(BRD_AUDIO_RATE);
    TB_EQ_INT(n, 32640);
    int16_t *up = malloc(n * sizeof *up), *down = malloc(n * sizeof *down);
    brd_chime_render(up, n, true, BRD_AUDIO_RATE);
    brd_chime_render(down, n, false, BRD_AUDIO_RATE);
    /* Mock-up chime(): about -13 dBFS (measured -13.1 for its Web Audio graph). */
    TB_NEAR(brd_peak_dbfs(up, n), -13.1, 0.3);
    TB_NEAR(brd_peak_dbfs(down, n), -13.1, 0.3);
    /* Starts from silence (the attack), no click at the end. */
    TB_TRUE(abs(up[0]) == 0);
    TB_TRUE(abs(up[n - 1]) < 10);
    TB_TRUE(abs(down[n - 1]) < 10);
    free(up);
    free(down);
}

/* The dominant frequency of a stretch, by zero crossings. */
static double crossings_hz(const int16_t *p, size_t from, size_t len, uint32_t rate)
{
    int zc = 0;
    for (size_t i = from + 1; i < from + len; i++)
        if ((p[i - 1] < 0) != (p[i] < 0)) zc++;
    return zc / 2.0 * rate / (double)len;
}

TB_TEST(chime_note_order)
{
    size_t n = brd_chime_samples(BRD_AUDIO_RATE);
    int16_t *up = malloc(n * sizeof *up), *down = malloc(n * sizeof *down);
    brd_chime_render(up, n, true, BRD_AUDIO_RATE);
    brd_chime_render(down, n, false, BRD_AUDIO_RATE);
    /* The first 0.18 s is the first note alone: 784 Hz rising to a break, 1175 Hz falling back to focus. */
    TB_NEAR(crossings_hz(up, 480, 3600, BRD_AUDIO_RATE), 784, 15);
    TB_NEAR(crossings_hz(down, 480, 3600, BRD_AUDIO_RATE), 1175, 15);
    free(up);
    free(down);
}

TB_TEST(tick_levels_match_the_mockup)
{
    size_t n = brd_tick_samples(BRD_AUDIO_RATE);
    TB_EQ_INT(n, 960);
    int16_t a[960], b[960];
    brd_tick_render(a, n, 2000, BRD_AUDIO_RATE);
    brd_tick_render(b, n, 1700, BRD_AUDIO_RATE);
    TB_NEAR(brd_peak_dbfs(a, n), -10.8, 0.1);
    TB_NEAR(brd_peak_dbfs(b, n), -10.8, 0.1);
    /* Deterministic: the same pitch renders the same samples. */
    int16_t a2[960];
    brd_tick_render(a2, n, 2000, BRD_AUDIO_RATE);
    TB_TRUE(memcmp(a, a2, sizeof a) == 0);
    /* It's a click: nearly all its energy is in the first 25 ms. */
    double e_all = 0, e_tail = 0;
    for (size_t i = 0; i < n; i++) {
        double v = (double)a[i] * a[i];
        e_all += v;
        if (i >= 600) e_tail += v;
    }
    TB_TRUE(e_tail < e_all * 0.01);
}

/* ================= Player ================= */

typedef struct {
    int16_t *chime_up, *chime_down, tick_a[960], tick_b[960];
    size_t chime_len, tick_len;
} sounds_t;

static void sounds_init(sounds_t *s)
{
    s->chime_len = brd_chime_samples(BRD_AUDIO_RATE);
    s->chime_up = malloc(s->chime_len * sizeof(int16_t));
    s->chime_down = malloc(s->chime_len * sizeof(int16_t));
    brd_chime_render(s->chime_up, s->chime_len, true, BRD_AUDIO_RATE);
    brd_chime_render(s->chime_down, s->chime_len, false, BRD_AUDIO_RATE);
    s->tick_len = brd_tick_samples(BRD_AUDIO_RATE);
    brd_tick_render(s->tick_a, s->tick_len, 2000, BRD_AUDIO_RATE);
    brd_tick_render(s->tick_b, s->tick_len, 1700, BRD_AUDIO_RATE);
}

static void sounds_free(sounds_t *s)
{
    free(s->chime_up);
    free(s->chime_down);
}

/* Run the player like the audio task: 10 ms chunks of 240 samples, time advancing by what was written (or 10 ms of
 * silence). Collects everything into out (n samples, one per 1/24000 s from t0). */
static void run_player(brd_player_t *p, int64_t t0, int16_t *out, size_t n)
{
    size_t at = 0;
    while (at < n) {
        int64_t now = t0 + (int64_t)at * 1000 / BRD_AUDIO_RATE;
        size_t want = n - at < 240 ? n - at : 240;
        size_t got = brd_player_fill(p, now, out + at, want);
        if (got < want) memset(out + at + got, 0, (want - got) * sizeof *out);
        at += want;
    }
}

TB_TEST(player_ticks_once_a_second_at_the_right_level)
{
    sounds_t s;
    sounds_init(&s);
    brd_player_t p;
    brd_player_init(&p, s.chime_down, s.chime_up, s.chime_len, s.tick_a, s.tick_b, s.tick_len, 0);
    TB_EQ_INT(brd_player_wait_ms(&p, 0), -1);
    TB_FALSE(brd_player_amp_wanted(&p, 0));
    brd_player_set_ticking(&p, 1, 0);
    TB_TRUE(brd_player_amp_wanted(&p, 0));
    TB_EQ_INT(brd_player_wait_ms(&p, 0), 1000);

    size_t n = 5 * BRD_AUDIO_RATE;
    int16_t *out = calloc(n, sizeof *out);
    run_player(&p, 0, out, n);
    /* Soft: -16.0 dBFS. Ticks start at 1, 2, 3, 4 s and nothing in between. */
    TB_NEAR(brd_peak_dbfs(out, n), -16.0, 0.2);
    int ticks = 0;
    for (size_t sec = 0; sec < 5; sec++) {
        float pk = brd_peak_dbfs(out + sec * BRD_AUDIO_RATE, 960);
        if (pk > -40) ticks++;
        TB_TRUE(brd_peak_dbfs(out + sec * BRD_AUDIO_RATE + 1200, BRD_AUDIO_RATE - 1200) < -100);
    }
    TB_EQ_INT(ticks, 4);
    TB_TRUE(brd_peak_dbfs(out, 960) < -100);    /* not at 0 s */

    /* Medium: -10.0 dBFS. */
    brd_player_set_ticking(&p, 2, 5000);
    run_player(&p, 5000, out, n);
    TB_NEAR(brd_peak_dbfs(out, n), -10.0, 0.2);
    free(out);
    sounds_free(&s);
}

TB_TEST(player_alternates_tick_pitch)
{
    sounds_t s;
    sounds_init(&s);
    brd_player_t p;
    brd_player_init(&p, s.chime_down, s.chime_up, s.chime_len, s.tick_a, s.tick_b, s.tick_len, 0);
    brd_player_set_ticking(&p, 2, 0);
    int16_t c1[960], c2[960], c3[960];
    TB_EQ_INT(brd_player_fill(&p, 1000, c1, 960), 960);
    TB_EQ_INT(brd_player_fill(&p, 1100, c2, 960), 0);
    TB_EQ_INT(brd_player_fill(&p, 2000, c2, 960), 960);
    TB_EQ_INT(brd_player_fill(&p, 3000, c3, 960), 960);
    TB_TRUE(memcmp(c1, c2, sizeof c1) != 0);
    TB_TRUE(memcmp(c1, c3, sizeof c1) == 0);
    sounds_free(&s);
}

TB_TEST(player_chime_mixes_with_ticks_and_restarts)
{
    sounds_t s;
    sounds_init(&s);
    brd_player_t p;
    brd_player_init(&p, s.chime_down, s.chime_up, s.chime_len, s.tick_a, s.tick_b, s.tick_len, 0);
    brd_player_chime(&p, true);
    TB_EQ_INT(brd_player_wait_ms(&p, 0), 0);
    size_t n = 2 * BRD_AUDIO_RATE;
    int16_t *out = calloc(n, sizeof *out);
    run_player(&p, 0, out, n);
    /* Alone it's exactly the rising chime. */
    TB_TRUE(memcmp(out, s.chime_up, s.chime_len * sizeof(int16_t)) == 0);
    TB_TRUE(brd_peak_dbfs(out + s.chime_len, n - s.chime_len) < -100);

    /* With ticking on (as auto-start can do), the tick at 1 s is mixed in, not dropped. */
    brd_player_set_ticking(&p, 2, 10000);
    brd_player_chime(&p, false);
    run_player(&p, 10000 + 0, out, n);
    /* At 1 s the output is the chime plus the first (2000 Hz) tick at Medium, sample for sample. */
    size_t at1s = BRD_AUDIO_RATE;
    int wrong = 0, differs = 0;
    for (size_t i = 0; i < 960; i++) {
        int32_t want = s.chime_down[at1s + i] + (((int32_t)s.tick_a[i] * (int32_t)lroundf(BRD_TICK_GAIN_MEDIUM * 32768.0f)) >> 15);
        wrong += out[at1s + i] != want;
        differs += out[at1s + i] != s.chime_down[at1s + i];
    }
    TB_EQ_INT(wrong, 0);
    TB_TRUE(differs > 300);

    /* A new chime restarts from the beginning. */
    brd_player_stop(&p, 20000);
    brd_player_chime(&p, true);
    int16_t c[240];
    brd_player_fill(&p, 20000, c, 240);
    brd_player_chime(&p, true);
    brd_player_fill(&p, 20010, c, 240);
    TB_TRUE(memcmp(c, s.chime_up, sizeof c) == 0);
    free(out);
    sounds_free(&s);
}

TB_TEST(player_stop_silences_everything)
{
    sounds_t s;
    sounds_init(&s);
    brd_player_t p;
    brd_player_init(&p, s.chime_down, s.chime_up, s.chime_len, s.tick_a, s.tick_b, s.tick_len, 0);
    brd_player_set_ticking(&p, 1, 0);
    brd_player_chime(&p, false);
    int16_t c[240];
    TB_EQ_INT(brd_player_fill(&p, 0, c, 240), 240);
    brd_player_stop(&p, 10);
    TB_EQ_INT(brd_player_fill(&p, 20, c, 240), 0);
    TB_EQ_INT(brd_player_fill(&p, 1500, c, 240), 0);  /* ticking is off too */
    TB_EQ_INT(brd_player_wait_ms(&p, 1500), -1);
    TB_FALSE(brd_player_amp_wanted(&p, 10));          /* no amplifier hold after a stop */
    sounds_free(&s);
}

TB_TEST(player_amplifier_hold)
{
    sounds_t s;
    sounds_init(&s);
    brd_player_t p;
    brd_player_init(&p, s.chime_down, s.chime_up, s.chime_len, s.tick_a, s.tick_b, s.tick_len, 0);
    TB_FALSE(brd_player_amp_wanted(&p, 0));
    brd_player_chime(&p, true);
    TB_TRUE(brd_player_amp_wanted(&p, 0));
    size_t n = s.chime_len + 240;
    int16_t *out = calloc(n, sizeof *out);
    run_player(&p, 0, out, n);
    int64_t end = 1360;
    TB_TRUE(brd_player_amp_wanted(&p, end + BRD_AMP_HOLD_MS - 20));     /* held a little after the sound */
    TB_FALSE(brd_player_amp_wanted(&p, end + BRD_AMP_HOLD_MS + 20));
    /* Ticking keeps it on between ticks. */
    brd_player_set_ticking(&p, 1, 10000);
    TB_TRUE(brd_player_amp_wanted(&p, 10500));
    brd_player_set_ticking(&p, 0, 10600);
    TB_FALSE(brd_player_amp_wanted(&p, 10600 + BRD_AMP_HOLD_MS + 1));
    free(out);
    sounds_free(&s);
}

TB_TEST(player_ticking_restart_and_catch_up)
{
    sounds_t s;
    sounds_init(&s);
    brd_player_t p;
    brd_player_init(&p, s.chime_down, s.chime_up, s.chime_len, s.tick_a, s.tick_b, s.tick_len, 0);
    brd_player_set_ticking(&p, 1, 0);
    /* Changing Soft -> Medium keeps the beat. */
    brd_player_set_ticking(&p, 2, 400);
    TB_EQ_INT(brd_player_wait_ms(&p, 400), 600);
    /* Off and on again: the first tick is a second later. */
    brd_player_set_ticking(&p, 0, 500);
    brd_player_set_ticking(&p, 1, 700);
    TB_EQ_INT(brd_player_wait_ms(&p, 700), 1000);
    /* The task stalled for 5 s: one tick, then back on a 1 s beat (no burst of catch-up ticks). */
    int16_t c[960];
    TB_EQ_INT(brd_player_fill(&p, 6700, c, 960), 960);
    TB_EQ_INT(brd_player_fill(&p, 6740, c, 960), 0);
    TB_EQ_INT(brd_player_wait_ms(&p, 6740), 960);
    sounds_free(&s);
}

/* ================= RTC ================= */

TB_TEST(rtc_civil_dates)
{
    TB_EQ_INT(brd_days_from_civil(1970, 1, 1), 0);
    TB_EQ_INT(brd_days_from_civil(2000, 3, 1), 11017);
    TB_EQ_INT(brd_days_from_civil(2026, 10, 4), 20730);
    int64_t y;
    unsigned m, d;
    brd_civil_from_days(20730, &y, &m, &d);
    TB_EQ_INT(y, 2026);
    TB_EQ_INT(m, 10);
    TB_EQ_INT(d, 4);
    /* Round trip across leap years against the C library. */
    for (int64_t day = 10957; day < 10957 + 366 * 100; day += 17) {
        brd_civil_from_days(day, &y, &m, &d);
        TB_EQ_INT(brd_days_from_civil(y, m, d), day);
        time_t tt = (time_t)(day * 86400);
        struct tm tm;
        gmtime_r(&tt, &tm);
        TB_EQ_INT(y, tm.tm_year + 1900);
        TB_EQ_INT(m, tm.tm_mon + 1);
        TB_EQ_INT(d, tm.tm_mday);
    }
}

TB_TEST(rtc_encode_example)
{
    /* 2026-10-04 21:07:59 UTC, a Sunday. */
    int64_t t = brd_days_from_civil(2026, 10, 4) * 86400 + 21 * 3600 + 7 * 60 + 59;
    uint8_t r[7];
    brd_rtc_encode(t, r);
    TB_EQ_INT(r[0], 0x59);
    TB_EQ_INT(r[1], 0x07);
    TB_EQ_INT(r[2], 0x21);
    TB_EQ_INT(r[3], 0x04);
    TB_EQ_INT(r[4], 0);
    TB_EQ_INT(r[5], 0x10);
    TB_EQ_INT(r[6], 0x26);
    int64_t back = 0;
    TB_TRUE(brd_rtc_decode(r, &back));
    TB_EQ_INT(back, t);
}

TB_TEST(rtc_round_trip_and_weekday)
{
    for (int64_t t = brd_days_from_civil(2024, 1, 1) * 86400; t < brd_days_from_civil(2099, 12, 31) * 86400;
         t += 86400 * 13 + 3607) {
        uint8_t r[7];
        brd_rtc_encode(t, r);
        int64_t back = -1;
        TB_TRUE(brd_rtc_decode(r, &back));
        TB_EQ_INT(back, t);
        time_t tt = (time_t)t;
        struct tm tm;
        gmtime_r(&tt, &tm);
        TB_EQ_INT(r[4], tm.tm_wday);
    }
}

TB_TEST(rtc_rejects_untrusted_times)
{
    int64_t t = brd_days_from_civil(2026, 10, 4) * 86400;
    uint8_t r[7], bad[7];
    int64_t out = 0;
    brd_rtc_encode(t, r);

    memcpy(bad, r, 7);
    bad[0] |= 0x80;                                     /* oscillator stopped */
    TB_FALSE(brd_rtc_decode(bad, &out));
    memcpy(bad, r, 7);
    bad[1] = 0x5a;                                      /* not BCD */
    TB_FALSE(brd_rtc_decode(bad, &out));
    memcpy(bad, r, 7);
    bad[2] = 0x24;                                      /* hour 24 */
    TB_FALSE(brd_rtc_decode(bad, &out));
    memcpy(bad, r, 7);
    bad[5] = 0x02;
    bad[3] = 0x30;                                      /* February 30 */
    TB_FALSE(brd_rtc_decode(bad, &out));
    memcpy(bad, r, 7);
    bad[5] = 0x13;                                      /* month 13 */
    TB_FALSE(brd_rtc_decode(bad, &out));
    memcpy(bad, r, 7);
    bad[3] = 0x00;                                      /* day 0 */
    TB_FALSE(brd_rtc_decode(bad, &out));
    /* The chip's reset date (2000-01-01 00:00:00, OS clear after a careless write) is "never set". */
    const uint8_t reset[7] = {0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x00};
    TB_FALSE(brd_rtc_decode(reset, &out));
    /* A leap day is fine. */
    brd_rtc_encode(brd_days_from_civil(2028, 2, 29) * 86400, r);
    TB_TRUE(brd_rtc_decode(r, &out));
}

TB_TEST(rtc_encode_clamps)
{
    uint8_t r[7];
    brd_rtc_encode(0, r);                               /* 1970: clamped to 2000-01-01 */
    TB_EQ_INT(r[6], 0x00);
    TB_EQ_INT(r[5], 0x01);
    TB_EQ_INT(r[3], 0x01);
    brd_rtc_encode(brd_days_from_civil(2150, 1, 1) * 86400, r);   /* clamped to 2099-12-31 23:59:59 */
    TB_EQ_INT(r[6], 0x99);
    TB_EQ_INT(r[5], 0x12);
    TB_EQ_INT(r[3], 0x31);
    TB_EQ_INT(r[2], 0x23);
}

/* ================= Touch ================= */

static void report(uint8_t b[32], uint8_t points, uint16_t rx, uint16_t ry)
{
    memset(b, 0, 32);
    b[1] = points;
    b[2] = (uint8_t)(rx >> 8);
    b[3] = (uint8_t)rx;
    b[4] = (uint8_t)(ry >> 8);
    b[5] = (uint8_t)ry;
}

TB_TEST(touch_decode_native)
{
    uint8_t b[32];
    int16_t x = -1, y = -1;
    report(b, 1, 100, 50);
    TB_TRUE(brd_touch_decode(b, 32, &x, &y));
    TB_EQ_INT(x, 50);
    TB_EQ_INT(y, 539);
    report(b, 0, 100, 50);
    TB_FALSE(brd_touch_decode(b, 32, &x, &y));          /* no point */
    report(b, 5, 100, 50);
    TB_FALSE(brd_touch_decode(b, 32, &x, &y));          /* garbage count */
    report(b, 1, 4000, 4000);
    b[2] |= 0xf0;                                       /* event bits above the 12-bit value are ignored */
    TB_TRUE(brd_touch_decode(b, 32, &x, &y));
    TB_EQ_INT(x, 171);
    TB_EQ_INT(y, 0);
    TB_FALSE(brd_touch_decode(b, 5, &x, &y));           /* short read */
}

/* LVGL 9.5's lv_display_rotate_point() for a 172 x 640 native display (src/display/lv_display.c). */
static void lvgl_rotate(int rot, int32_t *x, int32_t *y)
{
    int32_t nx = *x, ny = *y;
    if (rot == 90) {
        *x = 640 - ny - 1;
        *y = nx;
    } else if (rot == 270) {
        *x = ny;
        *y = 172 - nx - 1;
    }
}

TB_TEST(touch_matches_waveshare_landscape_mapping)
{
    /* 09_LVGL_V8_Test draws landscape by turning each frame itself, which is LVGL's rotation 270, and maps a touch
     * straight to landscape as (640 - raw X, 172 - raw Y). Our native decode followed by LVGL's own rotation must land
     * on the same point (within the one pixel Waveshare's formula is off by), so touch is right after a flip too. */
    uint8_t b[32];
    for (uint16_t rx = 0; rx < 640; rx += 37) {
        for (uint16_t ry = 0; ry < 172; ry += 11) {
            int16_t nx, ny;
            report(b, 1, rx, ry);
            TB_TRUE(brd_touch_decode(b, 32, &nx, &ny));
            int32_t lx = nx, ly = ny;
            lvgl_rotate(270, &lx, &ly);
            TB_TRUE(labs((long)lx - (640 - rx)) <= 1);
            TB_TRUE(labs((long)ly - (172 - ry)) <= 1);
            /* Rotation 90 (the other way up) puts the same finger at the opposite corner of the layout. */
            int32_t ax = nx, ay = ny;
            lvgl_rotate(90, &ax, &ay);
            TB_EQ_INT(ax + lx, 639);
            TB_EQ_INT(ay + ly, 171);
            TB_TRUE(lx >= 0 && lx < 640 && ly >= 0 && ly < 172);
        }
    }
}

/* ---------- Meeting-start chime (decisions.md "Meeting-start sound (2026-10-07)") ---------- */

TB_TEST(meeting_chime_is_two_soft_rising_notes_lighter_than_the_alarm)
{
    size_t n = brd_meeting_chime_samples(BRD_AUDIO_RATE);
    TB_EQ_INT(n, 20160);                                         /* 0.84 s at 24 kHz */
    int16_t *m = malloc(n * sizeof *m);
    brd_meeting_chime_render(m, n, BRD_AUDIO_RATE);
    /* Each note peaks at 0.10 (-20 dBFS); the second starts while the first still rings, so the sum peaks near -18.4,
     * against the Pomodoro chime's -13.1: quieter, so it isn't mistaken for it. */
    float peak = brd_peak_dbfs(m, n);
    TB_NEAR(peak, -18.4, 0.6);
    size_t cn = brd_chime_samples(BRD_AUDIO_RATE);
    int16_t *c = malloc(cn * sizeof *c);
    brd_chime_render(c, cn, true, BRD_AUDIO_RATE);
    TB_TRUE(peak < brd_peak_dbfs(c, cn) - 4.5f);
    TB_TRUE(n < cn);                                             /* and shorter: about 0.8 s against 1.4 s */
    /* Starts from silence, ends without a click. */
    TB_EQ_INT(abs(m[0]), 0);
    TB_TRUE(abs(m[n - 1]) < 10);
    /* A5 (880 Hz) first, then E6 (1319 Hz): rising. */
    TB_NEAR(crossings_hz(m, 1000, 2000, BRD_AUDIO_RATE), 880.0, 40.0);
    TB_NEAR(crossings_hz(m, 9600, 4800, BRD_AUDIO_RATE), 1319.0, 60.0);
    free(m);
    free(c);
}

TB_TEST(meeting_chime_plays_on_the_chime_voice_and_replaces_a_playing_alarm_chime)
{
    sounds_t s;
    sounds_init(&s);
    size_t mn = brd_meeting_chime_samples(BRD_AUDIO_RATE);
    int16_t *meeting = malloc(mn * sizeof *meeting);
    brd_meeting_chime_render(meeting, mn, BRD_AUDIO_RATE);
    brd_player_t p;
    brd_player_init(&p, s.chime_down, s.chime_up, s.chime_len, s.tick_a, s.tick_b, s.tick_len, 0);
    brd_player_set_meeting_chime(&p, meeting, mn);
    /* an alarm chime is under way when the meeting starts: it gives way */
    brd_player_chime(&p, true);
    size_t n = 2 * BRD_AUDIO_RATE;
    int16_t *out = calloc(n, sizeof *out);
    run_player(&p, 0, out, 4800);
    brd_player_meeting_chime(&p);
    run_player(&p, 200, out, n);
    TB_TRUE(memcmp(out, meeting, mn * sizeof(int16_t)) == 0);     /* exactly the meeting chime from its start */
    TB_TRUE(brd_peak_dbfs(out + mn, n - mn) < -100);              /* then silence, nothing of the alarm's tail */
    TB_EQ_INT(brd_player_wait_ms(&p, 5000), -1);
    /* with no chime set it does nothing */
    brd_player_t q;
    brd_player_init(&q, s.chime_down, s.chime_up, s.chime_len, s.tick_a, s.tick_b, s.tick_len, 0);
    brd_player_meeting_chime(&q);
    TB_EQ_INT(brd_player_wait_ms(&q, 0), -1);
    free(out);
    free(meeting);
    sounds_free(&s);
}
