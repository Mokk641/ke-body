#include "music.h"
#include "mp3src.h"
#include "audio.h"
#include "storage.h"
#include "ui.h"
#include "ui_render.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "music";

#define MUSIC_DIR "/sdcard/MUSIC"

typedef struct {
    char title[MUSIC_TITLE_LEN];
    char path[112];              /* empty for the song in RAM */
} track_t;

static track_t *s_tracks;
static char (*s_titles)[MUSIC_TITLE_LEN];
static int s_n;
static SemaphoreHandle_t s_lock;
static uint8_t *s_ram;           /* the pushed song when there is no card */
static size_t s_ram_len;

/* one playing (or paused) track */
typedef struct {
    FILE *f;
    const uint8_t *mem;
    size_t mem_len, mem_pos;
    mp3_stream_t *mp3;
    size_t total;
    long frames;
    int rate, kbps0;
    int tick;
} ctx_t;

static ctx_t *s_cur;             /* the stream that the UI talks about */
static int s_index = -1;
static volatile bool s_loaded, s_paused;
static volatile int s_elapsed_s, s_total_s, s_progress_pm;

/* ---- telling the UI ------------------------------------------------------------------------------------------- */

static void publish(void)
{
    music_info_t mi = {0};
    mi.count = s_n;
    mi.current = s_loaded ? s_index : -1;
    mi.playing = s_loaded && !s_paused;
    mi.paused = s_loaded && s_paused;
    mi.elapsed_s = s_elapsed_s;
    mi.total_s = s_total_s;
    mi.progress_pm = s_progress_pm;
    mi.volume = audio_get_volume();
    if (s_loaded && s_index >= 0 && s_index < s_n) snprintf(mi.title, sizeof mi.title, "%s", s_tracks[s_index].title);
    ui_set_music(&mi, (const char (*)[MUSIC_TITLE_LEN])s_titles);
    ui_set_music_playing(mi.playing);
}

/* ---- list ------------------------------------------------------------------------------------------------------ */

static int cmp_track(const void *a, const void *b) { return strcmp(((const track_t *)a)->title, ((const track_t *)b)->title); }

static void copy_title(char *dst, size_t n, const char *src)
{
    size_t len = strlen(src);
    if (len >= n) {
        len = n - 1;
        while (len > 0 && (src[len] & 0xC0) == 0x80) len--;           /* do not cut a UTF-8 character in half */
    }
    memcpy(dst, src, len);
    dst[len] = 0;
}

static void fill_titles(void)
{
    for (int i = 0; i < s_n; i++) snprintf(s_titles[i], MUSIC_TITLE_LEN, "%s", s_tracks[i].title);
}

void music_rescan(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* keep the pushed song in RAM (it is not a file) */
    track_t ram_track = {0};
    bool have_ram = s_ram != NULL && s_n > 0 && s_tracks[0].path[0] == 0;
    if (have_ram) ram_track = s_tracks[0];
    s_n = 0;
    if (have_ram) s_tracks[s_n++] = ram_track;
    storage_init();
    DIR *d = storage_is_sd() ? opendir(MUSIC_DIR) : NULL;
    int first = s_n;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL && s_n < MUSIC_MAX_TRACKS) {
            size_t l = strlen(e->d_name);
            if (l < 5 || e->d_name[0] == '.' || strcasecmp(e->d_name + l - 4, ".mp3") != 0 || e->d_type == DT_DIR) continue;
            char base[MUSIC_TITLE_LEN * 2];
            snprintf(base, sizeof base, "%.*s", (int)(l - 4), e->d_name);
            copy_title(s_tracks[s_n].title, MUSIC_TITLE_LEN, base);
            snprintf(s_tracks[s_n].path, sizeof s_tracks[s_n].path, "%s/%.90s", MUSIC_DIR, e->d_name);
            s_n++;
        }
        closedir(d);
        qsort(s_tracks + first, (size_t)(s_n - first), sizeof(track_t), cmp_track);
    }
    fill_titles();
    xSemaphoreGive(s_lock);
    if (s_index >= s_n) { s_index = -1; s_loaded = false; }
    ESP_LOGI(TAG, "%d song(s)%s", s_n, storage_is_sd() ? "" : " (no SD card)");
    publish();
}

int music_count(void) { return s_n; }
const char *music_title(int i) { return (i >= 0 && i < s_n) ? s_tracks[i].title : ""; }
const char (*music_titles(void))[MUSIC_TITLE_LEN] { return (const char (*)[MUSIC_TITLE_LEN])s_titles; }

/* ---- playback ---------------------------------------------------------------------------------------------------- */

static int src_read(void *c, uint8_t *dst, int n)
{
    ctx_t *x = c;
    if (x->f) return (int)fread(dst, 1, (size_t)n, x->f);
    size_t left = x->mem_len - x->mem_pos;
    if ((size_t)n > left) n = (int)left;
    memcpy(dst, x->mem + x->mem_pos, (size_t)n);
    x->mem_pos += (size_t)n;
    return n;
}

static int stream_read(void *c, int16_t *pcm, int max_frames, int *rate)
{
    ctx_t *x = c;
    (void)max_frames;
    int n = mp3s_next(x->mp3, pcm, rate);
    if (n <= 0) return n;
    x->frames += n;
    x->rate = *rate;
    if (++x->tick >= 20 && x == s_cur) {                        /* about every half second */
        x->tick = 0;
        int el = (int)(x->frames / *rate);
        size_t used = mp3s_consumed(x->mp3);
        int total = 0;
        if (x->total > 0) {
            if (el >= 3 && used > 0) total = (int)((double)x->total * el / (double)used);      /* VBR-proof: measured rate so far */
            else if (mp3s_kbps(x->mp3) > 0) total = (int)(x->total * 8 / (1000 * (size_t)mp3s_kbps(x->mp3)));
        }
        s_elapsed_s = el;
        s_total_s = total;
        s_progress_pm = x->total ? (int)(used * 1000 / x->total) : 0;
        publish();
    }
    return n;
}

static void stream_done(void *c, bool finished)
{
    ctx_t *x = c;
    bool mine = x == s_cur;
    if (x->mp3) mp3s_close(x->mp3);
    if (x->f) fclose(x->f);
    free(x);
    if (!mine) return;                                          /* a newer track already replaced this one */
    s_cur = NULL;
    int next = s_index + 1;
    if (finished && next < s_n) {
        music_play(next);                                       /* on to the next song */
        return;
    }
    s_loaded = false;
    s_paused = false;
    s_elapsed_s = s_total_s = s_progress_pm = 0;
    publish();
}

bool music_play(int index)
{
    if (index < 0 || index >= s_n) return false;
    ctx_t *x = calloc(1, sizeof *x);
    if (!x) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    track_t t = s_tracks[index];
    xSemaphoreGive(s_lock);
    if (t.path[0]) {
        x->f = fopen(t.path, "rb");
        if (!x->f) { free(x); ESP_LOGW(TAG, "cannot open %s", t.path); return false; }
        fseek(x->f, 0, SEEK_END);
        x->total = (size_t)ftell(x->f);
        fseek(x->f, 0, SEEK_SET);
    } else {
        if (!s_ram) { free(x); return false; }
        x->mem = s_ram;
        x->mem_len = x->total = s_ram_len;
    }
    x->mp3 = mp3s_open((mp3_reader_t){ src_read, x }, x->total);
    if (!x->mp3) { if (x->f) fclose(x->f); free(x); return false; }
    audio_stream_t st = { .read = stream_read, .done = stream_done, .ctx = x };
    s_cur = x;
    s_index = index;
    s_loaded = true;
    s_paused = false;
    s_elapsed_s = s_total_s = s_progress_pm = 0;
    if (audio_stream_start(&st) != ESP_OK) {
        s_cur = NULL; s_loaded = false;
        mp3s_close(x->mp3); if (x->f) fclose(x->f); free(x);
        publish();
        return false;
    }
    ESP_LOGI(TAG, "playing #%d %s", index, t.title);
    publish();
    return true;
}

void music_toggle(void)
{
    if (!s_loaded) {
        if (s_n > 0) music_play(s_index >= 0 && s_index < s_n ? s_index : 0);
        return;
    }
    s_paused = !s_paused;
    audio_stream_pause(s_paused);
    publish();
}

void music_next(void)
{
    if (s_n == 0) return;
    music_play(s_loaded ? (s_index + 1) % s_n : (s_index + 1 < s_n && s_index >= 0 ? s_index + 1 : 0));
}

void music_prev(void)
{
    if (s_n == 0) return;
    if (s_loaded && s_elapsed_s > 3) { music_play(s_index); return; }          /* first press: back to the start of this song */
    music_play(s_index > 0 ? s_index - 1 : s_n - 1);
}

void music_stop(void)
{
    audio_stream_stop();
    s_loaded = false;
    s_paused = false;
    s_cur = NULL;
    publish();
}

bool music_is_playing(void) { return s_loaded && !s_paused; }
bool music_is_paused(void) { return s_loaded && s_paused; }
int music_current(void) { return s_loaded ? s_index : -1; }

void music_refresh(void) { publish(); }

void music_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_tracks = heap_caps_calloc(MUSIC_MAX_TRACKS, sizeof(track_t), MALLOC_CAP_SPIRAM);
    s_titles = heap_caps_calloc(MUSIC_MAX_TRACKS, MUSIC_TITLE_LEN, MALLOC_CAP_SPIRAM);
}

/* ---- songs pushed by the PC ------------------------------------------------------------------------------------------ */

struct music_sink {
    FILE *f;
    uint8_t *ram;
    size_t cap, len;
    char title[MUSIC_TITLE_LEN];
    char path[112];
};

static void safe_name(char *dst, size_t n, const char *title)
{
    size_t o = 0;
    for (size_t i = 0; title[i] && o + 1 < n; i++) {
        unsigned char c = (unsigned char)title[i];
        dst[o++] = (c < 32 || strchr("/\\:*?\"<>|", c)) ? '_' : (char)c;
    }
    dst[o] = 0;
    if (!dst[0]) snprintf(dst, n, "song");
}

music_sink_t *music_sink_open(const char *title, size_t expected_len)
{
    music_sink_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    copy_title(s->title, sizeof s->title, title && title[0] ? title : "song");
    storage_init();
    if (storage_is_sd()) {
        mkdir(MUSIC_DIR, 0777);
        char file[MUSIC_TITLE_LEN + 8];
        safe_name(file, MUSIC_TITLE_LEN, s->title);
        snprintf(s->path, sizeof s->path, "%s/%.60s.mp3", MUSIC_DIR, file);
        s->f = fopen(s->path, "wb");
        if (!s->f) { free(s); return NULL; }
    } else {
        if (expected_len == 0 || expected_len > MUSIC_RAM_MAX) { free(s); return NULL; }
        s->ram = heap_caps_malloc(expected_len, MALLOC_CAP_SPIRAM);
        if (!s->ram) { free(s); return NULL; }
        s->cap = expected_len;
    }
    return s;
}

bool music_sink_write(music_sink_t *s, const uint8_t *data, size_t len)
{
    if (s->f) return fwrite(data, 1, len, s->f) == len && (s->len += len, true);
    if (s->len + len > s->cap) return false;
    memcpy(s->ram + s->len, data, len);
    s->len += len;
    return true;
}

const char *music_sink_title(const music_sink_t *s) { return s->title; }

bool music_sink_finish(music_sink_t *s, bool ok)
{
    if (s->f) { fclose(s->f); s->f = NULL; }
    if (!ok || s->len < 128) {
        if (s->path[0]) unlink(s->path);
        free(s->ram);
        free(s);
        return false;
    }
    if (s->ram) {                                              /* no card: this song replaces the one in RAM */
        bool was_ram_playing = s_loaded && s_index >= 0 && s_index < s_n && s_tracks[s_index].path[0] == 0;
        if (was_ram_playing) { music_stop(); vTaskDelay(pdMS_TO_TICKS(150)); }
        uint8_t *old = s_ram;
        s_ram = s->ram;
        s_ram_len = s->len;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_n > 0 && s_tracks[0].path[0] == 0 && old) {} else {
            memmove(&s_tracks[1], &s_tracks[0], sizeof(track_t) * (size_t)(s_n < MUSIC_MAX_TRACKS ? s_n : MUSIC_MAX_TRACKS - 1));
            if (s_n < MUSIC_MAX_TRACKS) s_n++;
        }
        snprintf(s_tracks[0].title, sizeof s_tracks[0].title, "%s", s->title);
        s_tracks[0].path[0] = 0;
        fill_titles();
        xSemaphoreGive(s_lock);
        if (old) { vTaskDelay(pdMS_TO_TICKS(100)); free(old); }
        s->ram = NULL;
    } else {                                                   /* card: rescan, then put the new file first */
        music_rescan();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < s_n; i++) {
            if (strcmp(s_tracks[i].path, s->path) == 0) {
                track_t t = s_tracks[i];
                memmove(&s_tracks[1], &s_tracks[0], sizeof(track_t) * (size_t)i);
                s_tracks[0] = t;
                break;
            }
        }
        fill_titles();
        xSemaphoreGive(s_lock);
    }
    free(s);
    music_play(0);
    return true;
}
