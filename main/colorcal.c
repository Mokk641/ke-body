#include "colorcal.h"
#include <math.h>
#include <string.h>

static bool s_on;
static uint8_t s_lut_r[32], s_lut_g[64], s_lut_b[32];
static int s_cal[4] = { 100, 100, 100, 100 };

static void build_lut(uint8_t *lut, int n, int gamma_x100, int gain_pct)
{
    for (int i = 0; i < n; i++) {
        float x = (float)i / (float)(n - 1);
        float y = powf(x, (float)gamma_x100 / 100.f) * (float)gain_pct / 100.f;
        int v = (int)(y * (float)(n - 1) + 0.5f);
        lut[i] = (uint8_t)(v < 0 ? 0 : (v > n - 1 ? n - 1 : v));
    }
}

void colorcal_set(int gamma_x100, int r_pct, int g_pct, int b_pct)
{
    if (gamma_x100 < 50) gamma_x100 = 50;
    if (gamma_x100 > 250) gamma_x100 = 250;
    int *v[3] = { &r_pct, &g_pct, &b_pct };
    for (int i = 0; i < 3; i++) {
        if (*v[i] < 40) *v[i] = 40;
        if (*v[i] > 100) *v[i] = 100;
    }
    s_cal[0] = gamma_x100; s_cal[1] = r_pct; s_cal[2] = g_pct; s_cal[3] = b_pct;
    build_lut(s_lut_r, 32, gamma_x100, r_pct);
    build_lut(s_lut_g, 64, gamma_x100, g_pct);
    build_lut(s_lut_b, 32, gamma_x100, b_pct);
    s_on = !(gamma_x100 == 100 && r_pct == 100 && g_pct == 100 && b_pct == 100);
}

void colorcal_get(int out[4]) { memcpy(out, s_cal, sizeof s_cal); }

bool colorcal_active(void) { return s_on; }

void colorcal_apply(uint16_t *dst, const uint16_t *src, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint16_t p = src[i];
        p = (uint16_t)((p << 8) | (p >> 8));
        uint16_t o = (uint16_t)((s_lut_r[p >> 11] << 11) | (s_lut_g[(p >> 5) & 63] << 5) | s_lut_b[p & 31]);
        dst[i] = (uint16_t)((o << 8) | (o >> 8));
    }
}
