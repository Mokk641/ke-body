#include "console_cmd.h"
#include "wifi_mgr.h"
#include "ui.h"
#include "audio.h"
#include "settings.h"
#include "light.h"
#include "pmic.h"
#include "board.h"
#include "bridge.h"
#include "imu.h"
#include "cam_ui.h"
#include "camera.h"
#include "storage.h"
#include "music.h"
#include "lineedit.h"
#include "gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
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
    if (ui_set_theme(argv[1]) != ESP_OK) { printf("usage: theme light|dark|auto\n"); return 1; }
    printf("theme: %s (saved)\n", ui_get_theme());
    return 0;
}

void app_send_text(const char *text);

static int cmd_msg(int argc, char **argv)
{
    char buf[200];
    join_args(argc, argv, buf, sizeof buf);
    if (!buf[0]) { printf("usage: msg <text>   (same as pressing a quick button)\n"); return 1; }
    app_send_text(buf);
    return 0;
}

/* colour calibration: color [gamma% r% g% b%] | color reset ; colortest */
static int cmd_color(int argc, char **argv)
{
    int c[4];
    if (argc == 1) {
        board_lcd_get_calibration(c);
        printf("color: gamma %d%%, r %d%%, g %d%%, b %d%%  (100 100 100 100 = off)\n", c[0], c[1], c[2], c[3]);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "reset") == 0) {
        board_lcd_set_calibration(100, 100, 100, 100);
        settings_set_str("cal", "100 100 100 100");
        printf("color calibration off\n");
        return 0;
    }
    if (argc != 5) { printf("usage: color <gamma%%> <r%%> <g%%> <b%%> | color reset   e.g. color 110 100 96 88\n"); return 1; }
    for (int i = 0; i < 4; i++) c[i] = atoi(argv[1 + i]);
    board_lcd_set_calibration(c[0], c[1], c[2], c[3]);
    board_lcd_get_calibration(c);
    char buf[40];
    snprintf(buf, sizeof buf, "%d %d %d %d", c[0], c[1], c[2], c[3]);
    settings_set_str("cal", buf);
    printf("color: %s (saved). gamma>100 darkens mid-tones, r/g/b are channel gains in %%\n", buf);
    return 0;
}

static int cmd_colortest(int argc, char **argv)
{
    ui_set_screen(UI_SCREEN_COLORTEST);
    printf("colour test pattern on screen; tap to leave\n");
    return 0;
}

static int cmd_mic(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "gain"))) { printf("mic gain: %d dB (analog up to 42 dB, the rest digital; the recording is then normalised to peak 0.8)\n", audio_get_mic_db()); return 0; }
    if (argc == 3 && !strcmp(argv[1], "gain")) {
        if (audio_set_mic_db(atoi(argv[2])) != ESP_OK) { printf("mic gain: 0-60 dB\n"); return 1; }
        printf("mic gain: %d dB (saved)\n", audio_get_mic_db());
        return 0;
    }
    printf("usage: mic gain <0-60 dB>\n");
    return 1;
}

static int cmd_music(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "list"))) {
        printf("%d song(s), %s\n", music_count(), music_is_playing() ? "playing" : (music_is_paused() ? "paused" : "stopped"));
        for (int i = 0; i < music_count(); i++) printf("  %c %2d  %s\n", i == music_current() ? '>' : ' ', i, music_title(i));
        return 0;
    }
    if (!strcmp(argv[1], "rescan")) { storage_rescan(); music_rescan(); printf("%d song(s)\n", music_count()); return 0; }
    if (!strcmp(argv[1], "play") && argc == 3) { printf(music_play(atoi(argv[2])) ? "ok\n" : "no such song\n"); return 0; }
    if (!strcmp(argv[1], "pause") || !strcmp(argv[1], "toggle")) { music_toggle(); return 0; }
    if (!strcmp(argv[1], "next")) { music_next(); return 0; }
    if (!strcmp(argv[1], "prev")) { music_prev(); return 0; }
    if (!strcmp(argv[1], "stop")) { music_stop(); return 0; }
    printf("usage: music [list] | play <n> | pause | next | prev | stop | rescan\n");
    return 1;
}

static int cmd_buttons(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "reset") == 0) {
        ui_reset_buttons();
        printf("buttons reset to defaults\n");
    }
    printf("%s\n", ui_get_buttons_json());
    return 0;
}

static int cmd_anim(int argc, char **argv)
{
    char st[128];
    if (argc == 1) { ui_anim_status(st, sizeof st); printf("anim: %s\n", st); return 0; }
    const char *name = argc >= 3 ? argv[1] : "all";
    const char *val = argc >= 3 ? argv[2] : argv[1];
    if ((strcmp(val, "on") && strcmp(val, "off")) || !ui_anim_set(name, strcmp(val, "on") == 0)) {
        printf("usage: anim [blink|blush|zzz|shake|flash] on|off\n");
        return 1;
    }
    ui_anim_status(st, sizeof st);
    printf("anim: %s (saved)\n", st);
    return 0;
}

bool app_chime_enabled(void);
void app_set_chime(bool on);

static int cmd_chime(int argc, char **argv)
{
    if (argc == 1) { printf("chime: %s\n", app_chime_enabled() ? "on" : "off"); return 0; }
    if (strcmp(argv[1], "on") && strcmp(argv[1], "off")) { printf("usage: chime on|off\n"); return 1; }
    app_set_chime(strcmp(argv[1], "on") == 0);
    printf("chime: %s (saved)\n", argv[1]);
    return 0;
}

static int cmd_imu(int argc, char **argv)
{
    if (argc == 1) { imu_print(); return 0; }
    if (argc == 3 && strcmp(argv[1], "invert") == 0) {
        imu_set_invert(strcmp(argv[2], "on") == 0);
        printf("imu invert: %s (saved)\n", imu_get_invert() ? "on" : "off");
        return 0;
    }
    printf("usage: imu | imu invert on|off\n");
    return 1;
}

static int cmd_autorotate(int argc, char **argv)
{
    if (argc == 1) { printf("autorotate: %s\n", imu_get_autorotate() ? "on" : "off"); return 0; }
    if (strcmp(argv[1], "on") && strcmp(argv[1], "off")) { printf("usage: autorotate on|off\n"); return 1; }
    imu_set_autorotate(strcmp(argv[1], "on") == 0);
    printf("autorotate: %s (saved)\n", argv[1]);
    return 0;
}

static int cmd_cam(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "on") == 0) { camui_enter(); return 0; }
    if (argc == 2 && strcmp(argv[1], "off") == 0) { camui_leave(); return 0; }
    if (argc == 2 && strcmp(argv[1], "shot") == 0) { camui_shoot(); return 0; }
    if (argc == 2 && strcmp(argv[1], "gallery") == 0) { camui_gallery_enter(); return 0; }
    if (argc == 3 && strcmp(argv[1], "vflip") == 0) { camera_set_vflip(strcmp(argv[2], "on") == 0); printf("vflip %s (saved)\n", argv[2]); return 0; }
    if (argc == 3 && strcmp(argv[1], "mirror") == 0) { camera_set_hmirror(strcmp(argv[2], "on") == 0); printf("mirror %s (saved)\n", argv[2]); return 0; }
    if (argc == 3 && strcmp(argv[1], "xclk") == 0) { camera_set_xclk(atoi(argv[2])); camera_print_settings(); return 0; }
    if (argc == 3 && strcmp(argv[1], "quality") == 0) { camera_set_quality(atoi(argv[2])); camera_print_settings(); return 0; }
    if (argc == 3 && strcmp(argv[1], "awb") == 0) { camera_set_awb(strcmp(argv[2], "on") == 0); camera_print_settings(); return 0; }
    if (argc == 3 && strcmp(argv[1], "wb") == 0) {
        if (!camera_set_wb(argv[2])) { printf("wb: auto sunny cloudy office home\n"); return 1; }
        camera_print_settings();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "rot") == 0) {
        if (!camera_set_rot(atoi(argv[2]))) { printf("rot: 0 90 180 270 (extra clockwise turn on top of the display rotation)\n"); return 1; }
        camera_print_settings();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "status") == 0) { camera_print_settings(); return 0; }
    if (argc == 2 && strcmp(argv[1], "sweep") == 0) { camui_sweep(); return 0; }
    printf("usage: cam on|off|shot|gallery|status\n"
           "       cam sweep             (photos + NO-SOI table for xclk 6/8/10 x quality 10/20)\n"
           "       cam xclk <6-24 MHz>   (default 10; lower = fewer stripes, slower)\n"
           "       cam quality <4-63>    (JPEG, lower = better, default 10)\n"
           "       cam awb on|off | cam wb auto|sunny|cloudy|office|home\n"
           "       cam rot 0|90|180|270  (extra clockwise turn if pictures are still sideways)\n"
           "       cam vflip on|off | cam mirror on|off\n");
    return 1;
}

static int cmd_peek(int argc, char **argv)
{
    if (argc == 1) { printf("peek: %s\n", camui_peek() ? "on" : "off"); return 0; }
    if (strcmp(argv[1], "on") && strcmp(argv[1], "off")) { printf("usage: peek on|off\n"); return 1; }
    camui_set_peek(strcmp(argv[1], "on") == 0);
    printf("peek: %s (saved)\n", argv[1]);
    return 0;
}

static int cmd_photos(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "rescan") == 0) { storage_rescan(); }
    camui_print_photos();
    return 0;
}

static int cmd_touchrange(int argc, char **argv)
{
    int seen[4];
    board_touch_seen(seen, argc == 2 && strcmp(argv[1], "reset") == 0);
    if (argc == 2 && strcmp(argv[1], "reset") == 0) { printf("touchrange: reset, now touch all four edges and corners of the screen\n"); return 0; }
    printf("raw touch values seen: x %d..%d (panel 0..319), y %d..%d (panel 0..479), from %d samples (one every 30 ms)\n",
           seen[0], seen[1], seen[2], seen[3], board_touch_samples());
    printf("(a quick swipe gives only a few samples and never records the extreme edge: to measure an edge, hold a finger still on it for a second)\n");
    printf("if a range is clearly smaller than the screen, the panel's edge is dead: run  touchcal %d %d %d %d\n",
           seen[0], seen[1], seen[2], seen[3]);
    return 0;
}

static int cmd_touchcal(int argc, char **argv)
{
    int r[4];
    if (argc == 2 && strcmp(argv[1], "reset") == 0) {
        board_touch_set_range(0, 319, 0, 479);
        settings_set_str("touchcal", "0 319 0 479");
        printf("touchcal: identity (saved)\n");
        return 0;
    }
    if (argc == 5) {
        for (int i = 0; i < 4; i++) r[i] = atoi(argv[1 + i]);
        board_touch_set_range(r[0], r[1], r[2], r[3]);
        board_touch_get_range(r);
        char buf[40];
        snprintf(buf, sizeof buf, "%d %d %d %d", r[0], r[1], r[2], r[3]);
        settings_set_str("touchcal", buf);
        printf("touchcal: raw x %d..%d, y %d..%d are stretched to the whole screen (saved)\n", r[0], r[1], r[2], r[3]);
        return 0;
    }
    board_touch_get_range(r);
    printf("touchcal: raw x %d..%d, y %d..%d   (usage: touchcal <xmin> <xmax> <ymin> <ymax> | touchcal reset)\n", r[0], r[1], r[2], r[3]);
    return 0;
}

static int cmd_touchlog(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "on") && strcmp(argv[1], "off"))) {
        printf("usage: touchlog on|off\n");
        return 1;
    }
    board_touch_log(strcmp(argv[1], "on") == 0);
    ui_set_touchlog(strcmp(argv[1], "on") == 0);         /* + a yellow ring on the screen where the touch is */
    printf("touchlog %s (serial: raw and mapped coordinates; screen: yellow ring + cross at the touch)\n", argv[1]);
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

/* ---- console transport: our own UTF-8 aware line editor instead of the IDF REPL ----
 * The IDF REPL (linenoise) deletes every byte >= 0x80 from the line, which makes it
 * impossible to type Chinese. Set-up below mirrors esp_console_new_repl_usb_serial_jtag(). */

static int console_read_byte(void)
{
    unsigned char b;
    for (;;) {
        if (read(STDIN_FILENO, &b, 1) == 1) return b;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void console_write(const char *s, size_t n)
{
    fwrite(s, 1, n, stdout);
    fflush(stdout);
}

static bool valid_utf8(const char *s)
{
    while (*s) {
        const char *before = s;
        uint32_t cp = gfx_utf8_next(&s);
        if (cp == 0xFFFD && (unsigned char)before[0] != 0xEF) return false;   /* 0xEF.. = a real U+FFFD */
    }
    return true;
}

static void console_task(void *arg)
{
    static lineedit_t le;
    le.read = console_read_byte;
    le.write = console_write;
    le.prompt = "ke-body> ";
    setvbuf(stdin, NULL, _IONBF, 0);
    printf("\r\nType 'help' for the command list. Up/Down = history. Set your terminal to UTF-8 to type Chinese.\r\n");

    char line[LINEEDIT_MAX];
    for (;;) {
        int n = lineedit_read(&le, line, sizeof line);
        if (n <= 0) continue;
        if (!valid_utf8(line)) {
            printf("warning: input is not valid UTF-8 - set the terminal encoding to UTF-8 "
                   "(PuTTY: Window > Translation; Tera Term: Setup > Terminal > Kanji)\n");
        }
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            /* empty command */
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
    }
}

esp_err_t console_cmd_start(void)
{
    /* Terminals send CR for Enter; move the caret to the start of the next line on LF */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    fcntl(fileno(stdout), F_SETFL, 0);
    fcntl(fileno(stdin), F_SETFL, 0);

    usb_serial_jtag_driver_config_t ucfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&ucfg);
    if (err != ESP_OK) return err;

    esp_console_config_t ccfg = ESP_CONSOLE_CONFIG_DEFAULT();
    ccfg.max_cmdline_length = LINEEDIT_MAX;
    err = esp_console_init(&ccfg);
    if (err != ESP_OK) return err;

    usb_serial_jtag_vfs_use_driver();

    esp_console_register_help_command();
    const esp_console_cmd_t cmds[] = {
        { .command = "wifi",     .help = "wifi <ssid> <password> | wifi | wifi clear", .func = cmd_wifi },
        { .command = "server",   .help = "server http://<pc-ip>:8770/hear | server | server clear", .func = cmd_server },
        { .command = "volume",   .help = "volume <0-100> | volume", .func = cmd_volume },
        { .command = "rotate",   .help = "rotate 0|90|180|270 (saved)", .func = cmd_rotate },
        { .command = "bright",   .help = "bright <5-100> (saved)", .func = cmd_bright },
        { .command = "night",    .help = "night HH:MM HH:MM <level> | night off | night (status + time)", .func = cmd_night },
        { .command = "theme",    .help = "theme light|dark|auto (auto = dark during the night schedule; saved)", .func = cmd_theme },
        { .command = "touchlog", .help = "touchlog on|off  print touch coordinates", .func = cmd_touchlog },
        { .command = "pmic",     .help = "pmic | pmic init | pmic <rail> on|off   (AXP2101 rails)", .func = cmd_pmic },
        { .command = "tca",      .help = "tca | tca <pin> 0|1|in   (TCA9554 expander pins)", .func = cmd_tca },
        { .command = "audio",    .help = "audio test [ms] [rate] | audio mic [ms] | audio slot mono|stereo | audio regs | audio gain <0-7>", .func = cmd_audio },
        { .command = "gpio",     .help = "gpio <n> 0|1|in   (drive a free ESP32 pin, amplifier-enable hunting)", .func = cmd_gpio },
        { .command = "msg",      .help = "msg <text>   send like a quick button (chat + bridge /msg)", .func = cmd_msg },
        { .command = "color",    .help = "color [gamma% r% g% b% | reset]  screen colour calibration (saved)", .func = cmd_color },
        { .command = "colortest", .help = "show swatches with their #RRGGBB to compare with a phone", .func = cmd_colortest },
        { .command = "touchrange", .help = "touchrange [reset]  smallest / largest raw touch values seen (dead panel edges?)", .func = cmd_touchrange },
        { .command = "touchcal", .help = "touchcal [xmin xmax ymin ymax | reset]  stretch the panel's real touch area to the screen (saved)", .func = cmd_touchcal },
        { .command = "mic",      .help = "mic gain <0-60 dB>  microphone gain for recordings (saved)", .func = cmd_mic },
        { .command = "music",    .help = "music [list|play <n>|pause|next|prev|stop|rescan]  the MP3 player (files in /sdcard/MUSIC)", .func = cmd_music },
        { .command = "buttons",  .help = "buttons [reset]  show the quick-button config JSON / restore defaults", .func = cmd_buttons },
        { .command = "anim",     .help = "anim [blink|blush|zzz|shake|flash] on|off  (no name = all; saved)", .func = cmd_anim },
        { .command = "chime",    .help = "chime on|off  soft tone on new message (saved)", .func = cmd_chime },
        { .command = "imu",      .help = "imu | imu invert on|off   (accelerometer)", .func = cmd_imu },
        { .command = "autorotate", .help = "autorotate on|off (saved, default off)", .func = cmd_autorotate },
        { .command = "cam",      .help = "cam on|off|shot|gallery|status|xclk|quality|awb|wb|rot|vflip|mirror (see `cam` for details)", .func = cmd_cam },
        { .command = "peek",     .help = "peek on|off  allow remote /snap (saved, default off)", .func = cmd_peek },
        { .command = "photos",   .help = "photos [rescan]  list photos (SD card or flash); rescan = look for a newly inserted SD card", .func = cmd_photos },
        { .command = "face",     .help = "face <kaomoji>  (local test)", .func = cmd_face },
        { .command = "say",      .help = "say <text>      (local test)", .func = cmd_say },
    };
    for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    /* stack: camera / SD commands run on this task */
    if (xTaskCreate(console_task, "console", 8192, NULL, 2, NULL) != pdPASS) return ESP_FAIL;
    return ESP_OK;
}
