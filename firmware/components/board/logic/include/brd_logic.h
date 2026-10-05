/*
 * brd_logic.h: the board's pure logic. Owner: board builder.
 *
 * Everything here is plain C11 with no ESP-IDF, FreeRTOS or LVGL headers, so it builds unchanged on Linux and is
 * tested in test/host/board/. The device code in ../src/ feeds it register values, raw samples and times, and carries
 * out what it decides. Times are monotonic milliseconds (int64_t), as everywhere in TinyBar.
 *
 *   backlight   percent -> LCD_BL PWM duty, through the mock-up's brightness curve and the V2 board's dimming circuit
 *   buttons     5 ms debouncer with "held at boot" suppression
 *   orient      QMI8658 samples -> upright / flipped, with hysteresis and a 0.5 s steadiness rule; the reading before
 *               the first frame, the remembered pose, and the LVGL rotation for each orientation
 *   synth       the chime and the focus tick as 16-bit PCM, matching the mock-up's Web Audio graph
 *   player      two voices (chime, tick) mixed into chunks, one tick a second, amplifier gating
 *   rtc         PCF85063 time registers <-> UTC seconds
 *   touch       AXS15231B touch report -> native panel coordinates
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------------------------------------------
 * Backlight
 *
 * The V2 schematic ("LCD_Backlight"): an AP3032 boost driver regulates the LED current so its FB pin sits at 0.2 V
 * across a 27 ohm sense resistor (via 5.1k). LCD_BL (GPIO 42) feeds the same FB node through 10k + 39k with 100 nF to
 * ground, so the PWM's average voltage V lowers the LED current linearly:
 *     I = (0.2 - (V - 0.2) * 5.1 / 49) / 27     -> 8.2 mA at V = 0, zero at V = 2.12 V (64% of 3.3 V)
 * So the pin is "inverted" (Waveshare's LCD_PWM_MODE_255 is duty 0) and the dark end comes well before 100% high.
 * BL_EN (EXIO1) switches the driver itself and is what really turns the light off.
 *
 * Brightness follows the mock-up: its Light tile (40, 70, 100%) is drawn with a CSS brightness factor
 * b = 0.45 + 0.55 * p / 100 on sRGB values, which shows as luminance b^2.2. The LED's light is roughly proportional to
 * its current, so the target current fraction is L = b^2.2, and the pin's high duty is zero_duty * (1 - L).
 * ------------------------------------------------------------------------------------------------------------- */

#define BRD_BL_DUTY_MAX 255     /* 8-bit LEDC, as 10_LVGL_V9_Test */
#define BRD_BL_ZERO_DUTY 164    /* duty (of 255) where the LED current reaches zero, from the schematic; check on board */

/* Target LED current as a fraction of the maximum, 0..1, for a brightness in percent (0 = off). */
float brd_bl_level(uint8_t percent);
/* The LCD_BL PWM duty (0..255 = how long the pin is high) for a brightness in percent. 0% gives 255 (fully dark).
 * zero_duty is the duty at which the light goes out (BRD_BL_ZERO_DUTY unless the board says otherwise). */
uint8_t brd_bl_duty(uint8_t percent, uint8_t zero_duty);

/* ---------------------------------------------------------------------------------------------------------------
 * Buttons: a debouncer fed every 5 ms with the raw "pressed" level. A change counts once it has held for
 * BRD_DEBOUNCE_SAMPLES samples in a row (20 ms). A button found pressed at init (the press that powered the bar on or
 * woke it) reports nothing until it has been released; that release isn't reported either.
 * ------------------------------------------------------------------------------------------------------------- */

#define BRD_BUTTON_POLL_MS    5
#define BRD_DEBOUNCE_SAMPLES  4

typedef enum { BRD_EDGE_NONE = 0, BRD_EDGE_DOWN, BRD_EDGE_UP } brd_edge_t;

typedef struct {
    bool pressed;       /* debounced state */
    bool suppress;      /* held since init: swallow edges until it's released */
    uint8_t run;        /* samples in a row that disagree with `pressed` */
} brd_debounce_t;

void brd_debounce_init(brd_debounce_t *d, bool pressed_now);
brd_edge_t brd_debounce_feed(brd_debounce_t *d, bool raw_pressed);

/* ---------------------------------------------------------------------------------------------------------------
 * Orientation (flip detection)
 *
 * The bar stands on a long edge, so gravity lies along the panel's short axis: one of the IMU's X or Y axes, with a
 * sign that depends on how the QMI8658 sits on the board (set in Kconfig, checked at bring-up). `up` names the axis
 * that reads +1 g when the bar stands upright: the way the user stands it, with the side buttons (BOOT, PWR) on top
 * (verified on the V2 board 2026-10-05). There the QMI8658's Y axis reads about -1 g, so `up` is -Y: inferred from
 * 1.0.1, whose steady pictures were right with +Y and no turn. "Flipped" is the other way up, buttons at the bottom.
 * Which LVGL rotation draws "upright" is brd_lcd_rotation()'s business.
 *
 *   - A sample votes "upright" when that axis reads >= +600 mg, "flipped" when <= -600 mg, and nothing in between
 *     (lying flat, standing on a short edge, half-way through a turn). The 1.2 g gap between the two thresholds is
 *     the hysteresis: a bar has to be turned well past level before it counts as the other way up.
 *   - Only samples whose total is 0.8 to 1.2 g count (not being swung or carried), and the reading along the axis
 *     must not jump by more than 150 mg between samples; anything else restarts the steadiness window.
 *   - A vote must hold for 500 ms to count.
 *   - The first result is INITIAL: the first steady vote, or after 2 s the orientation main started with. After
 *     that, CHANGED whenever a steady vote differs from the last reported orientation.
 * ------------------------------------------------------------------------------------------------------------- */

typedef enum { BRD_UP_X_POS = 0, BRD_UP_X_NEG, BRD_UP_Y_POS, BRD_UP_Y_NEG } brd_up_axis_t;
typedef enum { BRD_OR_NONE = 0, BRD_OR_INITIAL, BRD_OR_CHANGED } brd_or_event_t;

#define BRD_OR_PERIOD_MS    40      /* 25 Hz */
#define BRD_OR_STEADY_MS    500
#define BRD_OR_ENTER_MG     600
#define BRD_OR_BOOT_MG      350     /* the single reading before the first frame takes a weaker tilt */
#define BRD_OR_MAG_MIN_MG   800
#define BRD_OR_MAG_MAX_MG   1200
#define BRD_OR_JITTER_MG    150
#define BRD_OR_INITIAL_MS   2000

typedef struct {
    brd_up_axis_t up;
    bool flipped;           /* last reported (or, before INITIAL, what main started with) */
    bool initial_sent;
    int64_t start_ms;
    int8_t vote;            /* -1 none, 0 upright, 1 flipped */
    int64_t vote_since_ms;
    int32_t last_along;     /* last reading along the up axis, mg */
    bool have_last;
} brd_orient_t;

/* The reading along the up axis (mg, positive = the normal way up). */
int32_t brd_orient_along(brd_up_axis_t up, int32_t ax_mg, int32_t ay_mg);
/* One sample: 0 upright, 1 flipped, -1 can't tell (|along| < threshold_mg, or the total isn't about 1 g). */
int brd_orient_classify(brd_up_axis_t up, int32_t ax_mg, int32_t ay_mg, int32_t az_mg, int32_t threshold_mg);

void brd_orient_init(brd_orient_t *o, brd_up_axis_t up, bool flipped_now, int64_t now_ms);
/* Feed one sample. On INITIAL or CHANGED, *flipped says which way up the bar now is. */
brd_or_event_t brd_orient_feed(brd_orient_t *o, int32_t ax_mg, int32_t ay_mg, int32_t az_mg, int64_t now_ms,
                               bool *flipped);

/* QMI8658 accelerometer raw value at +-2 g full scale (16384 LSB/g) to mg. */
int32_t brd_qmi_raw_to_mg(int16_t raw);

/* For the log: "upright, buttons on top", "upside down, buttons at the bottom", or (any other value) "can't tell". */
const char *brd_orient_name(int flipped);

/* The LVGL rotation in degrees (90 or 270) that draws the layout the right way up. Upright is 90 and flipped 270 in
 * Waveshare's 10_LVGL_V9_Test; turn_180 (CONFIG_TINYBAR_LCD_TURN_180) swaps them. On the V2 board, upright (buttons on
 * top) is 270, so TinyBar ships with turn_180 on. Inverting `up` and turn_180 together changes nothing once the IMU
 * has a reading (both invert, and cancel); it only changes which pose "upright", the default, means. */
int brd_lcd_rotation(bool flipped, bool turn_180);

/* ---------------------------------------------------------------------------------------------------------------
 * The reading before the first frame (board_imu_read_flipped)
 *
 * The QMI8658A datasheet (Rev A, table 7 and section 7.3): after the accelerometer is enabled, its first sample comes
 * after about 3 ms plus one sample period, and the output takes 3/ODR more to settle (48 ms at 62.5 Hz, with the
 * low-pass filter on); "the new data will be stable in at least 3 samples ... discard the first several samples".
 * Before its first sample the data registers read zero. So a reading taken too early averages zeros and a ramp, and
 * can't tell which way up the bar is (what 1.0.1 did: it waited a fixed 40 ms and averaged four samples).
 *
 * Here, each new sample (STATUS0's data-ready bit) is fed with its time since the enable, and:
 *   - samples from the first BRD_BOOT_SETTLE_MS are dropped (turn-on and filter settling),
 *   - a sample whose total isn't 0.8 to 1.2 g (BRD_OR_MAG_*, the classifier's range) is dropped and starts the run
 *     again (zeros, a ramp, a jolt),
 *   - a sample that differs from the run's last one by more than BRD_OR_JITTER_MG on any axis starts a new run,
 *   - BRD_BOOT_SAMPLES good samples in a row are averaged and classified with BRD_OR_BOOT_MG.
 * The device gives up BRD_BOOT_WAIT_MS after the enable (the first frame waits on this) and uses the run it has.
 * ------------------------------------------------------------------------------------------------------------- */

#define BRD_BOOT_SETTLE_MS  51      /* 3 ms + 3/ODR at 62.5 Hz */
#define BRD_BOOT_WAIT_MS    150
#define BRD_BOOT_SAMPLES    3

typedef struct {
    int32_t sx, sy, sz;     /* sums over the current run of good samples */
    int32_t lx, ly, lz;     /* the run's last sample */
    uint8_t good;           /* good samples in the current run */
    uint8_t seen;           /* samples fed (saturates at 255), and why some were dropped: */
    uint8_t early;          /*   during turn-on and settling */
    uint8_t implausible;    /*   total not 0.8 to 1.2 g */
    uint8_t restarts;       /*   runs started again by a jump */
} brd_boot_read_t;

void brd_boot_read_init(brd_boot_read_t *b);
/* One new sample, t_ms after the accelerometer was enabled. True once BRD_BOOT_SAMPLES good samples in a row are in. */
bool brd_boot_read_feed(brd_boot_read_t *b, int32_t ax_mg, int32_t ay_mg, int32_t az_mg, int32_t t_ms);
/* The mean of the current run (rounded to nearest). False if it has no good sample yet. */
bool brd_boot_read_mean(const brd_boot_read_t *b, int32_t *ax_mg, int32_t *ay_mg, int32_t *az_mg);

/* Where the starting orientation came from. */
typedef enum { BRD_BOOT_FROM_IMU = 0, BRD_BOOT_FROM_MEMORY, BRD_BOOT_DEFAULT } brd_boot_src_t;

/* The starting orientation (true = flipped): the reading (brd_orient_classify: 0, 1 or -1) if it can tell, else the
 * remembered one (0, 1 or -1 for none), else upright, which is buttons on top. */
bool brd_orient_boot_choice(int reading, int remembered, brd_boot_src_t *src);

/* ---------------------------------------------------------------------------------------------------------------
 * Remembering the orientation (NVS, namespace "board", key "pose")
 *
 * What's stored is the pose: which IMU axis pointed up (a brd_up_axis_t), not "flipped", so a later build with another
 * CONFIG_TINYBAR_IMU_UP still reads it the right way (a pose along an axis it doesn't use is ignored). It's written
 * only when the bar has stood still in an orientation other than the stored one for BRD_OR_REMEMBER_MS (the vote
 * held, with no jolt), so a jiggle or a quick turn costs no flash: one write per turn held 10 s, a few dozen a day at
 * most (a flip is how the Pomodoro starts).
 * It's used only when the start-up reading can't tell (the bar lying flat, the IMU not answering).
 * ------------------------------------------------------------------------------------------------------------- */

#define BRD_OR_REMEMBER_MS  10000

/* The axis that points up in that orientation. */
brd_up_axis_t brd_orient_pose(brd_up_axis_t up, bool flipped);
/* A stored pose (any value) as an orientation under `up`: 0 upright, 1 flipped, -1 (another axis, or not a pose). */
int brd_orient_from_pose(brd_up_axis_t up, int pose);
/* Called on every sample after brd_orient_feed(). stored is the remembered orientation (0, 1, or -1 for none).
 * Returns the orientation to write now (0 or 1), or -1 for nothing. */
int brd_orient_to_remember(const brd_orient_t *o, int stored, int64_t now_ms);

/* ---------------------------------------------------------------------------------------------------------------
 * Sound synthesis (mock-up chime() and tickSound(); docs/decisions.md "Ticking during focus")
 *
 * chime  three sine notes 0.18 s apart, each a 20 ms linear attack to 0.18 of full scale, then an exponential decay
 *        to 0.0001 at 0.9 s, stopped at 1.0 s: 784, 988, 1175 Hz going to a break, 1175, 988, 784 Hz back to focus.
 *        Peaks at about -13.1 dBFS.
 * tick   25 ms of white noise shaped by (1 - t)^6, through Web Audio's band-pass biquad (Q 1.6) at 2000 Hz or
 *        1700 Hz (alternating), with 15 ms of filter tail. Rendered at the reference level BRD_TICK_REF_PEAK, the
 *        median peak of the mock-up's random noise at 48 kHz (-10.8 dBFS); the player then applies 0.55 (Soft) or
 *        1.10 (Medium), as the mock-up does, for peaks of -16.0 and -10.0 dBFS: 2.9 dB below and 3.1 dB above the
 *        chime's peak. A 25 ms tick sounds far quieter than its peak suggests, so these sit near the chime's peak.
 *        (Raised twice on 2026-10-05 after tests on the real bar: 0.10/0.25, then 0.25/0.45, were both too quiet.)
 * The noise comes from a fixed-seed generator, so every tick of a pitch is identical and the levels are exact.
 * ------------------------------------------------------------------------------------------------------------- */

#define BRD_AUDIO_RATE        24000     /* Hz, as 08_Audio_Test */
#define BRD_CHIME_SECONDS     1.36f     /* last note starts at 0.36 s and stops at 1.36 s */
#define BRD_TICK_MS           40        /* 25 ms burst + 15 ms tail */
#define BRD_TICK_REF_PEAK     0.289f
#define BRD_TICK_GAIN_SOFT    0.55f
#define BRD_TICK_GAIN_MEDIUM  1.10f

size_t brd_chime_samples(uint32_t rate);
void brd_chime_render(int16_t *out, size_t n, bool to_break, uint32_t rate);
size_t brd_tick_samples(uint32_t rate);
void brd_tick_render(int16_t *out, size_t n, float center_hz, uint32_t rate);
/* Peak of a buffer in dBFS (full scale 32767); -200 for silence. */
float brd_peak_dbfs(const int16_t *pcm, size_t n);

/* ---------------------------------------------------------------------------------------------------------------
 * Player: what the audio task writes to the codec, as mono samples in chunks.
 *
 * Two voices are mixed, as the mock-up's Web Audio does: the chime (a new chime restarts it) and the tick. While
 * ticking is on (level 1 Soft, 2 Medium), a tick starts once a second, the first one a second after ticking was
 * turned on, alternating 2000 and 1700 Hz. Stop silences both at once, turns ticking off and lets the amplifier go.
 * The amplifier is wanted while a voice plays, while ticking is on, and for BRD_AMP_HOLD_MS after the last sound, so
 * it isn't switched for every tick.
 * ------------------------------------------------------------------------------------------------------------- */

#define BRD_TICK_PERIOD_MS  1000
#define BRD_AMP_HOLD_MS     1500
#define BRD_AMP_LEAD_MS     40      /* silence after switching the amplifier on, before the sound */

typedef struct {
    const int16_t *pcm;
    size_t len, pos;
    int32_t gain_q15;       /* 32768 = 1.0 */
    bool active;
} brd_voice_t;

typedef struct {
    const int16_t *chime[2];    /* [0] back to focus (falling notes), [1] to a break (rising notes) */
    size_t chime_len;
    const int16_t *tick[2];     /* [0] 2000 Hz, [1] 1700 Hz, at BRD_TICK_REF_PEAK */
    size_t tick_len;
    brd_voice_t chime_v, tick_v;
    int tick_level;             /* 0 off, 1 Soft, 2 Medium */
    int64_t next_tick_ms;
    uint8_t tick_alt;
    int64_t quiet_since_ms;     /* when the last voice ended (or player init) */
} brd_player_t;

void brd_player_init(brd_player_t *p, const int16_t *chime_focus, const int16_t *chime_break, size_t chime_len,
                     const int16_t *tick_2000, const int16_t *tick_1700, size_t tick_len, int64_t now_ms);
void brd_player_chime(brd_player_t *p, bool to_break);
void brd_player_set_ticking(brd_player_t *p, int level, int64_t now_ms);
void brd_player_stop(brd_player_t *p, int64_t now_ms);
/* Fill up to max mono samples starting now (starting a tick if one is due). Returns how many were written: 0 when
 * nothing plays. Voices that end inside the chunk leave zeros after them. */
size_t brd_player_fill(brd_player_t *p, int64_t now_ms, int16_t *out, size_t max);
/* -1: nothing to do until a command arrives; 0: a voice is playing; > 0: ms until the next tick is due. */
int64_t brd_player_wait_ms(const brd_player_t *p, int64_t now_ms);
bool brd_player_amp_wanted(const brd_player_t *p, int64_t now_ms);

/* ---------------------------------------------------------------------------------------------------------------
 * RTC: the PCF85063's seven time registers, 0x04 (seconds) to 0x0A (years), in BCD, 24-hour mode. TinyBar keeps UTC
 * in it. Bit 7 of the seconds register is OS (oscillator stopped): set at power-on, cleared by writing the time.
 * ------------------------------------------------------------------------------------------------------------- */

#define BRD_RTC_MIN_YEAR 2024   /* earlier dates (the chip's 2000 default) mean "never set" */

int64_t brd_days_from_civil(int64_t y, unsigned m, unsigned d);
void brd_civil_from_days(int64_t days, int64_t *y, unsigned *m, unsigned *d);
/* UTC seconds -> registers (OS clear). Years outside 2000..2099 are clamped into it. */
void brd_rtc_encode(int64_t utc, uint8_t regs[7]);
/* Registers -> UTC seconds. False if OS is set, any field isn't valid BCD or is out of range, or the year is before
 * BRD_RTC_MIN_YEAR. */
bool brd_rtc_decode(const uint8_t regs[7], int64_t *utc);

/* ---------------------------------------------------------------------------------------------------------------
 * Touch: the AXS15231B answers the read command b5 ab a5 5a 00 00 00 0e 00 00 00 with 32 bytes: [1] is the number of
 * points (1 to 4 means touched), [2..3] and [4..5] the first point's 12-bit raw X (along the panel's 640 px side) and
 * Y (along its 172 px side). As in 10_LVGL_V9_Test's default (unrotated) mapping and 11_FactoryProgram, the native
 * panel point is x = raw Y, y = 639 - raw X. LVGL then turns it by the display's rotation (lv_display_rotate_point),
 * so this is right whichever way the layout is turned.
 * ------------------------------------------------------------------------------------------------------------- */

#define BRD_PANEL_W 172     /* native, portrait */
#define BRD_PANEL_H 640

bool brd_touch_decode(const uint8_t *buf, size_t len, int16_t *x, int16_t *y);

#ifdef __cplusplus
}
#endif
