/*
 * brd_sound.c: the chime and tick synthesis and the two-voice player. Owner: board builder.
 * A port of the mock-up's chime() and tickSound() Web Audio graphs (docs/mockup.html). No ESP-IDF headers.
 */
#include <math.h>
#include <string.h>

#include "brd_logic.h"

#define TWO_PI 6.283185307179586f

/* ---------- Chime ---------- */

static const uint16_t NOTES_TO_BREAK[3] = {784, 988, 1175};
static const uint16_t NOTES_TO_FOCUS[3] = {1175, 988, 784};
#define NOTE_GAP_S   0.18f
#define NOTE_STOP_S  1.0f
#define ATTACK_S     0.02f
#define DECAY_END_S  0.9f
#define PEAK_GAIN    0.18f
#define FLOOR_GAIN   0.0001f
#define LN_DECAY     (-7.4955419f)  /* ln(FLOOR_GAIN / PEAK_GAIN) = -ln 1800 */

size_t brd_chime_samples(uint32_t rate)
{
    return (size_t)lroundf(BRD_CHIME_SECONDS * (float)rate);
}

/* The gain envelope of one note, t seconds after it starts: setValueAtTime(0), linearRampToValueAtTime(0.18, 0.02),
 * exponentialRampToValueAtTime(0.0001, 0.9), then held until stop() at 1.0 s. */
static float note_gain(float t)
{
    if (t < 0.0f || t >= NOTE_STOP_S) return 0.0f;
    if (t < ATTACK_S) return PEAK_GAIN * t / ATTACK_S;
    if (t < DECAY_END_S) return PEAK_GAIN * expf(LN_DECAY * (t - ATTACK_S) / (DECAY_END_S - ATTACK_S));
    return FLOOR_GAIN;
}

static int16_t to_pcm(float v)
{
    float s = v * 32767.0f;
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    return (int16_t)lroundf(s);
}

void brd_chime_render(int16_t *out, size_t n, bool to_break, uint32_t rate)
{
    const uint16_t *notes = to_break ? NOTES_TO_BREAK : NOTES_TO_FOCUS;
    uint32_t start[3];
    for (int k = 0; k < 3; k++) start[k] = (uint32_t)lroundf((float)k * NOTE_GAP_S * (float)rate);
    for (size_t i = 0; i < n; i++) {
        float acc = 0.0f;
        for (int k = 0; k < 3; k++) {
            if (i < start[k]) continue;
            uint32_t j = (uint32_t)i - start[k];
            float t = (float)j / (float)rate;
            float g = note_gain(t);
            if (g == 0.0f) continue;
            /* The exact phase: (j * f mod rate) / rate cycles, so float precision never drifts. */
            float cycles = (float)(((uint64_t)j * notes[k]) % rate) / (float)rate;
            acc += g * sinf(TWO_PI * cycles);
        }
        out[i] = to_pcm(acc);
    }
}

/* ---------- Meeting-start chime (decisions.md "Meeting-start sound (2026-10-07)"; mock-up meetChimeNotes()) ----------
 * Two soft rising notes, A5 (880 Hz) then E6 (1319 Hz) 0.14 s apart, each a 20 ms linear attack to 0.10, an exponential
 * decay to 0.0001 at 0.65 s, stopped at 0.7 s: lighter and shorter than the Pomodoro chime's three (peak 0.18), so it
 * reads as "a meeting is starting", not "the timer is done". */

static const uint16_t MEETING_NOTES[2] = {880, 1319};
#define MEETING_GAP_S      0.14f
#define MEETING_STOP_S     0.70f
#define MEETING_DECAY_END  0.65f
#define MEETING_PEAK       0.10f
#define MEETING_LN_DECAY   (-6.9077553f)   /* ln(0.0001 / 0.10) = -ln 1000 */

size_t brd_meeting_chime_samples(uint32_t rate)
{
    return (size_t)lroundf(BRD_MEETING_CHIME_SECONDS * (float)rate);
}

static float meeting_gain(float t)
{
    if (t < 0.0f || t >= MEETING_STOP_S) return 0.0f;
    if (t < ATTACK_S) return MEETING_PEAK * t / ATTACK_S;
    if (t < MEETING_DECAY_END) return MEETING_PEAK * expf(MEETING_LN_DECAY * (t - ATTACK_S) / (MEETING_DECAY_END - ATTACK_S));
    return FLOOR_GAIN;
}

void brd_meeting_chime_render(int16_t *out, size_t n, uint32_t rate)
{
    uint32_t start[2];
    for (int k = 0; k < 2; k++) start[k] = (uint32_t)lroundf((float)k * MEETING_GAP_S * (float)rate);
    for (size_t i = 0; i < n; i++) {
        float acc = 0.0f;
        for (int k = 0; k < 2; k++) {
            if (i < start[k]) continue;
            uint32_t j = (uint32_t)i - start[k];
            float g = meeting_gain((float)j / (float)rate);
            if (g == 0.0f) continue;
            float cycles = (float)(((uint64_t)j * MEETING_NOTES[k]) % rate) / (float)rate;
            acc += g * sinf(TWO_PI * cycles);
        }
        out[i] = to_pcm(acc);
    }
}

/* ---------- Tick ---------- */

size_t brd_tick_samples(uint32_t rate)
{
    return (size_t)((uint64_t)rate * BRD_TICK_MS / 1000);
}

static uint32_t xorshift32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* Run the noise burst through the band-pass filter. With out == NULL, return the peak; otherwise write the result
 * scaled by k. The generator is seeded the same way each time, so both passes see the same noise. */
static float tick_pass(int16_t *out, size_t n, float center_hz, uint32_t rate, float k)
{
    size_t burst = (size_t)lroundf(0.025f * (float)rate);
    /* Web Audio's BiquadFilterNode "bandpass" (constant 0 dB peak gain), Q 1.6. */
    float w0 = TWO_PI * center_hz / (float)rate;
    float alpha = sinf(w0) / (2.0f * 1.6f);
    float a0 = 1.0f + alpha;
    float b0 = alpha / a0, b2 = -alpha / a0, a1 = -2.0f * cosf(w0) / a0, a2 = (1.0f - alpha) / a0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0, peak = 0;
    uint32_t seed = 0x7469636bu ^ (uint32_t)center_hz;      /* "tick", per pitch */
    for (size_t i = 0; i < n; i++) {
        float x = 0.0f;
        if (i < burst) {
            float u = (float)(xorshift32(&seed) >> 8) / 16777216.0f;     /* [0, 1) */
            float env = powf(1.0f - (float)i / (float)burst, 6.0f);
            x = (u * 2.0f - 1.0f) * env;
        }
        float v = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = v;
        if (fabsf(v) > peak) peak = fabsf(v);
        if (out) out[i] = to_pcm(v * k);
    }
    return peak;
}

void brd_tick_render(int16_t *out, size_t n, float center_hz, uint32_t rate)
{
    float peak = tick_pass(NULL, n, center_hz, rate, 0.0f);
    tick_pass(out, n, center_hz, rate, peak > 0.0f ? BRD_TICK_REF_PEAK / peak : 0.0f);
}

float brd_peak_dbfs(const int16_t *pcm, size_t n)
{
    int32_t peak = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = pcm[i] < 0 ? -(int32_t)pcm[i] : pcm[i];
        if (v > peak) peak = v;
    }
    if (peak == 0) return -200.0f;
    return 20.0f * log10f((float)peak / 32767.0f);
}

/* ---------- Player ---------- */

static void voice_start(brd_voice_t *v, const int16_t *pcm, size_t len, int32_t gain_q15)
{
    v->pcm = pcm;
    v->len = len;
    v->pos = 0;
    v->gain_q15 = gain_q15;
    v->active = pcm && len;
}

static int32_t tick_gain_q15(int level)
{
    return (int32_t)lroundf((level >= 2 ? BRD_TICK_GAIN_MEDIUM : BRD_TICK_GAIN_SOFT) * 32768.0f);
}

void brd_player_init(brd_player_t *p, const int16_t *chime_focus, const int16_t *chime_break, size_t chime_len,
                     const int16_t *tick_2000, const int16_t *tick_1700, size_t tick_len, int64_t now_ms)
{
    memset(p, 0, sizeof *p);
    p->chime[0] = chime_focus;
    p->chime[1] = chime_break;
    p->chime_len = chime_len;
    p->tick[0] = tick_2000;
    p->tick[1] = tick_1700;
    p->tick_len = tick_len;
    p->quiet_since_ms = now_ms - BRD_AMP_HOLD_MS;   /* start with the amplifier off */
}

void brd_player_chime(brd_player_t *p, bool to_break)
{
    voice_start(&p->chime_v, p->chime[to_break ? 1 : 0], p->chime_len, 32768);
}

void brd_player_set_meeting_chime(brd_player_t *p, const int16_t *pcm, size_t len)
{
    p->meeting = pcm;
    p->meeting_len = len;
}

/* On the chime's voice: the chime that was playing (a Pomodoro alarm's) gives way to it. */
void brd_player_meeting_chime(brd_player_t *p)
{
    voice_start(&p->chime_v, p->meeting, p->meeting_len, 32768);
}

void brd_player_set_ticking(brd_player_t *p, int level, int64_t now_ms)
{
    if (level <= 0) {
        p->tick_level = 0;
        return;
    }
    if (level > 2) level = 2;
    if (p->tick_level == 0) p->next_tick_ms = now_ms + BRD_TICK_PERIOD_MS;
    p->tick_level = level;
}

void brd_player_stop(brd_player_t *p, int64_t now_ms)
{
    p->chime_v.active = false;
    p->tick_v.active = false;
    p->tick_level = 0;
    p->quiet_since_ms = now_ms - BRD_AMP_HOLD_MS;   /* no hold: the amplifier goes off at once */
}

static void voice_mix(brd_voice_t *v, int32_t *acc, size_t n)
{
    if (!v->active) return;
    size_t left = v->len - v->pos;
    size_t k = n < left ? n : left;
    for (size_t i = 0; i < k; i++) acc[i] += ((int32_t)v->pcm[v->pos + i] * v->gain_q15) >> 15;
    v->pos += k;
    if (v->pos >= v->len) v->active = false;
}

size_t brd_player_fill(brd_player_t *p, int64_t now_ms, int16_t *out, size_t max)
{
    if (p->tick_level > 0 && now_ms >= p->next_tick_ms) {
        voice_start(&p->tick_v, p->tick[p->tick_alt], p->tick_len, tick_gain_q15(p->tick_level));
        p->tick_alt ^= 1;
        p->next_tick_ms += BRD_TICK_PERIOD_MS;
        if (p->next_tick_ms <= now_ms) p->next_tick_ms = now_ms + BRD_TICK_PERIOD_MS;  /* fell behind: don't burst */
    }
    if (!p->chime_v.active && !p->tick_v.active) return 0;

    /* How much of this chunk has sound: the longer of the two voices' remainders. */
    size_t n = 0;
    if (p->chime_v.active) n = p->chime_v.len - p->chime_v.pos;
    if (p->tick_v.active && p->tick_v.len - p->tick_v.pos > n) n = p->tick_v.len - p->tick_v.pos;
    if (n > max) n = max;

    int32_t acc[128];
    size_t done = 0;
    while (done < n) {
        size_t k = n - done < 128 ? n - done : 128;
        memset(acc, 0, k * sizeof acc[0]);
        voice_mix(&p->chime_v, acc, k);
        voice_mix(&p->tick_v, acc, k);
        for (size_t i = 0; i < k; i++) {
            int32_t v = acc[i];
            out[done + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        done += k;
    }
    if (!p->chime_v.active && !p->tick_v.active) p->quiet_since_ms = now_ms;
    return n;
}

int64_t brd_player_wait_ms(const brd_player_t *p, int64_t now_ms)
{
    if (p->chime_v.active || p->tick_v.active) return 0;
    if (p->tick_level > 0) return p->next_tick_ms > now_ms ? p->next_tick_ms - now_ms : 0;
    return -1;
}

bool brd_player_amp_wanted(const brd_player_t *p, int64_t now_ms)
{
    if (p->chime_v.active || p->tick_v.active || p->tick_level > 0) return true;
    return now_ms - p->quiet_since_ms < BRD_AMP_HOLD_MS;
}
