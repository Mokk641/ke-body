/* Recording helpers (pure C, host-testable): automatic gain for a quiet voice, loudness for the screen bar. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Bring the loudest sample to `target` (fraction of full scale, e.g. 0.8) but never amplify by more than max_gain
 * and never turn the volume down; near-silence (peak below `floor`) is left alone. Returns the gain applied (1.0 = none). */
float agc_apply(int16_t *pcm, size_t n, float target, float max_gain, int floor);

/* Peak sample (0..32768) -> 0..100 on a dB scale (-50 dB and below = 0, full scale = 100). */
int agc_level(int peak);
