/* Audio path for Waveshare ESP32-S3-Touch-LCD-3.5(-C).
 *
 * Sources (official):
 *  - I2S pins / codec address / 16-bit stereo Philips slots / MCLK from the MCLK pin:
 *    waveshareteam/ESP32-S3-Touch-LCD-3.5  ESP-IDF/07_lvgl_wifi/components/esp_port/esp_es8311_port.cpp
 *    (I2S_NUM_0, MCLK=GPIO12, BCLK=GPIO13, LRCK=GPIO15, DOUT=GPIO16, DIN=GPIO14, pa_pin = none)
 *  - Codec init / volume 70 / analog mic:  Arduino/examples/01_audio_out, 04_es8311_example
 *  - Driver: components/es8311 (Espressif es8311 driver shipped in the official Arduino libraries)
 *  - ESP-IDF's own examples/peripherals/i2s/i2s_codec/i2s_es8311 uses the same driver/config.
 *
 * The codec runs at 16 kHz while recording and at the WAV's own rate while playing
 * (16 k / 24 k / others in the ES8311 divider table), MCLK = 256 * fs.
 * The I2S wire format is always 16-bit stereo; mono <-> stereo is done in software.
 */
#include "audio.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <stdio.h>
#include "es8311.h"

static const char *TAG = "audio";

#define PIN_I2S_MCLK  GPIO_NUM_12
#define PIN_I2S_BCLK  GPIO_NUM_13
#define PIN_I2S_WS    GPIO_NUM_15
#define PIN_I2S_DOUT  GPIO_NUM_16   /* ESP32 -> codec DAC */
#define PIN_I2S_DIN   GPIO_NUM_14   /* codec ADC -> ESP32 */
#define ES8311_ADDR   ES8311_ADDRESS_0
#define MCLK_MULTIPLE 256
#define MIC_GAIN      ES8311_MIC_GAIN_30DB

#define FRAMES_PER_CHUNK 256                       /* 16 ms @ 16 kHz */
#define CHUNK_BYTES      (FRAMES_PER_CHUNK * 2 * 2) /* stereo int16 */
#define WAV_HDR          44
#define REC_MAX_SAMPLES  (AUDIO_REC_RATE * AUDIO_REC_MAX_SECONDS)

typedef enum { CMD_REC, CMD_PLAY } cmd_type_t;
typedef struct { cmd_type_t type; uint8_t *buf; size_t len; } cmd_t;

static i2s_chan_handle_t s_tx, s_rx;
static es8311_handle_t s_codec;
static int s_rate;
static QueueHandle_t s_q;
static audio_cb_t s_cb;
static bool s_ready;
static volatile bool s_recording;
static volatile bool s_stop_rec;
static uint8_t *s_rec_buf;
static int16_t *s_chunk;
static int s_volume = AUDIO_DEFAULT_VOLUME;

/* ---- WAV helpers ---------------------------------------------------------- */

static void put_le32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put_le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static uint32_t get_le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t get_le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

static void wav_write_header(uint8_t *h, int rate, int channels, uint32_t pcm_bytes)
{
    memcpy(h, "RIFF", 4);
    put_le32(h + 4, 36 + pcm_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put_le32(h + 16, 16);
    put_le16(h + 20, 1);                          /* PCM */
    put_le16(h + 22, channels);
    put_le32(h + 24, rate);
    put_le32(h + 28, rate * channels * 2);
    put_le16(h + 32, channels * 2);
    put_le16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put_le32(h + 40, pcm_bytes);
}

typedef struct { int rate, channels, bits; const uint8_t *pcm; size_t pcm_len; } wav_info_t;

static bool wav_parse(const uint8_t *d, size_t len, wav_info_t *w)
{
    if (len < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return false;
    size_t pos = 12;
    bool have_fmt = false;
    memset(w, 0, sizeof *w);
    while (pos + 8 <= len) {
        uint32_t csize = get_le32(d + pos + 4);
        const uint8_t *body = d + pos + 8;
        size_t avail = len - pos - 8;
        if (!memcmp(d + pos, "fmt ", 4) && csize >= 16 && avail >= 16) {
            uint16_t fmt = get_le16(body);
            if (fmt != 1 && fmt != 0xFFFE) return false;
            w->channels = get_le16(body + 2);
            w->rate = get_le32(body + 4);
            w->bits = get_le16(body + 14);
            have_fmt = true;
        } else if (!memcmp(d + pos, "data", 4)) {
            w->pcm = body;
            w->pcm_len = csize < avail ? csize : avail;
            return have_fmt && w->pcm_len > 0;
        }
        pos += 8 + csize + (csize & 1);
    }
    return false;
}

static bool rate_supported(int hz)
{
    static const int ok[] = { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 };
    for (unsigned i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) if (ok[i] == hz) return true;
    return false;
}

/* ---- hardware -------------------------------------------------------------- */

static esp_err_t set_rate(int hz)
{
    if (hz == s_rate) return ESP_OK;
    ESP_LOGI(TAG, "sample rate %d -> %d", s_rate, hz);
    i2s_channel_disable(s_tx);
    i2s_channel_disable(s_rx);
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(hz);
    clk.mclk_multiple = MCLK_MULTIPLE;
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_tx, &clk), TAG, "tx clk");
    ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(s_rx, &clk), TAG, "rx clk");
    ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(s_codec, hz * MCLK_MULTIPLE, hz), TAG, "codec fs");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "tx en");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "rx en");
    s_rate = hz;
    return ESP_OK;
}

static esp_err_t i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;   /* send silence instead of stale data on underrun */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, &s_rx), TAG, "new channel");

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_REC_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din = PIN_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std.clk_cfg.mclk_multiple = MCLK_MULTIPLE;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "tx std");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std), TAG, "rx std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "tx enable");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "rx enable");
    s_rate = AUDIO_REC_RATE;
    return ESP_OK;
}

static esp_err_t codec_init(i2c_master_bus_handle_t bus)
{
    s_codec = es8311_create(bus, ES8311_ADDR);
    ESP_RETURN_ON_FALSE(s_codec, ESP_FAIL, TAG, "es8311 create");
    const es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = AUDIO_REC_RATE * MCLK_MULTIPLE,
        .sample_frequency = AUDIO_REC_RATE,
    };
    ESP_RETURN_ON_ERROR(es8311_init(s_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16), TAG, "es8311 init");

    /* Extra registers that the esp_codec_dev ES8311 driver (used by the official
     * Waveshare ESP-IDF examples, esp-adf components/esp_codec_dev/device/es8311/es8311.c:
     * es8311_open + es8311_start) writes and the small esp-bsp driver above does not.
     * Values copied verbatim so the codec ends up in the same state as under the
     * official example. */
    static const uint8_t extra[][2] = {
        { 0x0B, 0x00 }, { 0x0C, 0x00 },   /* system */
        { 0x10, 0x1F }, { 0x11, 0x7F },   /* system: analog bias / VMID */
        { 0x1B, 0x0A },                   /* ADC HPF */
        { 0x44, 0x58 },                   /* internal reference signal (ADCL + DACR), esp_codec_dev default */
        { 0x17, 0xBF },                   /* ADC digital volume 0 dB (esp-bsp writes 0xC8) */
        { 0x15, 0x40 },                   /* ADC ramp rate */
        { 0x45, 0x00 },                   /* GP control */
    };
    for (unsigned i = 0; i < sizeof(extra) / sizeof(extra[0]); i++) {
        ESP_RETURN_ON_ERROR(es8311_write_register(s_codec, extra[i][0], extra[i][1]), TAG, "es8311 extra reg");
    }
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(s_codec, s_volume, NULL), TAG, "volume");
    ESP_RETURN_ON_ERROR(es8311_microphone_config(s_codec, false), TAG, "mic");
    ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(s_codec, MIC_GAIN), TAG, "mic gain");
    uint8_t id1 = 0, id2 = 0, ver = 0;
    es8311_read_register(s_codec, 0xFD, &id1);
    es8311_read_register(s_codec, 0xFE, &id2);
    es8311_read_register(s_codec, 0xFF, &ver);
    if (id1 == 0x83 && id2 == 0x11) {
        ESP_LOGI(TAG, "ES8311 found: id 0x%02x%02x version 0x%02x", id1, id2, ver);
    } else {
        ESP_LOGW(TAG, "ES8311 chip id mismatch: read 0x%02x 0x%02x (expected 0x83 0x11)", id1, id2);
    }
    return ESP_OK;
}

/* ---- recording / playback (audio task) ------------------------------------- */

static void do_record(void)
{
    if (set_rate(AUDIO_REC_RATE) != ESP_OK) return;
    int16_t *pcm = (int16_t *)(s_rec_buf + WAV_HDR);
    size_t n = 0;
    if (s_stop_rec) {
        /* finger already lifted while this command waited in the queue (e.g. behind an upload) */
        ESP_LOGI(TAG, "release before record start, skipped");
        return;
    }
    s_recording = true;
    if (s_cb) s_cb(AUDIO_EVT_REC_START, NULL, 0);

    /* drop whatever was sitting in the RX DMA buffers before the press */
    size_t got;
    for (int i = 0; i < 4; i++) {
        if (i2s_channel_read(s_rx, s_chunk, CHUNK_BYTES, &got, 0) != ESP_OK) break;
    }

    while (!s_stop_rec && n < REC_MAX_SAMPLES) {
        if (i2s_channel_read(s_rx, s_chunk, CHUNK_BYTES, &got, pdMS_TO_TICKS(200)) != ESP_OK) continue;
        size_t frames = got / 4;
        for (size_t i = 0; i < frames && n < REC_MAX_SAMPLES; i++) {
            /* average L and R: works whether the codec drives one or both slots */
            pcm[n++] = (int16_t)(((int)s_chunk[2 * i] + (int)s_chunk[2 * i + 1]) / 2);
        }
    }
    s_recording = false;
    wav_write_header(s_rec_buf, AUDIO_REC_RATE, 1, (uint32_t)(n * 2));
    ESP_LOGI(TAG, "recorded %u samples (%.1f s)", (unsigned)n, (double)n / AUDIO_REC_RATE);
    if (s_cb) s_cb(AUDIO_EVT_REC_DONE, s_rec_buf, WAV_HDR + n * 2);
}

static void do_play(uint8_t *wav, size_t len)
{
    wav_info_t w;
    if (!wav_parse(wav, len, &w) || set_rate(w.rate) != ESP_OK) {
        ESP_LOGE(TAG, "cannot play this wav");
        return;
    }
    ESP_LOGI(TAG, "play %d Hz %d ch, %u bytes", w.rate, w.channels, (unsigned)w.pcm_len);
    if (s_cb) s_cb(AUDIO_EVT_PLAY_START, NULL, 0);

    const int16_t *src = (const int16_t *)w.pcm;
    size_t frames_total = w.pcm_len / (2 * w.channels);
    size_t pos = 0;
    size_t bytes_out = 0;
    int64_t t0 = esp_timer_get_time();
    while (pos < frames_total) {
        /* a newer command (record / another play) preempts this playback */
        if (uxQueueMessagesWaiting(s_q) > 0) break;
        size_t frames = frames_total - pos;
        if (frames > FRAMES_PER_CHUNK) frames = FRAMES_PER_CHUNK;
        for (size_t i = 0; i < frames; i++) {
            if (w.channels == 1) {
                int16_t s = src[pos + i];
                s_chunk[2 * i] = s;
                s_chunk[2 * i + 1] = s;
            } else {
                s_chunk[2 * i] = src[(pos + i) * w.channels];
                s_chunk[2 * i + 1] = src[(pos + i) * w.channels + 1];
            }
        }
        size_t written = 0;
        esp_err_t werr = i2s_channel_write(s_tx, s_chunk, frames * 4, &written, pdMS_TO_TICKS(1000));
        bytes_out += written;
        if (werr != ESP_OK) {
            ESP_LOGE(TAG, "i2s write failed: %s", esp_err_to_name(werr));
            break;
        }
        pos += frames;
    }
    vTaskDelay(pdMS_TO_TICKS(80));   /* let the DMA drain before reporting done */
    ESP_LOGI(TAG, "play done: %u/%u frames, %u bytes on I2S in %d ms (volume %d)",
             (unsigned)pos, (unsigned)frames_total, (unsigned)bytes_out,
             (int)((esp_timer_get_time() - t0) / 1000), s_volume);
    if (s_cb) s_cb(AUDIO_EVT_PLAY_DONE, NULL, 0);
}

static void audio_task(void *arg)
{
    cmd_t c;
    for (;;) {
        if (xQueueReceive(s_q, &c, portMAX_DELAY) != pdTRUE) continue;
        switch (c.type) {
        case CMD_REC:
            do_record();
            break;
        case CMD_PLAY:
            do_play(c.buf, c.len);
            free(c.buf);
            break;
        }
    }
}

/* ---- public API ------------------------------------------------------------- */

esp_err_t audio_init(i2c_master_bus_handle_t bus, audio_cb_t cb)
{
    s_cb = cb;
    s_rec_buf = heap_caps_malloc(WAV_HDR + REC_MAX_SAMPLES * 2, MALLOC_CAP_SPIRAM);
    s_chunk = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_rec_buf && s_chunk, ESP_ERR_NO_MEM, TAG, "buffers");
    ESP_RETURN_ON_ERROR(i2s_init(), TAG, "i2s");
    ESP_RETURN_ON_ERROR(codec_init(bus), TAG, "codec");
    s_q = xQueueCreate(4, sizeof(cmd_t));
    xTaskCreatePinnedToCore(audio_task, "audio", 6144, NULL, 6, NULL, 0);
    s_ready = true;
    ESP_LOGI(TAG, "ready (volume %d)", s_volume);
    return ESP_OK;
}

bool audio_ready(void) { return s_ready; }
bool audio_is_recording(void) { return s_recording; }

void audio_record_start(void)
{
    if (!s_ready || s_recording) return;
    s_stop_rec = false;
    cmd_t c = { .type = CMD_REC };
    xQueueSend(s_q, &c, 0);
}

void audio_record_stop(void)
{
    s_stop_rec = true;
}

esp_err_t audio_play_wav(uint8_t *wav, size_t len)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    wav_info_t w;
    if (!wav_parse(wav, len, &w)) return ESP_ERR_INVALID_ARG;
    if (w.bits != 16 || w.channels < 1 || w.channels > 2 || !rate_supported(w.rate)) return ESP_ERR_NOT_SUPPORTED;
    cmd_t c = { .type = CMD_PLAY, .buf = wav, .len = len };
    if (xQueueSend(s_q, &c, 0) != pdTRUE) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

esp_err_t audio_set_volume(int percent)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    esp_err_t err = es8311_voice_volume_set(s_codec, percent, NULL);
    if (err == ESP_OK) s_volume = percent;
    return err;
}

int audio_get_volume(void) { return s_volume; }

/* ---- diagnostics (serial console) ------------------------------------------- */

esp_err_t audio_test_tone(int ms)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (ms < 100) ms = 100;
    if (ms > 10000) ms = 10000;
    const int rate = AUDIO_REC_RATE;
    size_t n = (size_t)rate * ms / 1000;
    size_t len = WAV_HDR + n * 2;
    uint8_t *wav = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!wav) wav = malloc(len);
    if (!wav) return ESP_ERR_NO_MEM;
    wav_write_header(wav, rate, 1, (uint32_t)(n * 2));
    int16_t *pcm = (int16_t *)(wav + WAV_HDR);
    for (size_t i = 0; i < n; i++) {
        /* 1 kHz sine, amplitude 8000, 20 ms fade in/out to avoid clicks */
        float env = 1.0f;
        size_t fade = rate / 50;
        if (i < fade) env = (float)i / fade;
        else if (n - i < fade) env = (float)(n - i) / fade;
        pcm[i] = (int16_t)(8000.0f * env * sinf(2.0f * 3.14159265f * 1000.0f * i / rate));
    }
    ESP_LOGI(TAG, "test tone: 1 kHz, %d ms, %u bytes", ms, (unsigned)len);
    esp_err_t err = audio_play_wav(wav, len);
    if (err != ESP_OK) free(wav);
    return err;
}

void audio_dump_regs(void)
{
    if (!s_ready) { printf("audio: not ready\n"); return; }
    printf("ES8311 registers (rate %d Hz, volume %d):\n", s_rate, s_volume);
    for (int reg = 0; reg <= 0x45; reg++) {
        uint8_t v = 0;
        esp_err_t err = es8311_read_register(s_codec, (uint8_t)reg, &v);
        if (reg % 8 == 0) printf("  %02x:", reg);
        if (err == ESP_OK) printf(" %02x", v); else printf(" ??");
        if (reg % 8 == 7) printf("\n");
    }
    printf("\n");
    uint8_t a = 0, b = 0, cver = 0;
    es8311_read_register(s_codec, 0xFD, &a);
    es8311_read_register(s_codec, 0xFE, &b);
    es8311_read_register(s_codec, 0xFF, &cver);
    printf("  chip id %02x %02x (expect 83 11), version %02x\n", a, b, cver);
    printf("  meaning: 0D/0E analog power, 12 DAC power (00=on), 13 HP drive (10=on), 14 mic, 31 mute, 32 DAC volume\n");
}

esp_err_t audio_set_mic_gain(int step)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (step < 0 || step > 7) return ESP_ERR_INVALID_ARG;
    return es8311_microphone_gain_set(s_codec, (es8311_mic_gain_t)step);
}
