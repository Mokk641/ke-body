#include "light.h"
#include "settings.h"
#include "board.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_timer.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_log.h"

static const char *TAG = "light";

#define KEY_BRIGHT "bright"
#define KEY_NIGHT  "night"
#define CHECK_MS   20000

static int s_bright = LIGHT_DEFAULT_BRIGHT;
static char s_night[32] = LIGHT_DEFAULT_NIGHT;
static bool s_night_on;
static int s_start_min, s_end_min, s_night_level;
static bool s_night_active;
static bool s_sntp_started;
static esp_timer_handle_t s_timer;

static bool parse_night(const char *spec, int *start, int *end, int *level)
{
    int sh, sm, eh, em, lv;
    if (strcmp(spec, "off") == 0) return false;
    if (sscanf(spec, "%d:%d %d:%d %d", &sh, &sm, &eh, &em, &lv) != 5) return false;
    if (sh < 0 || sh > 23 || eh < 0 || eh > 23 || sm < 0 || sm > 59 || em < 0 || em > 59) return false;
    if (lv < LIGHT_MIN_BRIGHT || lv > 100) return false;
    *start = sh * 60 + sm;
    *end = eh * 60 + em;
    *level = lv;
    return true;
}

bool light_time_valid(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_year + 1900 >= 2024;
}

static bool in_window(int now_min)
{
    if (s_start_min == s_end_min) return false;
    if (s_start_min < s_end_min) return now_min >= s_start_min && now_min < s_end_min;
    return now_min >= s_start_min || now_min < s_end_min;   /* crosses midnight */
}

static void apply(void)
{
    bool night = false;
    if (s_night_on && light_time_valid()) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        night = in_window(t.tm_hour * 60 + t.tm_min);
    }
    int level = night ? (s_night_level < s_bright ? s_night_level : s_bright) : s_bright;
    if (night != s_night_active) {
        ESP_LOGI(TAG, "night dimming %s -> backlight %d", night ? "on" : "off", level);
    }
    s_night_active = night;
    board_backlight_set((uint8_t)level);
}

static void timer_cb(void *arg) { apply(); }

void light_init(void)
{
    setenv("TZ", "CST-8", 1);   /* 东八区 */
    tzset();

    char buf[32];
    if (settings_get_str(KEY_BRIGHT, buf, sizeof buf)) {
        int v = atoi(buf);
        if (v >= LIGHT_MIN_BRIGHT && v <= 100) s_bright = v;
    }
    if (settings_get_str(KEY_NIGHT, buf, sizeof buf)) {
        strlcpy(s_night, buf, sizeof s_night);
    }
    s_night_on = parse_night(s_night, &s_start_min, &s_end_min, &s_night_level);
    if (!s_night_on && strcmp(s_night, "off") != 0) {
        ESP_LOGW(TAG, "bad night spec \"%s\", using default", s_night);
        strlcpy(s_night, LIGHT_DEFAULT_NIGHT, sizeof s_night);
        s_night_on = parse_night(s_night, &s_start_min, &s_end_min, &s_night_level);
    }
    apply();
    const esp_timer_create_args_t a = { .callback = timer_cb, .name = "light" };
    ESP_ERROR_CHECK(esp_timer_create(&a, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, (uint64_t)CHECK_MS * 1000));
    ESP_LOGI(TAG, "bright %d, night \"%s\"", s_bright, s_night);
}

esp_err_t light_set_bright(int percent)
{
    if (percent < LIGHT_MIN_BRIGHT) percent = LIGHT_MIN_BRIGHT;
    if (percent > 100) percent = 100;
    s_bright = percent;
    char buf[8];
    snprintf(buf, sizeof buf, "%d", percent);
    esp_err_t err = settings_set_str(KEY_BRIGHT, buf);
    apply();
    return err;
}

int light_get_bright(void) { return s_bright; }

esp_err_t light_set_night(const char *spec)
{
    int a, b, c;
    bool on = parse_night(spec, &a, &b, &c);
    if (!on && strcmp(spec, "off") != 0) return ESP_ERR_INVALID_ARG;
    strlcpy(s_night, spec, sizeof s_night);
    s_night_on = on;
    if (on) { s_start_min = a; s_end_min = b; s_night_level = c; }
    esp_err_t err = settings_set_str(KEY_NIGHT, s_night);
    apply();
    return err;
}

const char *light_get_night(void) { return s_night; }
bool light_night_active(void) { return s_night_active; }

static void on_time_sync(struct timeval *tv)
{
    ESP_LOGI(TAG, "time synced");
    apply();
}

void light_start_sntp(void)
{
    if (s_sntp_started) return;
    s_sntp_started = true;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "pool.ntp.org"));
    cfg.sync_cb = on_time_sync;
    esp_err_t err = esp_netif_sntp_init(&cfg);
    ESP_LOGI(TAG, "sntp start: %s", esp_err_to_name(err));
}

void light_print_status(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &t);
    printf("time: %s (%s)\n", ts, light_time_valid() ? "synced, CST-8" : "NOT synced");
    printf("bright: %d\nnight: %s%s\n", s_bright, s_night,
           s_night_on ? (s_night_active ? "  [active now]" : "  [not active now]") : "");
}
