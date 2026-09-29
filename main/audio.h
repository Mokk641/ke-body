/* ES8311 codec + I2S: push-to-talk recording (16 kHz mono WAV) and WAV playback. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#define AUDIO_REC_RATE        16000
#define AUDIO_REC_MAX_SECONDS 30
#define AUDIO_DEFAULT_VOLUME  70

typedef enum {
    AUDIO_EVT_REC_START,   /* recording began */
    AUDIO_EVT_REC_DONE,    /* wav/wav_len = complete WAV file (valid until the callback returns) */
    AUDIO_EVT_PLAY_START,
    AUDIO_EVT_PLAY_DONE,
} audio_evt_t;

typedef void (*audio_cb_t)(audio_evt_t evt, const uint8_t *wav, size_t wav_len);

/* Called from the audio task. */
esp_err_t audio_init(i2c_master_bus_handle_t bus, audio_cb_t cb);
bool audio_ready(void);

void audio_record_start(void);   /* no-op if already recording; aborts playback */
void audio_record_stop(void);    /* no-op if not recording */
bool audio_is_recording(void);

/* Validates the WAV header (PCM 16-bit, 1-2 channels, supported rate) and queues
 * playback. On ESP_OK the buffer is owned (and later freed) by the audio module;
 * on error the caller still owns it. */
esp_err_t audio_play_wav(uint8_t *wav, size_t len);

esp_err_t audio_set_volume(int percent);   /* 0..100 */
int audio_get_volume(void);

/* Diagnostics for the serial console. */
esp_err_t audio_test_tone(int ms);      /* play a 1 kHz sine through the normal playback path */
void audio_dump_regs(void);             /* print ES8311 registers */
esp_err_t audio_set_mic_gain(int step); /* 0..7 = 0..42 dB in 6 dB steps */
