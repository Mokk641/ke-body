/* ke-body phase 1: a face on the Waveshare ESP32-S3-Touch-LCD-3.5-C.
 *
 *  - boot: light background, big kaomoji "(—_—)"
 *  - Wi-Fi credentials from NVS (serial: wifi <ssid> <password>), IP in the corner
 *  - HTTP: GET /ping, POST /face, POST /say
 *  - touch anywhere: "(—//—)" for 2 s
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "board.h"
#include "ui.h"
#include "wifi_mgr.h"
#include "http_api.h"
#include "console_cmd.h"

static const char *TAG = "main";

#define WAITING_TEXT "等待配网\n串口输入: wifi <ssid> <password>"

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
        break;
    case WIFI_MGR_DISCONNECTED:
        ui_set_corner("已断开, 重连中");
        break;
    }
}

static void touch_task(void *arg)
{
    bool was_down = false;
    for (;;) {
        uint16_t x, y;
        bool down = board_touch_read(&x, &y);
        if (down && !was_down) {
            ESP_LOGI(TAG, "touch at %u,%u", x, y);
            ui_blush();
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
    ui_start();                 /* first frame: default face */
    board_backlight_set(80);

    xTaskCreate(touch_task, "touch", 3072, NULL, 4, NULL);

    wifi_mgr_start(on_wifi_state);
    console_cmd_start();
}
