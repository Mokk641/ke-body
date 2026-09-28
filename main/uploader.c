#include "uploader.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "upload";

esp_err_t uploader_post_wav(const char *url, const uint8_t *data, size_t len, int *status)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        ESP_LOGE(TAG, "bad url: %s", url);
        return ESP_ERR_INVALID_ARG;
    }
    esp_http_client_set_header(c, "Content-Type", "audio/wav");
    esp_err_t err = esp_http_client_open(c, (int)len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(c);
        return err;
    }
    size_t sent = 0;
    while (sent < len) {
        int n = esp_http_client_write(c, (const char *)data + sent, (int)(len - sent));
        if (n <= 0) { err = ESP_FAIL; break; }
        sent += n;
    }
    if (err == ESP_OK) {
        if (esp_http_client_fetch_headers(c) < 0) {
            err = ESP_FAIL;
        } else {
            *status = esp_http_client_get_status_code(c);
            char tail[64];
            int r = esp_http_client_read_response(c, tail, sizeof(tail) - 1);
            if (r > 0) { tail[r] = 0; ESP_LOGI(TAG, "server: %d %s", *status, tail); }
            else ESP_LOGI(TAG, "server: %d", *status);
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    ESP_LOGI(TAG, "sent %u/%u bytes to %s (%s)", (unsigned)sent, (unsigned)len, url, esp_err_to_name(err));
    return err;
}
