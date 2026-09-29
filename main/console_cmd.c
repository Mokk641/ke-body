#include "console_cmd.h"
#include "wifi_mgr.h"
#include "ui.h"
#include "audio.h"
#include "settings.h"
#include "light.h"
#include "pmic.h"
#include "board.h"

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

/* ---- phase 3 ---- */

static int cmd_rotate(int argc, char **argv)
{
    if (argc == 1) { printf("rotate: %d\n", ui_get_rotation()); return 0; }
    int r = atoi(argv[1]);
    esp_err_t err = ui_set_rotation(r);
    if (err != ESP_OK) { printf("usage: rotate 0|90|180|270\n"); return 1; }
    printf("rotate: %d (saved)\n", r);
    return 0;
}

static int cmd_bright(int argc, char **argv)
{
    if (argc == 1) { printf("bright: %d\n", light_get_bright()); return 0; }
    int v = atoi(argv[1]);
    if (v < 0 || v > 100) { printf("usage: bright <%d-100>\n", LIGHT_MIN_BRIGHT); return 1; }
    light_set_bright(v);
    printf("bright: %d (saved)\n", light_get_bright());
    return 0;
}

static int cmd_night(int argc, char **argv)
{
    if (argc == 1) { light_print_status(); return 0; }
    char spec[32];
    join_args(argc, argv, spec, sizeof spec);
    esp_err_t err = light_set_night(spec);
    if (err != ESP_OK) {
        printf("usage: night HH:MM HH:MM <level %d-100>   |   night off\n", LIGHT_MIN_BRIGHT);
        return 1;
    }
    printf("night: %s (saved)\n", light_get_night());
    return 0;
}

static int cmd_theme(int argc, char **argv)
{
    if (argc == 1) { printf("theme: %s\n", ui_get_theme()); return 0; }
    if (ui_set_theme(argv[1]) != ESP_OK) { printf("usage: theme dark|light\n"); return 1; }
    printf("theme: %s (saved)\n", ui_get_theme());
    return 0;
}

static int cmd_touchlog(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "on") && strcmp(argv[1], "off"))) {
        printf("usage: touchlog on|off\n");
        return 1;
    }
    board_touch_log(strcmp(argv[1], "on") == 0);
    printf("touchlog %s\n", argv[1]);
    return 0;
}

static int cmd_pmic(int argc, char **argv)
{
    if (argc == 1) { pmic_dump(); return 0; }
    if (argc == 2 && strcmp(argv[1], "init") == 0) {
        esp_err_t err = pmic_init(board_i2c_bus());
        printf("pmic init: %s\n", esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    if (argc == 3 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        esp_err_t err = pmic_set_rail(argv[1], strcmp(argv[2], "on") == 0);
        if (err == ESP_ERR_NOT_FOUND) printf("unknown rail. rails: dc1..dc5 aldo1..aldo4 bldo1 bldo2 dldo1 dldo2 cpusldo\n");
        else printf("%s %s: %s\n", argv[1], argv[2], esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    printf("usage: pmic | pmic init | pmic <rail> on|off\n");
    return 1;
}

static int cmd_tca(int argc, char **argv)
{
    if (argc == 1) { return board_expander_dump() == ESP_OK ? 0 : 1; }
    if (argc == 3) {
        int pin = atoi(argv[1]);
        int mode = strcmp(argv[2], "in") == 0 ? 2 : strcmp(argv[2], "1") == 0 ? 1 : strcmp(argv[2], "0") == 0 ? 0 : -1;
        if (mode >= 0) {
            esp_err_t err = board_expander_set(pin, mode);
            printf("tca pin %d: %s\n", pin, esp_err_to_name(err));
            return err == ESP_OK ? 0 : 1;
        }
    }
    printf("usage: tca | tca <pin 0-7> 0|1|in     (pin 1 = LCD reset, leave it high)\n");
    return 1;
}

static int cmd_gpio(int argc, char **argv)
{
    if (argc == 3) {
        int pin = atoi(argv[1]);
        int mode = strcmp(argv[2], "in") == 0 ? 2 : strcmp(argv[2], "1") == 0 ? 1 : strcmp(argv[2], "0") == 0 ? 0 : -1;
        if (mode >= 0) {
            esp_err_t err = board_gpio_set(pin, mode);
            if (err != ESP_OK) printf("gpio %d: %s (reserved pin or bad number)\n", pin, esp_err_to_name(err));
            return err == ESP_OK ? 0 : 1;
        }
    }
    printf("usage: gpio <n> 0|1|in    (free pins on this board: 2 4 43 44; camera pins 17 18 21 38-42 45-48 are idle too)\n");
    return 1;
}

static int cmd_audio(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "test") == 0) {
        int ms = argc >= 3 ? atoi(argv[2]) : 1000;
        int rate = argc >= 4 ? atoi(argv[3]) : 0;
        esp_err_t err = audio_test_tone(ms, rate);
        printf("test tone %d ms: %s  (watch the log for 'play done' and 'loopback' lines)\n", ms, esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "mic") == 0) {
        int ms = argc >= 3 ? atoi(argv[2]) : 2000;
        esp_err_t err = audio_mic_test(ms);
        printf("mic test %d ms: %s  (speak now; result in the log)\n", ms, esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[1], "slot") == 0) {
        bool mono = strcmp(argv[2], "mono") == 0;
        if (!mono && strcmp(argv[2], "stereo") != 0) { printf("usage: audio slot mono|stereo\n"); return 1; }
        esp_err_t err = audio_set_slot_mode(mono);
        printf("slot mode %s: %s\n", argv[2], esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    if (argc == 2 && strcmp(argv[1], "regs") == 0) { audio_dump_regs(); return 0; }
    if (argc == 3 && strcmp(argv[1], "gain") == 0) {
        int g = atoi(argv[2]);
        esp_err_t err = audio_set_mic_gain(g);
        printf("mic gain step %d (0=0dB .. 7=42dB, 6 dB/step): %s\n", g, esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    printf("usage: audio test [ms] [rate] | audio mic [ms] | audio slot mono|stereo | audio regs | audio gain <0-7>\n");
    return 1;
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
        { .command = "wifi",     .help = "wifi <ssid> <password> | wifi | wifi clear", .func = cmd_wifi },
        { .command = "server",   .help = "server http://<pc-ip>:8770/hear | server | server clear", .func = cmd_server },
        { .command = "volume",   .help = "volume <0-100> | volume", .func = cmd_volume },
        { .command = "rotate",   .help = "rotate 0|90|180|270 (saved)", .func = cmd_rotate },
        { .command = "bright",   .help = "bright <5-100> (saved)", .func = cmd_bright },
        { .command = "night",    .help = "night HH:MM HH:MM <level> | night off | night (status + time)", .func = cmd_night },
        { .command = "theme",    .help = "theme dark|light (saved)", .func = cmd_theme },
        { .command = "touchlog", .help = "touchlog on|off  print touch coordinates", .func = cmd_touchlog },
        { .command = "pmic",     .help = "pmic | pmic init | pmic <rail> on|off   (AXP2101 rails)", .func = cmd_pmic },
        { .command = "tca",      .help = "tca | tca <pin> 0|1|in   (TCA9554 expander pins)", .func = cmd_tca },
        { .command = "audio",    .help = "audio test [ms] [rate] | audio mic [ms] | audio slot mono|stereo | audio regs | audio gain <0-7>", .func = cmd_audio },
        { .command = "gpio",     .help = "gpio <n> 0|1|in   (drive a free ESP32 pin, amplifier-enable hunting)", .func = cmd_gpio },
        { .command = "face",     .help = "face <kaomoji>  (local test)", .func = cmd_face },
        { .command = "say",      .help = "say <text>      (local test)", .func = cmd_say },
    };
    for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    return esp_console_start_repl(repl);
}
