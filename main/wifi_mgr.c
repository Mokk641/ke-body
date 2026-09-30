#include "wifi_mgr.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "wifi";

#define NVS_NS       "kebody"
#define NVS_KEY_SSID "wifi_ssid"
#define NVS_KEY_PASS "wifi_pass"
#define RETRY_MS     3000

static wifi_mgr_cb_t s_cb;
static wifi_mgr_state_t s_state = WIFI_MGR_NO_CREDS;
static char s_ip[16];
static esp_timer_handle_t s_retry_timer;

static void set_state(wifi_mgr_state_t st)
{
    s_state = st;
    if (s_cb) s_cb(st, s_ip);
}

static void retry_cb(void *arg)
{
    ESP_LOGI(TAG, "reconnecting");
    esp_wifi_connect();
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e = data;
        ESP_LOGW(TAG, "disconnected, reason %d", e ? e->reason : -1);
        s_ip[0] = 0;
        set_state(s_state == WIFI_MGR_CONNECTING ? WIFI_MGR_CONNECTING : WIFI_MGR_DISCONNECTED);
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, (uint64_t)RETRY_MS * 1000);
    }
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "got ip %s", s_ip);
        set_state(WIFI_MGR_CONNECTED);
    }
}

bool wifi_mgr_get_ssid(char *out, unsigned out_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = out_len;
    esp_err_t err = nvs_get_str(h, NVS_KEY_SSID, out, &len);
    nvs_close(h);
    return err == ESP_OK && out[0];
}

static bool load_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t l1 = ssid_len, l2 = pass_len;
    esp_err_t e1 = nvs_get_str(h, NVS_KEY_SSID, ssid, &l1);
    esp_err_t e2 = nvs_get_str(h, NVS_KEY_PASS, pass, &l2);
    nvs_close(h);
    if (e2 != ESP_OK) pass[0] = 0;
    return e1 == ESP_OK && ssid[0];
}

esp_err_t wifi_mgr_save_creds(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32) return ESP_ERR_INVALID_ARG;
    if (pass && strlen(pass) > 63) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_PASS, pass ? pass : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t wifi_mgr_clear_creds(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_erase_key(h, NVS_KEY_SSID);
    nvs_erase_key(h, NVS_KEY_PASS);
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

wifi_mgr_state_t wifi_mgr_state(void) { return s_state; }
const char *wifi_mgr_ip(void) { return s_ip; }

esp_err_t wifi_mgr_start(wifi_mgr_cb_t cb)
{
    s_cb = cb;
    char ssid[33] = {0}, pass[65] = {0};
    if (!load_creds(ssid, sizeof ssid, pass, sizeof pass)) {
        ESP_LOGW(TAG, "no credentials in NVS; use serial command: wifi <ssid> <password>");
        set_state(WIFI_MGR_NO_CREDS);
        return ESP_OK;
    }
    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    const esp_timer_create_args_t targs = { .callback = retry_cb, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_retry_timer));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL));

    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strncpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    set_state(WIFI_MGR_CONNECTING);
    ESP_ERROR_CHECK(esp_wifi_start());
    /* no Wi-Fi power save: the board must answer ARP / HTTP at once and reach a sleepy PC reliably */
    esp_wifi_set_ps(WIFI_PS_NONE);
    return ESP_OK;
}
