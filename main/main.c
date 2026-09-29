/* ke-body: a small body for the Waveshare ESP32-S3-Touch-LCD-3.5-C.
 *
 *  phase 1 (face)
 *  - boot: big kaomoji "(—_—)", IP in the corner, HTTP /ping /face /say
 *  - short tap anywhere: "(—//—)" for 2 s
 *  phase 2 (ears and mouth)
 *  - hold >= 0.5 s: record from the mic (16 kHz mono) until release (max 30 s),
 *    then POST the WAV to the server URL from NVS (serial: server http://host:8770/hear)
 *  - POST /play (WAV) plays on the speaker; POST /volume 0-100
 *  phase 3
 *  - AXP2101 power rails set up as in the official examples (speaker amp supply)
 *  - rotation 0/90/180/270 (default 90), brightness, night dimming (NTP), dark/light theme
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "board.h"
#include "pmic.h"
#include "ui.h"
#include "light.h"
#include "wifi_mgr.h"
#include "http_api.h"
#include "console_cmd.h"
#include "audio.h"
#include "settings.h"
#include "uploader.h"

static const char *TAG = "main";

#define WAITING_TEXT   "等待配网\n串口输入: wifi <ssid> <password>"
#define LONG_PRESS_US  500000

static bool s_showing_waiting;

static void on_wifi_state(wifi_mgr_state_t st, const char *ip)
{
    switch (st) {
    case WIFI_MGR_NO_CREDS:
        ui_set_corner("等待配网");
        ui_set_say(WAITING_TEXT);
        s_showing_waiting = true;
        break;
    case WIFI_MGR_CONNECTING:
        ui_set_corner("连接中...");
        break;
    case WIFI_MGR_CONNECTED:
        ui_set_corner(ip);
        if (s_showing_waiting) { ui_set_say(""); s_showing_waiting = false; }
        http_api_start();
        light_start_sntp();
        break;
    case WIFI_MGR_DISCONNECTED:
        ui_set_corner("已断开, 重连中");
        break;
    }
}

/* Runs in the audio task right after a recording ends. */
static void send_recording(const uint8_t *wav, size_t len)
{
    char url[160];
    if (!settings_get_str(SETTINGS_KEY_SERVER_URL, url, sizeof url)) {
        ui_set_say("未设置服务器地址\n串口输入: server http://电脑IP:8770/hear");
        return;
    }
    if (wifi_mgr_state() != WIFI_MGR_CONNECTED) {
        ui_set_say("没有网络，录音没发出去");
        return;
    }
    ui_override_corner("发送中");
    int status = 0;
    esp_err_t err = uploader_post_wav(url, wav, len, &status);
    ui_override_corner(NULL);
    if (err != ESP_OK) {
        char msg[96];
        snprintf(msg, sizeof msg, "发送失败: %s", esp_err_to_name(err));
        ui_set_say(msg);
    } else if (status / 100 != 2) {
        char msg[64];
        snprintf(msg, sizeof msg, "服务器返回 %d", status);
        ui_set_say(msg);
    }
}

static void on_audio(audio_evt_t evt, const uint8_t *wav, size_t len)
{
    switch (evt) {
    case AUDIO_EVT_REC_START:
        ui_override_face("(—o—)");
        ui_override_corner("在听");
        break;
    case AUDIO_EVT_REC_DONE:
        ui_override_face(NULL);
        ui_override_corner(NULL);
        send_recording(wav, len);
        break;
    case AUDIO_EVT_PLAY_START:
        ui_override_face("(—▽—)");
        break;
    case AUDIO_EVT_PLAY_DONE:
        ui_override_face(NULL);
        break;
    }
}

/* short tap -> blush; hold >= 0.5 s -> record until release */
static void touch_task(void *arg)
{
    bool was_down = false, long_started = false;
    int64_t t_down = 0;
    for (;;) {
        uint16_t x, y;
        bool down = board_touch_read(&x, &y);
        int64_t now = esp_timer_get_time();
        if (down && !was_down) {
            t_down = now;
            long_started = false;
            ESP_LOGI(TAG, "touch down at %u,%u", x, y);
        }
        if (down && !long_started && now - t_down >= LONG_PRESS_US) {
            long_started = true;
            if (audio_ready()) audio_record_start();
            else ui_blush();
        }
        if (!down && was_down) {
            if (long_started) audio_record_stop();
            else ui_blush();
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(board_init());

    /* power rails first (official examples do this before the codec) */
    if (pmic_init(board_i2c_bus()) != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 init failed; continuing without PMIC setup");
    }

    ui_start();                 /* rotation/theme from NVS, first frame */
    light_init();               /* brightness from NVS, night schedule */

    if (audio_init(board_i2c_bus(), on_audio) != ESP_OK) {
        ESP_LOGE(TAG, "audio init failed; recording/playback disabled");
    }

    xTaskCreate(touch_task, "touch", 3072, NULL, 4, NULL);

    wifi_mgr_start(on_wifi_state);
    console_cmd_start();
}
