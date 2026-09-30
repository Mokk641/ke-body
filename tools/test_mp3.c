/* Host test for main/mp3src.c against minimp3's own conformance vectors (tools/testdata: .bit streams and reference .pcm). */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mp3src.h"

static int fails;
#define CHECK(cond, ...) do { if (cond) printf("ok:   "); else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

typedef struct { const uint8_t *d; size_t n, pos; int chunk; } mem_t;
static int mem_read(void *ctx, uint8_t *dst, int n)
{
    mem_t *m = ctx;
    if (n > m->chunk) n = m->chunk;                              /* a slow source: small reads */
    size_t left = m->n - m->pos;
    if ((size_t)n > left) n = (int)left;
    memcpy(dst, m->d + m->pos, (size_t)n);
    m->pos += (size_t)n;
    return n;
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n + 1);
    *len = fread(b, 1, (size_t)n, f);
    fclose(f);
    return b;
}

/* decode a whole vector; compare with the reference (mono references are the mono samples, we output stereo) */
static void run(const char *dir, const char *name, int chunk, int skip_frames, double tol)
{
    char p[512];
    size_t nb, np;
    snprintf(p, sizeof p, "%s/%s.bit", dir, name);
    uint8_t *bit = slurp(p, &nb);
    snprintf(p, sizeof p, "%s/%s.pcm", dir, name);
    uint8_t *ref = slurp(p, &np);
    if (!bit || !ref) { CHECK(0, "%s: fixture missing", name); return; }
    mem_t m = { bit, nb, 0, chunk };
    mp3_reader_t rd = { mem_read, &m };
    mp3_stream_t *s = mp3s_open(rd, nb);
    int16_t out[MP3S_MAX_FRAMES * 2];
    long total = 0, cap = 4L << 20;
    int16_t *all = malloc((size_t)cap * sizeof(int16_t));
    int rate = 0, n;
    while ((n = mp3s_next(s, out, &rate)) > 0 && total + n * 2 <= cap) { memcpy(all + total, out, (size_t)n * 2 * sizeof(int16_t)); total += n * 2; }
    CHECK(n == 0, "%s (reads of %d bytes): decoded to the end", name, chunk);
    /* the reference is 16-bit PCM, mono or stereo; ours is always interleaved stereo. `skip_frames` = leading frames of ours
     * that the reference decoder does not output (an Xing/LAME info frame decodes to silence). */
    long ref_samples = (long)(np / 2);
    bool stereo_ref = ref_samples > total * 3 / 4;
    const int16_t *r = (const int16_t *)ref;
    double err = 0; long cnt = 0;
    long off = skip_frames * 1152 * 2;
    if (stereo_ref) {
        for (long i = 0; i < ref_samples && off + i < total; i++) { err += fabs((double)all[off + i] - r[i]); cnt++; }
    } else {
        long ours_frames = total / 2;
        for (long i = 0; i < ref_samples && i < ours_frames; i++) { err += fabs((double)all[2 * i] - r[i]); cnt++; }
    }
    double mean = cnt ? err / cnt : 1e9;
    CHECK(cnt > 0 && mean < tol, "%s: output matches the reference decoder (%ld samples, mean error %.2f of a signal averaging 4000, rate %d Hz, %d kbps)", name, cnt, mean, mp3s_rate(s), mp3s_kbps(s));
    CHECK(mp3s_consumed(s) <= nb && mp3s_consumed(s) + 4096 >= nb, "%s: consumed %u of %u bytes", name, (unsigned)mp3s_consumed(s), (unsigned)nb);
    mp3s_close(s);
    free(all); free(bit); free(ref);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "tools/testdata";
    run(dir, "l3-hecommon", 100000, 1, 150);   /* intensity-stereo stream: minimp3 is not bit-exact with the reference here, a wrong alignment would be ~4000 */
    run(dir, "l3-hecommon", 700, 1, 150);               /* tiny reads, like a slow SD card */
    run(dir, "l3-si", 100000, 0, 4);
    run(dir, "l3-nonstandard-id3v2", 100000, 0, 4);   /* starts with an ID3v2 tag */

    /* not an MP3 at all */
    uint8_t junk[8192];
    for (unsigned i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 37 + 11);
    mem_t m = { junk, sizeof junk, 0, 4096 };
    mp3_stream_t *s = mp3s_open((mp3_reader_t){ mem_read, &m }, sizeof junk);
    int16_t out[MP3S_MAX_FRAMES * 2];
    int rate, n = mp3s_next(s, out, &rate);
    CHECK(n <= 0, "junk gives no audio (%d)", n);
    mp3s_close(s);
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
