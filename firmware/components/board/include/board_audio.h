/*
 * board_audio.h: the ES8311 codec and speaker: the Pomodoro chime and the optional focus tick. Owner: board builder.
 *
 * Sounds are synthesized once at init into PCM buffers (16-bit mono, 24 kHz, as 08_Audio_Test uses) and played by a
 * small audio task, so callers never block:
 *   chime  three sine notes 0.18 s apart, each with a 20 ms attack to 0.18 of full scale and an exponential decay to
 *          silence at 0.9 s (mock-up chime()): 784, 988, 1175 Hz going to a break, 1175, 988, 784 Hz back to focus.
 *   tick   25 ms of white noise shaped by (1 - t)^6, band-passed at 2000 Hz and 1700 Hz alternately (Q 1.6), at gain
 *          0.10 (Soft) or 0.25 (Medium) of the chime's scale: peaks about 18 dB and 10 dB below the chime
 *          (mock-up tickSound(), decisions.md "Ticking during focus"). One per second while ticking is on.
 * It's an open office: keep the codec volume modest and fixed; the levels above are relative to the chime.
 * core decides when (TB_FX_CHIME, TB_FX_TICKING) and never asks for sound during a call or meeting.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Codec on I2C_NUM_0 + I2S (MCLK 7, BCLK 15, WS 46, DOUT 45), the amplifier via EXIO7, the audio task. */
esp_err_t board_audio_init(void);
/* Play the chime once (to_break picks the rising or falling notes). Never blocks; a chime replaces a tick. */
void board_audio_chime(bool to_break);
/* Ticking: level 0 off, 1 Soft, 2 Medium. The audio task ticks once a second while it's on. */
void board_audio_set_ticking(int level);
/* Mute everything at once (before power off). */
void board_audio_stop(void);

#ifdef __cplusplus
}
#endif
