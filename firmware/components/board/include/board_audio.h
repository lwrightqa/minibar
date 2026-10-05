/*
 * board_audio.h: the ES8311 codec and speaker: the Pomodoro chime (also the alarm, which core repeats every 4 s) and
 * the optional focus tick. Owner: board builder.
 *
 * Sounds are synthesized once at init into PCM (16-bit, 24 kHz, as 08_Audio_Test uses) at the mock-up's digital
 * levels, and mixed by a small audio task, so callers never block:
 *   chime  three sine notes 0.18 s apart, each a 20 ms attack to 0.18 of full scale and an exponential decay to
 *          silence at 0.9 s (mock-up chime()): 784, 988, 1175 Hz going to a break, 1175, 988, 784 Hz back to focus.
 *          Peaks at -13.1 dBFS.
 *   tick   25 ms of white noise shaped by (1 - t)^6, band-passed at 2000 Hz and 1700 Hz alternately (Q 1.6), at gain
 *          0.55 (Soft) or 1.10 (Medium): peaks at -16.0 and -10.0 dBFS, near the chime's peak (a 25 ms tick sounds much quieter) (mock-up
 *          tickSound(), decisions.md "Ticking during focus"). One a second while ticking is on, the first a second
 *          after it's turned on.
 * A chime and a tick that overlap are mixed, as the mock-up's Web Audio does; a new chime restarts the chime.
 * Loudness is CONFIG_TINYBAR_AUDIO_VOLUME (menuconfig: TinyBar board). It's an open office: tune it on the real bar.
 * core decides when (TB_FX_CHIME, TB_FX_TICKING) and never asks for sound during a call or meeting.
 * The amplifier (EXIO7) is switched on around sounds when CONFIG_TINYBAR_AUDIO_AMP_GATE is set (the default).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Codec on I2C_NUM_0 + I2S (MCLK 7, BCLK 15, WS 46, DOUT 45), the sounds, the audio task. Call after
 * board_power_hold(). On failure the bar stays silent and the calls below do nothing. */
esp_err_t board_audio_init(void);
/* Play the chime once (to_break picks the rising or falling notes). Never blocks. */
void board_audio_chime(bool to_break);
/* Ticking: level 0 off, 1 Soft, 2 Medium. Never blocks. */
void board_audio_set_ticking(int level);
/* Stop every sound and turn ticking off (before power off and restart). Never blocks. */
void board_audio_stop(void);

#ifdef __cplusplus
}
#endif
