#include "bridge.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "ui.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "bridge";

typedef struct { int type; uint8_t *data; size_t len; } job_t;   /* type 0 = text, 1 = photo */
static QueueHandle_t s_q;

bool bridge_url(const char *path, char *out, size_t out_len)
{
    char base[160];
    if (!settings_get_str(SETTINGS_KEY_SERVER_URL, base, sizeof base)) return false;
    /* strip the last path segment (after the last '/', but not the one in "http://") */
    char *scheme = strstr(base, "://");
    char *p = scheme ? scheme + 3 : base;
    char *slash = strrchr(p, '/');
    if (slash) *slash = 0;
    snprintf(out, out_len, "%s/%s", base, path);
    return true;
}

esp_err_t bridge_post(const char *url, const char *content_type, const uint8_t *data, size_t len,
                      int *status, uint8_t **resp, size_t *resp_len, size_t resp_max)
{
    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 15000 };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_ERR_INVALID_ARG;
    esp_http_client_set_header(c, "Content-Type", content_type);
    esp_err_t err = esp_http_client_open(c, (int)len);
    if (err == ESP_OK) {
        size_t sent = 0;
        while (sent < len) {
            int n = esp_http_client_write(c, (const char *)data + sent, (int)(len - sent));
            if (n <= 0) { err = ESP_FAIL; break; }
            sent += n;
        }
    }
    if (err == ESP_OK) {
        int64_t clen = esp_http_client_fetch_headers(c);
        if (clen < 0) {
            err = ESP_FAIL;
        } else {
            *status = esp_http_client_get_status_code(c);
            if (resp) {
                size_t cap = clen > 0 && (size_t)clen < resp_max ? (size_t)clen : resp_max;
                uint8_t *buf = heap_caps_malloc(cap + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!buf) buf = malloc(cap + 1);
                size_t got = 0;
                if (buf) {
                    while (got < cap) {
                        int r = esp_http_client_read(c, (char *)buf + got, (int)(cap - got));
                        if (r <= 0) break;
                        got += r;
                    }
                    buf[got] = 0;
                }
                *resp = buf;
                if (resp_len) *resp_len = got;
            }
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    ESP_LOGI(TAG, "POST %s (%u bytes): %s, status %d", url, (unsigned)len, esp_err_to_name(err),
             err == ESP_OK ? *status : 0);
    return err;
}

static void worker(void *arg)
{
    job_t j;
    for (;;) {
        if (xQueueReceive(s_q, &j, portMAX_DELAY) != pdTRUE) continue;
        char url[200];
        const char *path = j.type == 1 ? "photo" : "msg";
        if (!bridge_url(path, url, sizeof url)) {
            ui_toast("未设置服务器地址\n串口输入: server http://电脑IP:8770/hear", 4000);
        } else if (wifi_mgr_state() != WIFI_MGR_CONNECTED) {
            ui_toast("没有网络，没发出去", 3000);
        } else {
            int status = 0;
            esp_err_t err = bridge_post(url, j.type == 1 ? "image/jpeg" : "text/plain; charset=utf-8",
                                        j.data, j.len, &status, NULL, NULL, 0);
            if (err != ESP_OK) {
                char msg[80];
                snprintf(msg, sizeof msg, "发送失败: %s", esp_err_to_name(err));
                ui_toast(msg, 3000);
            } else if (status / 100 != 2) {
                char msg[64];
                snprintf(msg, sizeof msg, "服务器返回 %d", status);
                ui_toast(msg, 3000);
            } else if (j.type == 1) {
                ui_toast("寄出去了", 1500);
            }
        }
        free(j.data);
    }
}

static void enqueue(int type, const uint8_t *data, size_t len)
{
    job_t j = { .type = type, .len = len };
    j.data = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!j.data) j.data = malloc(len + 1);
    if (!j.data) return;
    memcpy(j.data, data, len);
    j.data[len] = 0;
    if (xQueueSend(s_q, &j, 0) != pdTRUE) {
        free(j.data);
        ui_toast("发送队列满了", 2000);
    }
}

void bridge_send_text(const char *text)
{
    enqueue(0, (const uint8_t *)text, strlen(text));
}

void bridge_send_photo(const uint8_t *jpeg, size_t len)
{
    enqueue(1, jpeg, len);
}

/* Every 15 s: GET /ping on the bridge -> green/grey dot in the chat top bar. */
static void probe_task(void *arg)
{
    bool last = false, first = true;
    for (;;) {
        bool ok = false;
        char url[200];
        if (wifi_mgr_state() == WIFI_MGR_CONNECTED && bridge_url("ping", url, sizeof url)) {
            esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_GET, .timeout_ms = 2500 };
            esp_http_client_handle_t c = esp_http_client_init(&cfg);
            if (c) {
                ok = esp_http_client_perform(c) == ESP_OK && esp_http_client_get_status_code(c) / 100 == 2;
                esp_http_client_cleanup(c);
            }
        }
        if (first || ok != last) { ui_set_online(ok); last = ok; first = false; }
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

void bridge_start(void)
{
    s_q = xQueueCreate(6, sizeof(job_t));
    xTaskCreate(worker, "bridge", 5120, NULL, 3, NULL);
    xTaskCreate(probe_task, "bridge_probe", 4096, NULL, 2, NULL);
}
