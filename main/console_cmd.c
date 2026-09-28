#include "console_cmd.h"
#include "wifi_mgr.h"
#include "ui.h"
#include "audio.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_log.h"

static int cmd_wifi(int argc, char **argv)
{
    if (argc == 1) {
        char ssid[33] = {0};
        if (wifi_mgr_get_ssid(ssid, sizeof ssid)) {
            printf("ssid: %s\nstate: %d\nip: %s\n", ssid, (int)wifi_mgr_state(), wifi_mgr_ip());
        } else {
            printf("no credentials stored. usage: wifi <ssid> <password>\n");
        }
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        esp_err_t err = wifi_mgr_clear_creds();
        printf(err == ESP_OK ? "cleared, rebooting\n" : "error: %s\n", esp_err_to_name(err));
        if (err == ESP_OK) { vTaskDelay(pdMS_TO_TICKS(200)); esp_restart(); }
        return err == ESP_OK ? 0 : 1;
    }
    if (argc < 2 || argc > 3) {
        printf("usage: wifi <ssid> <password>   (quote values that contain spaces)\n");
        return 1;
    }
    const char *pass = argc == 3 ? argv[2] : "";
    esp_err_t err = wifi_mgr_save_creds(argv[1], pass);
    if (err != ESP_OK) {
        printf("error saving: %s (ssid max 32 bytes, password max 63 bytes)\n", esp_err_to_name(err));
        return 1;
    }
    printf("saved ssid \"%s\", rebooting...\n", argv[1]);
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return 0;
}

static int cmd_server(int argc, char **argv)
{
    char url[160];
    if (argc == 1) {
        if (settings_get_str(SETTINGS_KEY_SERVER_URL, url, sizeof url)) printf("server: %s\n", url);
        else printf("no server url stored. usage: server http://<pc-ip>:8770/hear\n");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        esp_err_t err = settings_erase(SETTINGS_KEY_SERVER_URL);
        printf(err == ESP_OK ? "cleared\n" : "error: %s\n", esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    if (argc != 2 || strncmp(argv[1], "http://", 7) != 0 || strlen(argv[1]) >= sizeof url) {
        printf("usage: server http://<pc-ip>:8770/hear   (http only, max %u chars)\n", (unsigned)sizeof url - 1);
        return 1;
    }
    esp_err_t err = settings_set_str(SETTINGS_KEY_SERVER_URL, argv[1]);
    if (err != ESP_OK) {
        printf("error saving: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("saved server url: %s (takes effect immediately)\n", argv[1]);
    return 0;
}

static int cmd_volume(int argc, char **argv)
{
    if (argc == 1) {
        printf("volume: %d\n", audio_get_volume());
        return 0;
    }
    int v = atoi(argv[1]);
    if (v < 0 || v > 100) { printf("usage: volume <0-100>\n"); return 1; }
    esp_err_t err = audio_set_volume(v);
    if (err != ESP_OK) {
        printf("error: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("volume: %d\n", v);
    return 0;
}

static void join_args(int argc, char **argv, char *out, size_t out_len)
{
    out[0] = 0;
    for (int i = 1; i < argc; i++) {
        if (i > 1) strlcat(out, " ", out_len);
        strlcat(out, argv[i], out_len);
    }
}

static int cmd_face(int argc, char **argv)
{
    char buf[128];
    join_args(argc, argv, buf, sizeof buf);
    ui_set_face(buf);
    return 0;
}

static int cmd_say(int argc, char **argv)
{
    char buf[300];
    join_args(argc, argv, buf, sizeof buf);
    ui_set_say(buf);
    return 0;
}

esp_err_t console_cmd_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    cfg.prompt = "ke-body>";
    cfg.max_cmdline_length = 256;

    esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&hw, &cfg, &repl);
    if (err != ESP_OK) return err;

    esp_console_register_help_command();
    const esp_console_cmd_t cmds[] = {
        { .command = "wifi",   .help = "wifi <ssid> <password> | wifi | wifi clear", .func = cmd_wifi },
        { .command = "server", .help = "server http://<pc-ip>:8770/hear | server | server clear", .func = cmd_server },
        { .command = "volume", .help = "volume <0-100> | volume", .func = cmd_volume },
        { .command = "face",   .help = "face <kaomoji>  (local test)", .func = cmd_face },
        { .command = "say",    .help = "say <text>      (local test)", .func = cmd_say },
    };
    for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    return esp_console_start_repl(repl);
}
