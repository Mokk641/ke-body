/* ke-body: a small body for the Waveshare ESP32-S3-Touch-LCD-3.5-C.
 *
 *  phase 1  face, Wi-Fi, HTTP /ping /face /say, tap = blush
 *  phase 2  hold to talk (16 kHz WAV -> bridge /hear), /play, /volume
 *  phase 3  AXP2101 rails, rotation, brightness, night dimming, dark theme, amp enable (TCA9554 P7)
 *  phase 4  chat log + quick buttons (-> bridge /msg), animations, IMU (shake / face-down),
 *           silent alert, camera + gallery (-> bridge /photo), remote /snap when "peek" is on
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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
#include "bridge.h"
#include "app_actions.h"
#include "imu.h"
#include "cam_ui.h"
#include "picview.h"
#include "music.h"

static const char *TAG = "main";

#define WAITING_TEXT   "等待配网\n串口输入: wifi <ssid> <password>"

static bool s_showing_waiting;
static bool s_chime;

bool app_chime_enabled(void) { return s_chime; }
void app_set_chime(bool on) { s_chime = on; settings_set_str("chime", on ? "on" : "off"); }

void app_on_new_message(void)
{
    if (s_chime && !music_is_playing()) audio_play_chime();      /* a chime would cut the song off */
}


/* ---- Wi-Fi state -> screen ---------------------------------------------------- */

static void on_wifi_state(wifi_mgr_state_t st, const char *ip)
{
    switch (st) {
    case WIFI_MGR_NO_CREDS:
        ui_set_corner("等待配网");
        ui_toast(WAITING_TEXT, 60000);
        s_showing_waiting = true;
        break;
    case WIFI_MGR_CONNECTING:
        ui_set_corner("连接中...");
        break;
    case WIFI_MGR_CONNECTED:
        ui_set_corner(ip);
        if (s_showing_waiting) { ui_toast("", 0); s_showing_waiting = false; }
        http_api_start();
        light_start_sntp();
        break;
    case WIFI_MGR_DISCONNECTED:
        ui_set_corner("已断开, 重连中");
        break;
    }
}

/* ---- recording -> bridge /hear ---------------------------------------------------- */

/* Runs in the audio task right after a recording ends. */
static void send_recording(const uint8_t *wav, size_t len)
{
    char url[160];
    if (!settings_get_str(SETTINGS_KEY_SERVER_URL, url, sizeof url)) {
        ui_toast("未设置服务器地址\n串口输入: server http://电脑IP:8770/hear", 4000);
        return;
    }
    if (wifi_mgr_state() != WIFI_MGR_CONNECTED) {
        ui_toast("没有网络，录音没发出去", 3000);
        return;
    }
    ui_override_corner("发送中");
    int status = 0;
    esp_err_t err = uploader_post_wav(url, wav, len, &status);
    ui_override_corner(NULL);
    if (err != ESP_OK) {
        char msg[96];
        snprintf(msg, sizeof msg, "发送失败: %s", esp_err_to_name(err));
        ui_toast(msg, 3000);
    } else if (status / 100 != 2) {
        char msg[64];
        snprintf(msg, sizeof msg, "服务器返回 %d", status);
        ui_toast(msg, 3000);
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

/* ---- touch actions (called from the touch task via ui_touch) ------------------------ */

void app_send_text(const char *text)
{
    if (!text || !text[0]) return;
    ui_chat_add(CHAT_HER, text);
    bridge_send_text(text);
}

void app_on_tap(int hit)
{
    char buf[UI_BTN_TEXT_LEN];
    if (hit >= UI_HIT_TEXT_BTN0 && hit < UI_HIT_TEXT_BTN0 + UI_MAX_TEXT_BTN) {
        if (ui_button_text(false, hit - UI_HIT_TEXT_BTN0, buf, sizeof buf)) app_send_text(buf);
        return;
    }
    if (hit >= UI_HIT_EMOJI_BTN0 && hit < UI_HIT_EMOJI_BTN0 + UI_MAX_EMOJI_BTN) {
        if (ui_button_text(true, hit - UI_HIT_EMOJI_BTN0, buf, sizeof buf)) app_send_text(buf);
        return;
    }
    if (hit >= UI_HIT_MUSIC_ROW0 && hit < UI_HIT_MUSIC_ROW0 + MUSIC_MAX_TRACKS) {
        if (music_play(hit - UI_HIT_MUSIC_ROW0)) ui_set_screen(UI_SCREEN_MUSIC);
        return;
    }
    if (hit >= UI_HIT_PIC0 && hit < UI_HIT_PIC0 + UI_CHAT_MAX) {
        picview_open(ui_msg_pic_id(hit - UI_HIT_PIC0));
        return;
    }
    switch (hit) {
    case UI_HIT_MUSIC_OPEN:
        music_rescan();
        ui_set_screen(UI_SCREEN_MUSIC);
        break;
    case UI_HIT_MUSIC_PLAY:
        music_toggle();
        break;
    case UI_HIT_MUSIC_NEXT:
        music_next();
        break;
    case UI_HIT_MUSIC_PREV:
        music_prev();
        break;
    case UI_HIT_FACE:
    case UI_HIT_CHAT:
        ui_blush();
        break;
    case UI_HIT_CAM_BTN:
        camui_enter();
        break;
    case UI_HIT_CAM_VIEW:
        camui_shoot();
        break;
    case UI_HIT_CAM_GALLERY:
        camui_gallery_enter();
        break;
    case UI_HIT_CAM_BACK:
        camui_back();
        break;
    case UI_HIT_GAL_DELETE:
        camui_gallery_delete();
        break;
    case UI_HIT_GAL_SEND:
        camui_gallery_send();
        break;
    default:
        break;
    }
}

void app_on_long_press(void)
{
    if (audio_ready()) audio_record_start();
    else ui_blush();
}

void app_on_long_release(void)
{
    audio_record_stop();
}

void app_on_swipe(int dir)
{
    camui_gallery_step(dir);
}

static void on_imu(imu_evt_t evt, int arg)
{
    switch (evt) {
    case IMU_EVT_SHAKE:
        ui_shake();
        if (!ui_is_sleeping()) app_send_text(ui_shake_text());
        break;
    case IMU_EVT_FACE_DOWN:
        ui_set_sleeping(true);
        light_set_override(LIGHT_MIN_BRIGHT);
        break;
    case IMU_EVT_FACE_UP:
        ui_set_sleeping(false);
        light_set_override(-1);
        break;
    case IMU_EVT_ORIENTATION:
        if (arg != ui_get_rotation()) {
            /* auto-rotate does not touch the saved "rotate" setting */
            ESP_LOGI(TAG, "auto-rotate -> %d", arg);
            board_lcd_set_rotation(arg);
            ui_refresh_after_rotation();
        }
        break;
    }
}

void app_on_touch_activity(void)
{
    if (ui_is_sleeping()) {
        ui_set_sleeping(false);
        light_set_override(-1);
    }
}

void app_set_volume(int percent)
{
    audio_set_volume(percent);
    music_refresh();
}

void app_send_ink(const uint8_t *png, size_t len)
{
    bridge_send_ink(png, len);
}

static void touch_task(void *arg)
{
    bool was_down = false;
    uint16_t lx = 0, ly = 0;   /* last position seen while the finger was down */
    int miss = 0;
    for (;;) {
        uint16_t x = 0, y = 0;
        bool down = board_touch_read(&x, &y);
        if (down) {
            lx = x; ly = y; miss = 0; was_down = true;
        } else if (was_down && ++miss < (ui_get_screen() == UI_SCREEN_INK ? 4 : 3)) {
            down = true;       /* the FT6336 sometimes reports "no touch" for one scan mid-press: don't release yet */
        } else {
            was_down = false; miss = 0;
        }
        ui_touch(down, lx, ly);
        vTaskDelay(pdMS_TO_TICKS(ui_get_screen() == UI_SCREEN_INK ? 8 : 30));   /* faster while writing: smoother strokes */
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

    if (pmic_init(board_i2c_bus()) != ESP_OK) {
        ESP_LOGE(TAG, "AXP2101 init failed; continuing without PMIC setup");
    }

    {   /* saved colour calibration: "gamma r g b" (percent) */
        char cal[40];
        int c[4] = { 100, 100, 100, 100 };
        if (settings_get_str("cal", cal, sizeof cal)) sscanf(cal, "%d %d %d %d", &c[0], &c[1], &c[2], &c[3]);
        board_lcd_set_calibration(c[0], c[1], c[2], c[3]);
    }
    {   /* saved touch active area: "xmin xmax ymin ymax" (raw) */
        char tc[40];
        int r[4];
        if (settings_get_str("touchcal", tc, sizeof tc) && sscanf(tc, "%d %d %d %d", &r[0], &r[1], &r[2], &r[3]) == 4)
            board_touch_set_range(r[0], r[1], r[2], r[3]);
    }
    ui_start();
    music_init();
    light_init();
    bridge_start();
    camui_init();

    if (audio_init(board_i2c_bus(), on_audio) != ESP_OK) {
        ESP_LOGE(TAG, "audio init failed; recording/playback disabled");
    }

    char buf[8];
    if (settings_get_str("chime", buf, sizeof buf)) s_chime = strcmp(buf, "on") == 0;
    imu_init(board_i2c_bus(), on_imu);   /* optional: logs and continues if not found */

    xTaskCreate(touch_task, "touch", 4096, NULL, 4, NULL);

    wifi_mgr_start(on_wifi_state);
    console_cmd_start();
}
