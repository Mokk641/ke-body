/* MP3 -> PCM, streaming (pure C on top of minimp3, host-testable): feed it any byte source (an SD card file, a buffer in
 * PSRAM) and pull one decoded frame at a time. Output is always interleaved STEREO int16. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    /* copy up to n bytes to dst, return how many (0 = end of data, < 0 = error) */
    int (*read)(void *ctx, uint8_t *dst, int n);
    void *ctx;
} mp3_reader_t;

#define MP3S_MAX_FRAMES 1152        /* stereo frames per decoded MP3 frame at most */

typedef struct mp3_stream mp3_stream_t;

mp3_stream_t *mp3s_open(mp3_reader_t reader, size_t total_bytes);    /* total_bytes: file size (progress), may be 0 */
void mp3s_close(mp3_stream_t *s);

/* Decode the next frame into out (room for MP3S_MAX_FRAMES * 2 samples). Returns the number of stereo frames,
 * 0 at the end of the data, -1 if it is not MP3 at all. *rate = sample rate of this frame. */
int mp3s_next(mp3_stream_t *s, int16_t *out, int *rate);

size_t mp3s_consumed(const mp3_stream_t *s);       /* bytes of the file used so far */
long mp3s_decoded_frames(const mp3_stream_t *s);   /* stereo frames produced so far */
int mp3s_rate(const mp3_stream_t *s);              /* last frame's sample rate (0 before the first) */
int mp3s_kbps(const mp3_stream_t *s);              /* last frame's bit rate */
