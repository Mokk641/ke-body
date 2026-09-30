#include "mp3src.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#define BUF_SIZE (16 * 1024)

struct mp3_stream {
    mp3_reader_t rd;
    mp3dec_t dec;
    uint8_t *buf;
    int pos, valid;
    bool eof;
    bool started;
    size_t consumed, total;
    long frames;
    int rate, kbps;
    int junk;                       /* consecutive bytes thrown away without finding a frame */
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
};

static void refill(mp3_stream_t *s)
{
    if (s->pos > 0) {
        memmove(s->buf, s->buf + s->pos, (size_t)s->valid);
        s->pos = 0;
    }
    while (!s->eof && s->valid < BUF_SIZE) {
        int r = s->rd.read(s->rd.ctx, s->buf + s->valid, BUF_SIZE - s->valid);
        if (r <= 0) { s->eof = true; break; }
        s->valid += r;
        if (r < 512) break;                                 /* a slow source: work with what we have */
    }
}

mp3_stream_t *mp3s_open(mp3_reader_t reader, size_t total_bytes)
{
    mp3_stream_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->buf = malloc(BUF_SIZE);
    if (!s->buf) { free(s); return NULL; }
    s->rd = reader;
    s->total = total_bytes;
    mp3dec_init(&s->dec);
    return s;
}

void mp3s_close(mp3_stream_t *s)
{
    if (!s) return;
    free(s->buf);
    free(s);
}

/* skip an ID3v2 tag at the very start (it can be tens of KB: album art) */
static void skip_id3(mp3_stream_t *s)
{
    refill(s);
    if (s->valid < 10 || memcmp(s->buf, "ID3", 3) != 0) return;
    size_t size = 10 + (((size_t)s->buf[6] & 0x7F) << 21) + (((size_t)s->buf[7] & 0x7F) << 14) +
                  (((size_t)s->buf[8] & 0x7F) << 7) + ((size_t)s->buf[9] & 0x7F);
    if (s->buf[5] & 0x10) size += 10;                       /* footer */
    s->consumed += size;
    while (size > 0) {
        int take = size < (size_t)s->valid ? (int)size : s->valid;
        s->pos += take; s->valid -= take; size -= (size_t)take;
        if (size == 0) break;
        s->pos = 0; s->valid = 0;
        if (s->eof) return;
        refill(s);
        if (s->valid == 0) return;
    }
}

int mp3s_next(mp3_stream_t *s, int16_t *out, int *rate)
{
    if (!s->started) { s->started = true; skip_id3(s); }
    for (;;) {
        if (s->valid < 4096 && !s->eof) refill(s);
        if (s->valid <= 0) return s->frames ? 0 : (s->junk ? -1 : 0);
        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&s->dec, s->buf + s->pos, s->valid, s->pcm, &info);
        if (info.frame_bytes > 0) {
            s->pos += info.frame_bytes;
            s->valid -= info.frame_bytes;
            s->consumed += (size_t)info.frame_bytes;
        }
        if (samples > 0) {
            s->junk = 0;
            s->rate = info.hz;
            s->kbps = info.bitrate_kbps;
            if (info.channels == 2) {
                memcpy(out, s->pcm, (size_t)samples * 2 * sizeof(int16_t));
            } else {
                for (int i = 0; i < samples; i++) out[2 * i] = out[2 * i + 1] = s->pcm[i];
            }
            s->frames += samples;
            if (rate) *rate = info.hz;
            return samples;
        }
        if (info.frame_bytes == 0) {                        /* not enough data for a frame */
            if (s->eof) { s->valid = 0; return 0; }
            if (s->valid >= BUF_SIZE) { s->pos += 1; s->valid -= 1; s->junk++; }      /* stuck on garbage */
            else refill(s);
        } else {
            s->junk += info.frame_bytes;                    /* skipped junk or a frame it could not decode */
            if (s->frames == 0 && s->junk > 256 * 1024) return -1;                       /* this is not an MP3 */
        }
    }
}

size_t mp3s_consumed(const mp3_stream_t *s) { return s->consumed; }
long mp3s_decoded_frames(const mp3_stream_t *s) { return s->frames; }
int mp3s_rate(const mp3_stream_t *s) { return s->rate; }
int mp3s_kbps(const mp3_stream_t *s) { return s->kbps; }
