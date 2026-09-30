#include "agc.h"
#include <math.h>

float agc_apply(int16_t *pcm, size_t n, float target, float max_gain, int floor)
{
    int peak = 0;
    for (size_t i = 0; i < n; i++) {
        int v = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (v > peak) peak = v;
    }
    if (peak <= floor) return 1.0f;
    float g = target * 32767.0f / (float)peak;
    if (g > max_gain) g = max_gain;
    if (g <= 1.05f) return 1.0f;
    for (size_t i = 0; i < n; i++) {
        int v = (int)((float)pcm[i] * g);
        pcm[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
    return g;
}

int agc_level(int peak)
{
    if (peak <= 0) return 0;
    float db = 20.0f * log10f((float)peak / 32768.0f);
    int level = (int)((db + 50.0f) * 2.0f);
    return level < 0 ? 0 : (level > 100 ? 100 : level);
}
