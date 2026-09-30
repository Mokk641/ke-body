/* Optional screen colour calibration on the byte-swapped RGB565 framebuffer (pure C, host-testable).
 * gamma x100 (100 = none; > 100 darkens the mid-tones) and per-channel gains in percent. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void colorcal_set(int gamma_x100, int r_pct, int g_pct, int b_pct);   /* values are clamped */
void colorcal_get(int out[4]);
bool colorcal_active(void);                                           /* false while everything is 100 */
void colorcal_apply(uint16_t *dst, const uint16_t *src, size_t n);    /* dst and src hold byte-swapped RGB565 */
