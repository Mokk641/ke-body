/* Host test for main/agc.c */
#include <stdio.h>
#include <stdlib.h>
#include "agc.h"

static int fails;
#define CHECK(cond, ...) do { if (cond) printf("ok:   "); else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static int peak_of(const int16_t *p, size_t n) { int m = 0; for (size_t i = 0; i < n; i++) { int v = abs(p[i]); if (v > m) m = v; } return m; }

int main(void)
{
    int16_t quiet[1000];
    for (int i = 0; i < 1000; i++) quiet[i] = (int16_t)((i % 50 - 25) * 144);          /* peak 3600 = 0.11 of full scale, her whisper */
    float g = agc_apply(quiet, 1000, 0.8f, 25.0f, 60);
    CHECK(g > 7.0f && g < 7.5f, "a whisper at 0.11 of full scale gets x%.1f", g);
    int pk = peak_of(quiet, 1000);
    CHECK(pk > 26000 && pk <= 26214 + 40, "and ends at about 0.8 of full scale (%d)", pk);

    int16_t loud[100];
    for (int i = 0; i < 100; i++) loud[i] = (int16_t)(i % 2 ? 30000 : -30000);
    CHECK(agc_apply(loud, 100, 0.8f, 25.0f, 60) == 1.0f && peak_of(loud, 100) == 30000, "a loud recording is not turned down");

    int16_t silence[100];
    for (int i = 0; i < 100; i++) silence[i] = (int16_t)(i % 3 - 1);
    CHECK(agc_apply(silence, 100, 0.8f, 25.0f, 60) == 1.0f && peak_of(silence, 100) == 1, "near-silence (noise) is left alone, not blown up");

    int16_t faint[100];
    for (int i = 0; i < 100; i++) faint[i] = (int16_t)(i % 2 ? 200 : -200);
    g = agc_apply(faint, 100, 0.8f, 25.0f, 60);
    CHECK(g == 25.0f && peak_of(faint, 100) == 5000, "very faint sound is capped at x25 (%.0f)", g);

    int16_t clip[4] = { 32000, -32000, 100, -100 };
    agc_apply(clip, 4, 1.4f, 25.0f, 60);
    CHECK(clip[0] == 32767 && clip[1] == -32768, "results are clipped, never wrapped");

    CHECK(agc_level(0) == 0 && agc_level(100) == 0 && agc_level(32768) == 100, "level: silence 0, full scale 100");
    CHECK(agc_level(3600) > 55 && agc_level(3600) < 85, "level of a whisper (%d) is clearly visible", agc_level(3600));
    CHECK(agc_level(1000) < agc_level(3000) && agc_level(3000) < agc_level(10000), "louder = longer bar");
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
