/* Music player: MP3 files from /sdcard/MUSIC, or a song pushed by the PC (POST /music) that is kept in PSRAM when there
 * is no card. Decoding runs in the audio task (minimp3); everything else here is just bookkeeping. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define MUSIC_MAX_TRACKS 100
#define MUSIC_TITLE_LEN  64
#define MUSIC_RAM_MAX    (5 * 1024 * 1024)     /* biggest song that is kept in PSRAM (no SD card) */

void music_init(void);
void music_refresh(void);                    /* tell the UI the current state again (after a volume change) */
void music_rescan(void);                     /* look at /sdcard/MUSIC again (a card may have been inserted) */
int music_count(void);
const char *music_title(int index);
const char (*music_titles(void))[MUSIC_TITLE_LEN];

bool music_play(int index);                  /* start this track (false: no such track) */
void music_toggle(void);                     /* play / pause; starts the first track when idle */
void music_next(void);
void music_prev(void);
void music_stop(void);
bool music_is_playing(void);                 /* a track is loaded and not paused */
bool music_is_paused(void);
int music_current(void);                     /* index of the loaded track, -1 if none */

/* Receiving a song from the PC. open() before the first byte, write() for each piece, finish(true) at the end: the song
 * goes to the top of the list and starts playing. NULL from open(): too big for RAM and no card / card error. */
typedef struct music_sink music_sink_t;
music_sink_t *music_sink_open(const char *title, size_t expected_len);
bool music_sink_write(music_sink_t *s, const uint8_t *data, size_t len);
bool music_sink_finish(music_sink_t *s, bool ok);
const char *music_sink_title(const music_sink_t *s);
