/*
 * test_orient_start.c: host tests for the orientation at start-up (firmware 1.0.2). Owner: lead developer.
 *
 * On the V2 board the user stands the bar with its side buttons on top, and that's "upright". 1.0.1 drew the first
 * frame upside down there and righted it half a second later. 1.0.2 turns both the IMU's up axis (+Y -> -Y) and the
 * picture (CONFIG_TINYBAR_LCD_TURN_180) so "upright", and the default when the IMU can't tell, mean buttons on top;
 * waits for settled samples before the first frame; and remembers the last steady pose in NVS. These prove:
 *   - the two inversions cancel: with a reading, every sample and every sequence draws the same rotation as before;
 *   - only the no-information default changed, to buttons on top;
 *   - the start-up reading drops zeros, the filter's ramp and early samples, and finishes inside 150 ms;
 *   - the remembered pose survives a change of the IMU axis setting, and is written only after 10 s standing still.
 */
#include <math.h>
#include <stdlib.h>

#include "brd_logic.h"
#include "tb_test.h"

/* Before (1.0.1) and now (1.0.2). */
#define OLD_UP BRD_UP_Y_POS
#define OLD_TURN false
#define NEW_UP BRD_UP_Y_NEG
#define NEW_TURN true

static int rotation_of(int verdict, bool turn)
{
    return brd_lcd_rotation(verdict == 1, turn);   /* -1 (can't tell) starts upright */
}

/* ================= The two inversions cancel ================= */

TB_TEST(orient_lcd_rotation_table)
{
    TB_EQ_INT(brd_lcd_rotation(false, false), 90);
    TB_EQ_INT(brd_lcd_rotation(true, false), 270);
    TB_EQ_INT(brd_lcd_rotation(false, true), 270);
    TB_EQ_INT(brd_lcd_rotation(true, true), 90);
}

TB_TEST(orient_names_say_where_the_buttons_are)
{
    TB_EQ_STR(brd_orient_name(0), "upright, buttons on top");
    TB_EQ_STR(brd_orient_name(1), "upside down, buttons at the bottom");
    TB_EQ_STR(brd_orient_name(-1), "can't tell");
}

TB_TEST(orient_inversions_cancel_for_every_sample)
{
    /* Every IMU axis setting, inverted together with the picture's turn, gives the same rotation for every sample
     * that can be classified, at both thresholds; and a sample that can't be classified stays unclassified. */
    const brd_up_axis_t ups[4] = {BRD_UP_X_POS, BRD_UP_X_NEG, BRD_UP_Y_POS, BRD_UP_Y_NEG};
    const int32_t thresholds[2] = {BRD_OR_BOOT_MG, BRD_OR_ENTER_MG};
    long told = 0, untold = 0;
    for (int k = 0; k < 4; k++) {
        brd_up_axis_t inv = brd_orient_pose(ups[k], true);
        TB_TRUE(inv != ups[k]);
        TB_EQ_INT(brd_orient_pose(inv, true), ups[k]);
        for (int th = 0; th < 2; th++) {
            for (int32_t ax = -1300; ax <= 1300; ax += 100) {
                for (int32_t ay = -1300; ay <= 1300; ay += 100) {
                    for (int32_t az = -900; az <= 900; az += 300) {
                        int a = brd_orient_classify(ups[k], ax, ay, az, thresholds[th]);
                        int b = brd_orient_classify(inv, ax, ay, az, thresholds[th]);
                        if (a < 0) {
                            TB_EQ_INT(b, -1);
                            untold++;
                            continue;
                        }
                        told++;
                        TB_EQ_INT(b, 1 - a);
                        for (int t = 0; t < 2; t++) TB_EQ_INT(rotation_of(a, t), rotation_of(b, !t));
                    }
                }
            }
        }
    }
    TB_TRUE(told > 1000 && untold > 1000);  /* the grid covers both cases */
}

TB_TEST(orient_only_the_default_changed)
{
    /* Buttons on top, standing (the QMI8658's Y reads about -1 g there): 1.0.1 said flipped, rotation 270; 1.0.2 says
     * upright, rotation 270. Buttons at the bottom: 90 both times. Can't tell (lying flat): 1.0.1 drew 90, which is
     * upside down for buttons on top; 1.0.2 draws 270. */
    int a = brd_orient_classify(OLD_UP, 10, -985, 160, BRD_OR_BOOT_MG);
    int b = brd_orient_classify(NEW_UP, 10, -985, 160, BRD_OR_BOOT_MG);
    TB_EQ_INT(a, 1);
    TB_EQ_INT(b, 0);
    TB_EQ_INT(rotation_of(a, OLD_TURN), 270);
    TB_EQ_INT(rotation_of(b, NEW_TURN), 270);
    TB_EQ_INT(rotation_of(brd_orient_classify(OLD_UP, 0, 990, 100, BRD_OR_BOOT_MG), OLD_TURN), 90);
    TB_EQ_INT(rotation_of(brd_orient_classify(NEW_UP, 0, 990, 100, BRD_OR_BOOT_MG), NEW_TURN), 90);
    int flat_old = brd_orient_classify(OLD_UP, 20, 30, 1000, BRD_OR_BOOT_MG);
    int flat_new = brd_orient_classify(NEW_UP, 20, 30, 1000, BRD_OR_BOOT_MG);
    TB_EQ_INT(flat_old, -1);
    TB_EQ_INT(flat_new, -1);
    TB_EQ_INT(rotation_of(flat_old, OLD_TURN), 90);
    TB_EQ_INT(rotation_of(flat_new, NEW_TURN), 270);
}

/* A small deterministic generator, so the sequences are the same on every run. */
static uint32_t s_rng = 12345;
static int32_t rnd(int32_t lo, int32_t hi)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return lo + (int32_t)((s_rng >> 8) % (uint32_t)(hi - lo + 1));
}

/* One sample of a pose: 0 buttons on top, 1 buttons at the bottom, 2 lying flat, 3 on a short edge, 4 carried. In
 * the board's raw axes, buttons on top is Y about -1 g. */
static void pose_sample(int pose, int32_t *x, int32_t *y, int32_t *z)
{
    int32_t n = rnd(-40, 40);
    switch (pose) {
    case 0: *x = n; *y = -970 + rnd(-30, 30); *z = 180 + n; break;
    case 1: *x = n; *y = 970 + rnd(-30, 30); *z = 180 - n; break;
    case 2: *x = n; *y = rnd(-80, 80); *z = 990; break;
    case 3: *x = 990; *y = rnd(-80, 80); *z = n; break;
    default: *x = rnd(-500, 500); *y = rnd(-1500, 1500); *z = rnd(-700, 700); break;
    }
}

TB_TEST(orient_inversions_cancel_for_every_sequence)
{
    /* 40 random runs of poses and their lengths, fed to 1.0.1's tracker and to 1.0.2's side by side, starting from
     * the same picture. Every event comes at the same sample with the same rotation, and the remembered pose (1.0.2
     * only; 1.0.1 had none, so it's checked against a tracker with the old setting) is written at the same samples
     * with the same physical pose. */
    for (int run = 0; run < 40; run++) {
        bool start_old = rnd(0, 1) == 1;    /* the picture main started with, in 1.0.1's terms */
        bool start_new = !start_old;         /* the same picture in 1.0.2's terms */
        TB_EQ_INT(brd_lcd_rotation(start_old, OLD_TURN), brd_lcd_rotation(start_new, NEW_TURN));
        brd_orient_t o_old, o_new;
        int64_t t = 1000;
        brd_orient_init(&o_old, OLD_UP, start_old, t);
        brd_orient_init(&o_new, NEW_UP, start_new, t);
        int stored_old = -1, stored_new = -1, events = 0, writes = 0;
        for (int seg = 0; seg < 12; seg++) {
            int pose = rnd(0, 4);
            int samples = rnd(1, 400);      /* up to 16 s */
            for (int i = 0; i < samples; i++) {
                t += BRD_OR_PERIOD_MS;
                int32_t x, y, z;
                pose_sample(pose, &x, &y, &z);
                bool f_old = false, f_new = false;
                brd_or_event_t e_old = brd_orient_feed(&o_old, x, y, z, t, &f_old);
                brd_or_event_t e_new = brd_orient_feed(&o_new, x, y, z, t, &f_new);
                TB_EQ_INT(e_old, e_new);
                if (e_old != BRD_OR_NONE) {
                    events++;
                    TB_EQ_INT(brd_lcd_rotation(f_old, OLD_TURN), brd_lcd_rotation(f_new, NEW_TURN));
                }
                int w_old = brd_orient_to_remember(&o_old, stored_old, t);
                int w_new = brd_orient_to_remember(&o_new, stored_new, t);
                TB_EQ_INT(w_old >= 0, w_new >= 0);
                if (w_old >= 0 && w_new >= 0) {
                    writes++;
                    TB_EQ_INT(brd_orient_pose(OLD_UP, w_old == 1), brd_orient_pose(NEW_UP, w_new == 1));
                    stored_old = w_old;
                    stored_new = w_new;
                }
            }
        }
        TB_TRUE(events >= 1);               /* INITIAL at least */
        TB_TRUE(writes <= 12);              /* at most one per segment */
    }
}

/* ================= The reading before the first frame ================= */

/* The chip as the datasheet describes it, from the enable: no data until first_ms, then a sample every 16 ms that
 * ramps up through the low-pass filter (8.4 Hz at 62.5 Hz with mode 11: a time constant of about 19 ms). g is the
 * settled vector. */
#define ODR_MS 16
static void chip_sample(int32_t t_ms, int32_t first_ms, const int32_t g[3], int32_t out[3])
{
    double k = t_ms < first_ms ? 0.0 : 1.0 - exp(-(double)(t_ms - first_ms + ODR_MS) / 19.0);
    for (int i = 0; i < 3; i++) out[i] = (int32_t)lround(g[i] * k);
}

/* Feed what the chip has at each data-ready from first_ms on; returns the time the reading finished, or -1 if it
 * didn't within BRD_BOOT_WAIT_MS (b then holds what it got). */
static int32_t boot_read(brd_boot_read_t *b, int32_t first_ms, const int32_t g[3])
{
    brd_boot_read_init(b);
    for (int32_t t = first_ms; t < BRD_BOOT_WAIT_MS; t += ODR_MS) {
        int32_t s[3];
        chip_sample(t, first_ms, g, s);
        if (brd_boot_read_feed(b, s[0], s[1], s[2], t)) return t;
    }
    return -1;
}

/* 1.0.1's reading: 40 ms after the enable, four samples 16 ms apart, whatever they held, averaged. */
static int old_reading(int32_t first_ms, const int32_t g[3])
{
    int32_t sum[3] = {0, 0, 0};
    for (int i = 0; i < 4; i++) {
        int32_t s[3];
        chip_sample(40 + 16 * i, first_ms, g, s);
        for (int k = 0; k < 3; k++) sum[k] += s[k];
    }
    return brd_orient_classify(OLD_UP, sum[0] / 4, sum[1] / 4, sum[2] / 4, BRD_OR_BOOT_MG);
}

static const int32_t BUTTONS_ON_TOP[3] = {12, -985, 160};

TB_TEST(boot_read_as_the_datasheet_says)
{
    /* First sample at 19 ms (3 ms turn-on + one period): done well inside 150 ms, settled, and upright. */
    brd_boot_read_t b;
    int32_t done = boot_read(&b, 19, BUTTONS_ON_TOP);
    TB_TRUE(done >= BRD_BOOT_SETTLE_MS && done <= 120);
    TB_EQ_INT(b.early, 2);              /* the samples at 19 and 35 ms: still settling */
    TB_EQ_INT(done, 83);                /* 51, 67 and 83 ms */
    TB_EQ_INT(b.good, BRD_BOOT_SAMPLES);
    int32_t x, y, z;
    TB_TRUE(brd_boot_read_mean(&b, &x, &y, &z));
    TB_TRUE(y < -900 && y > -1000);
    int c = brd_orient_classify(NEW_UP, x, y, z, BRD_OR_BOOT_MG);
    TB_EQ_INT(c, 0);
    TB_EQ_INT(rotation_of(c, NEW_TURN), 270);
}

TB_TEST(boot_read_late_first_sample_the_101_failure)
{
    /* If the first sample comes later than the datasheet's typical (here 60 ms), 1.0.1's fixed 40 ms wait averaged
     * zeros with the start of the ramp, couldn't tell, and drew upright, which was buttons at the bottom: the report
     * "upside down when I first start up, then it rights itself". 1.0.2 waits for settled samples instead. (The cause
     * on the real bar isn't confirmed; the start-up log line now says how long the reading took and what it saw.) */
    int old = old_reading(60, BUTTONS_ON_TOP);
    TB_EQ_INT(old, -1);
    TB_EQ_INT(rotation_of(old, OLD_TURN), 90);              /* upside down with the buttons on top */
    brd_boot_read_t b;
    int32_t done = boot_read(&b, 60, BUTTONS_ON_TOP);
    TB_TRUE(done > 0 && done < BRD_BOOT_WAIT_MS);
    TB_TRUE(b.implausible >= 1);                            /* the ramp's first sample is under 0.8 g */
    int32_t x, y, z;
    TB_TRUE(brd_boot_read_mean(&b, &x, &y, &z));
    int c = brd_orient_classify(NEW_UP, x, y, z, BRD_OR_BOOT_MG);
    TB_EQ_INT(c, 0);
    TB_EQ_INT(rotation_of(c, NEW_TURN), 270);
}

TB_TEST(boot_read_drops_zeros_early_and_implausible_samples)
{
    brd_boot_read_t b;
    brd_boot_read_init(&b);
    /* Zeros (registers before the first conversion) and a full 1 g sample during settling: none count. */
    TB_FALSE(brd_boot_read_feed(&b, 0, 0, 0, 5));
    TB_FALSE(brd_boot_read_feed(&b, 0, -990, 100, 30));
    TB_EQ_INT(b.early, 2);
    TB_EQ_INT(b.good, 0);
    TB_FALSE(brd_boot_read_feed(&b, 0, 0, 0, 60));         /* zeros after settling: implausible */
    TB_FALSE(brd_boot_read_feed(&b, 0, -650, 100, 76));    /* 0.66 g: still ramping */
    TB_EQ_INT(b.implausible, 2);
    TB_FALSE(brd_boot_read_feed(&b, 0, -990, 100, 92));
    TB_FALSE(brd_boot_read_feed(&b, 0, -1400, 100, 108));  /* 1.4 g: a jolt, and the run starts again */
    TB_EQ_INT(b.good, 0);
    TB_FALSE(brd_boot_read_feed(&b, 0, -990, 100, 124));
    TB_FALSE(brd_boot_read_feed(&b, 10, -980, 110, 140));
    TB_TRUE(brd_boot_read_feed(&b, -10, -985, 105, 156));
    TB_EQ_INT(b.seen, 9);
    int32_t x, y, z;
    TB_TRUE(brd_boot_read_mean(&b, &x, &y, &z));
    TB_EQ_INT(x, 0);
    TB_EQ_INT(y, -985);
    TB_EQ_INT(z, 105);
}

TB_TEST(boot_read_takes_the_classifiers_one_g)
{
    /* The start-up reading and the classifier agree on what 1 g is (0.8 to 1.2 g), so a finished run standing on its
     * edge always classifies: one at 0.75 g used to be averaged (0.7 to 1.3 g) and then came out "can't tell". */
    brd_boot_read_t b;
    brd_boot_read_init(&b);
    for (int i = 0; i < 3; i++) TB_FALSE(brd_boot_read_feed(&b, 0, -750, 0, 60 + 16 * i));     /* 0.75 g */
    for (int i = 0; i < 3; i++) TB_FALSE(brd_boot_read_feed(&b, 0, -1210, 0, 108 + 16 * i));   /* 1.21 g */
    TB_EQ_INT(b.implausible, 6);
    TB_EQ_INT(b.good, 0);
    /* Just inside both ends: the run finishes and its mean classifies. */
    const int32_t ends[2] = {-805, -1195};
    for (int e = 0; e < 2; e++) {
        brd_boot_read_init(&b);
        TB_FALSE(brd_boot_read_feed(&b, 0, ends[e], 0, 60));
        TB_FALSE(brd_boot_read_feed(&b, 0, ends[e], 0, 76));
        TB_TRUE(brd_boot_read_feed(&b, 0, ends[e], 0, 92));
        int32_t x, y, z;
        TB_TRUE(brd_boot_read_mean(&b, &x, &y, &z));
        TB_EQ_INT(brd_orient_classify(NEW_UP, x, y, z, BRD_OR_BOOT_MG), 0);
    }
    /* Every finished run of one sample repeated, over a grid around 1 g: the mean classifies whenever it points
     * along the up axis by at least the start-up threshold. */
    long finished = 0;
    for (int32_t ay = -1300; ay <= 1300; ay += 25) {
        for (int32_t az = -1300; az <= 1300; az += 25) {
            brd_boot_read_init(&b);
            bool done = false;
            for (int i = 0; i < BRD_BOOT_SAMPLES; i++) done = brd_boot_read_feed(&b, 30, ay, az, 60 + 16 * i);
            if (!done) continue;
            finished++;
            int32_t x, y, z;
            TB_TRUE(brd_boot_read_mean(&b, &x, &y, &z));
            if (labs((long)y) >= BRD_OR_BOOT_MG) TB_TRUE(brd_orient_classify(NEW_UP, x, y, z, BRD_OR_BOOT_MG) >= 0);
        }
    }
    TB_TRUE(finished > 100);
}

TB_TEST(boot_read_a_jump_starts_a_new_run)
{
    brd_boot_read_t b;
    brd_boot_read_init(&b);
    TB_FALSE(brd_boot_read_feed(&b, 0, 980, 150, 60));
    TB_FALSE(brd_boot_read_feed(&b, 0, 960, 160, 76));
    TB_FALSE(brd_boot_read_feed(&b, 0, 760, 600, 92));     /* tipped 200 mg: a new run of one */
    TB_EQ_INT(b.restarts, 1);
    TB_EQ_INT(b.good, 1);
    int32_t x, y, z;
    TB_TRUE(brd_boot_read_mean(&b, &x, &y, &z));
    TB_EQ_INT(y, 760);
    TB_FALSE(brd_boot_read_feed(&b, 0, 750, 610, 108));
    TB_TRUE(brd_boot_read_feed(&b, 0, 770, 590, 124));
    TB_TRUE(brd_boot_read_mean(&b, &x, &y, &z));
    TB_EQ_INT(y, 760);
    TB_EQ_INT(z, 600);
}

TB_TEST(boot_read_nothing_good_and_rounding)
{
    brd_boot_read_t b;
    brd_boot_read_init(&b);
    int32_t x = 7, y = 7, z = 7;
    TB_FALSE(brd_boot_read_mean(&b, &x, &y, &z));
    for (int i = 0; i < 300; i++) brd_boot_read_feed(&b, 0, 0, 0, 60 + i);
    TB_EQ_INT(b.seen, 255);                                 /* saturates */
    TB_EQ_INT(b.implausible, 255);
    TB_FALSE(brd_boot_read_mean(&b, &x, &y, &z));
    TB_EQ_INT(x, 7);                                        /* untouched */
    /* Means round to nearest, both signs. */
    brd_boot_read_init(&b);
    brd_boot_read_feed(&b, 1, -991, 101, 60);
    brd_boot_read_feed(&b, 2, -992, 102, 76);
    TB_TRUE(brd_boot_read_mean(&b, &x, &y, &z));
    TB_EQ_INT(x, 2);                                        /* 1.5 */
    TB_EQ_INT(y, -992);                                     /* -991.5 */
    TB_EQ_INT(z, 102);
}

TB_TEST(boot_read_a_stuck_data_ready_bit_still_reads_settled_data)
{
    /* If STATUS0's data-ready bit never cleared, the same settled sample would be read every 2 ms after the settling
     * time: still a right reading, just from one conversion. */
    brd_boot_read_t b;
    brd_boot_read_init(&b);
    int32_t done = -1;
    for (int32_t t = 1; t < BRD_BOOT_WAIT_MS && done < 0; t += 2) {
        int32_t s[3];
        chip_sample(t, 19, BUTTONS_ON_TOP, s);
        if (brd_boot_read_feed(&b, s[0], s[1], s[2], t)) done = t;
    }
    TB_TRUE(done >= BRD_BOOT_SETTLE_MS && done < BRD_BOOT_WAIT_MS);
}

TB_TEST(boot_choice_reading_then_memory_then_buttons_on_top)
{
    brd_boot_src_t src = BRD_BOOT_DEFAULT;
    TB_FALSE(brd_orient_boot_choice(0, 1, &src));
    TB_EQ_INT(src, BRD_BOOT_FROM_IMU);
    TB_TRUE(brd_orient_boot_choice(1, 0, &src));
    TB_EQ_INT(src, BRD_BOOT_FROM_IMU);
    TB_TRUE(brd_orient_boot_choice(-1, 1, &src));
    TB_EQ_INT(src, BRD_BOOT_FROM_MEMORY);
    TB_FALSE(brd_orient_boot_choice(-1, 0, &src));
    TB_EQ_INT(src, BRD_BOOT_FROM_MEMORY);
    TB_FALSE(brd_orient_boot_choice(-1, -1, &src));
    TB_EQ_INT(src, BRD_BOOT_DEFAULT);
    TB_EQ_INT(brd_lcd_rotation(false, NEW_TURN), 270);     /* the default: buttons on top */
}

/* ================= The remembered pose ================= */

TB_TEST(pose_round_trip_and_foreign_values)
{
    const brd_up_axis_t ups[4] = {BRD_UP_X_POS, BRD_UP_X_NEG, BRD_UP_Y_POS, BRD_UP_Y_NEG};
    for (int k = 0; k < 4; k++) {
        for (int f = 0; f < 2; f++) TB_EQ_INT(brd_orient_from_pose(ups[k], (int)brd_orient_pose(ups[k], f)), f);
        TB_EQ_INT(brd_orient_from_pose(ups[k], -1), -1);
        TB_EQ_INT(brd_orient_from_pose(ups[k], 4), -1);
        TB_EQ_INT(brd_orient_from_pose(ups[k], 255), -1);
    }
    /* A pose along the other axis means nothing to this setting. */
    TB_EQ_INT(brd_orient_from_pose(BRD_UP_Y_NEG, BRD_UP_X_POS), -1);
    TB_EQ_INT(brd_orient_from_pose(BRD_UP_X_NEG, BRD_UP_Y_POS), -1);
    /* Buttons on top is -Y on the V2 board. */
    TB_EQ_INT(brd_orient_pose(NEW_UP, false), BRD_UP_Y_NEG);
    TB_EQ_INT(brd_orient_pose(NEW_UP, true), BRD_UP_Y_POS);
}

TB_TEST(pose_survives_a_change_of_axis_setting)
{
    /* Stored under one setting, read under the inverted one (with the picture turned too): the same picture. */
    for (int f = 0; f < 2; f++) {
        brd_up_axis_t pose = brd_orient_pose(OLD_UP, f);
        int now = brd_orient_from_pose(NEW_UP, pose);
        TB_EQ_INT(now, 1 - f);
        TB_EQ_INT(brd_lcd_rotation(f, OLD_TURN), brd_lcd_rotation(now == 1, NEW_TURN));
    }
}

/* Feed a pose for ms milliseconds; returns the last orientation to_remember asked to write (and "writes" it). */
static int hold(brd_orient_t *o, int64_t *t, int ms, int32_t x, int32_t y, int32_t z, int *stored, int *writes,
                int64_t *wrote_at)
{
    int last = -1;
    for (int i = 0; i < ms / BRD_OR_PERIOD_MS; i++) {
        *t += BRD_OR_PERIOD_MS;
        bool f = false;
        brd_orient_feed(o, x, y, z, *t, &f);
        int w = brd_orient_to_remember(o, *stored, *t);
        if (w >= 0) {
            *stored = w;
            (*writes)++;
            last = w;
            if (wrote_at) *wrote_at = *t;
        }
    }
    return last;
}

TB_TEST(remember_after_ten_seconds_standing_still_and_only_once)
{
    brd_orient_t o;
    int64_t t = 0, at = 0;
    int stored = -1, writes = 0;
    brd_orient_init(&o, NEW_UP, false, t);
    /* Nothing is written before INITIAL, nor in the first 10 s. */
    TB_EQ_INT(brd_orient_to_remember(&o, stored, t), -1);
    TB_EQ_INT(hold(&o, &t, 9800, 0, -980, 150, &stored, &writes, NULL), -1);
    TB_EQ_INT(writes, 0);
    /* At 10 s of standing still: buttons on top. */
    TB_EQ_INT(hold(&o, &t, 400, 0, -980, 150, &stored, &writes, &at), 0);
    TB_EQ_INT(writes, 1);
    TB_TRUE(at >= BRD_OR_REMEMBER_MS && at <= BRD_OR_REMEMBER_MS + 2 * BRD_OR_PERIOD_MS);
    /* And never again while it stays. */
    hold(&o, &t, 60000, 0, -980, 150, &stored, &writes, NULL);
    TB_EQ_INT(writes, 1);
    /* Turned over: written 10 s after the turn, once. */
    int64_t turned = t;
    TB_EQ_INT(hold(&o, &t, 12000, 0, 980, 150, &stored, &writes, &at), 1);
    TB_EQ_INT(writes, 2);
    TB_TRUE(at - turned >= BRD_OR_REMEMBER_MS && at - turned <= BRD_OR_REMEMBER_MS + 2 * BRD_OR_PERIOD_MS);
    TB_EQ_INT(stored, 1);
}

TB_TEST(remember_ignores_jiggles_quick_turns_and_lying_flat)
{
    brd_orient_t o;
    int64_t t = 0;
    int stored = 0, writes = 0;                 /* buttons on top already remembered */
    brd_orient_init(&o, NEW_UP, false, t);
    hold(&o, &t, 2000, 0, -980, 150, &stored, &writes, NULL);
    /* Turned over for 4 s and back: no write. */
    hold(&o, &t, 4000, 0, 980, 150, &stored, &writes, NULL);
    hold(&o, &t, 15000, 0, -980, 150, &stored, &writes, NULL);
    TB_EQ_INT(writes, 0);
    /* Turned over and jostled every 2 s for a minute: the layout turns, but it's never still for 10 s. */
    for (int i = 0; i < 30; i++) {
        hold(&o, &t, 1960, 0, 980, 150, &stored, &writes, NULL);
        hold(&o, &t, 40, 0, 760, 600, &stored, &writes, NULL);
    }
    TB_TRUE(o.flipped);
    TB_EQ_INT(writes, 0);
    /* Turned over, then laid flat within 10 s: no steady vote, no write. */
    hold(&o, &t, 3000, 0, -980, 150, &stored, &writes, NULL);
    hold(&o, &t, 3000, 0, 980, 150, &stored, &writes, NULL);
    hold(&o, &t, 30000, 20, 30, 1000, &stored, &writes, NULL);
    TB_EQ_INT(writes, 0);
    /* Then left standing that way: written. */
    TB_EQ_INT(hold(&o, &t, 11000, 0, 980, 150, &stored, &writes, NULL), 1);
    TB_EQ_INT(writes, 1);
}

TB_TEST(remember_after_a_start_lying_flat)
{
    /* Started from memory (flipped) while lying flat: INITIAL confirms it after 2 s and nothing is written; stood
     * up the other way, it turns and remembers that. */
    brd_orient_t o;
    int64_t t = 0;
    int stored = 1, writes = 0;
    brd_orient_init(&o, NEW_UP, true, t);
    hold(&o, &t, 20000, 20, 30, 1000, &stored, &writes, NULL);
    TB_TRUE(o.initial_sent);
    TB_TRUE(o.flipped);
    TB_EQ_INT(writes, 0);
    TB_EQ_INT(hold(&o, &t, 11000, 0, -980, 150, &stored, &writes, NULL), 0);
    TB_FALSE(o.flipped);
    TB_EQ_INT(writes, 1);
}
