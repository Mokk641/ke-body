#include "http_api.h"
#include "ui.h"
#include "ui_render.h"
#include "audio.h"
#include "light.h"
#include "cam_ui.h"

#include <string.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "http";
static httpd_handle_t s_server;

#define BODY_MAX     512
#define WAV_BODY_MAX (3 * 1024 * 1024)

/* Read the whole request body into buf (NUL-terminated). Returns length or -1. */
static int read_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    size_t total = req->content_len;
    if (total >= buf_size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too long");
        return -1;
    }
    size_t got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, total - got);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return -1;
        }
        got += r;
    }
    buf[got] = 0;
    /* strip trailing newlines */
    while (got && (buf[got - 1] == '\n' || buf[got - 1] == '\r')) buf[--got] = 0;
    return (int)got;
}

static esp_err_t ok(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t ping_get(httpd_req_t *req) { return ok(req); }

static esp_err_t face_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    int n = read_body(req, body, sizeof body);
    if (n < 0) return ESP_FAIL;
    /* a face is a single line */
    char *nl = strchr(body, '\n');
    if (nl) *nl = 0;
    ESP_LOGI(TAG, "face: %s", body);
    ui_set_face(body);
    return ok(req);
}

static esp_err_t say_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    int n = read_body(req, body, sizeof body);
    if (n < 0) return ESP_FAIL;
    ESP_LOGI(TAG, "say: %s", body);
    ui_set_say(body);          /* Ke's message: into the chat log + silent alert */
    return ok(req);
}

/* text she said (speech-to-text result from the PC): her side of the chat, no alert */
static esp_err_t heard_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    int n = read_body(req, body, sizeof body);
    if (n < 0) return ESP_FAIL;
    if (n > 0) ui_chat_add(CHAT_HER, body);
    return ok(req);
}

static esp_err_t buttons_post(httpd_req_t *req)
{
    char body[1024];
    int n = read_body(req, body, sizeof body);
    if (n < 0) return ESP_FAIL;
    esp_err_t err = ui_set_buttons_json(body);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
            "expect JSON: {\"text\":[\"想你了\",...],\"emoji\":[\"♡\",...],\"shake\":\"想你了\"} (max 8 each, 12 chars, <1KB)");
    }
    ESP_LOGI(TAG, "buttons updated");
    return ok(req);
}

static esp_err_t buttons_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_sendstr(req, ui_get_buttons_json());
}

bool app_chime_enabled(void);
void app_set_chime(bool on);

static esp_err_t chime_post(httpd_req_t *req)
{
    char body[16];
    if (read_body(req, body, sizeof body) < 0) return ESP_FAIL;
    if (strcmp(body, "on") && strcmp(body, "off")) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "chime must be on or off");
    app_set_chime(strcmp(body, "on") == 0);
    return ok(req);
}

static esp_err_t peek_post(httpd_req_t *req)
{
    char body[16];
    if (read_body(req, body, sizeof body) < 0) return ESP_FAIL;
    if (strcmp(body, "on") && strcmp(body, "off")) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "peek must be on or off");
    camui_set_peek(strcmp(body, "on") == 0);
    return ok(req);
}

/* remote snapshot: only when "让克看看" is on */
static esp_err_t snap_handler(httpd_req_t *req)
{
    if (!camui_peek()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        return httpd_resp_sendstr(req, "peek is off (POST /peek on, or console: peek on)");
    }
    uint8_t *jpeg = NULL;
    size_t len = 0;
    esp_err_t err = camui_remote_snap(&jpeg, &len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "snap failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera failed");
    }
    httpd_resp_set_type(req, "image/jpeg");
    err = httpd_resp_send(req, (const char *)jpeg, len);
    free(jpeg);
    return err;
}

static esp_err_t anim_post(httpd_req_t *req)
{
    char body[16];
    if (read_body(req, body, sizeof body) < 0) return ESP_FAIL;
    if (strcmp(body, "on") && strcmp(body, "off")) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "anim must be on or off");
    ui_set_anim(strcmp(body, "on") == 0);
    return ok(req);
}

static esp_err_t volume_post(httpd_req_t *req)
{
    char body[32];
    int n = read_body(req, body, sizeof body);
    if (n < 0) return ESP_FAIL;
    char *end;
    long v = strtol(body, &end, 10);
    if (end == body || v < 0 || v > 100) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "volume must be 0-100");
    }
    if (audio_set_volume((int)v) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "audio not ready");
    }
    ESP_LOGI(TAG, "volume: %ld", v);
    return ok(req);
}

static esp_err_t play_post(httpd_req_t *req)
{
    size_t total = req->content_len;
    if (total < 44 || total > WAV_BODY_MAX) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "wav body must be 44 bytes .. 3 MB");
    }
    uint8_t *buf = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!buf) buf = malloc(total);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
    size_t got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, (char *)buf + got, total - got);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(buf);
            return ESP_FAIL;
        }
        got += r;
    }
    esp_err_t err = audio_play_wav(buf, total);
    if (err != ESP_OK) {
        free(buf);
        ESP_LOGW(TAG, "play rejected: %s", esp_err_to_name(err));
        if (err == ESP_ERR_INVALID_STATE) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "audio not ready");
        }
        if (err == ESP_ERR_NO_MEM) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "audio busy, try again");
        }
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "need a PCM 16-bit WAV, mono or stereo, 16000/24000 Hz (8k-48k accepted)");
    }
    ESP_LOGI(TAG, "play: %u bytes queued", (unsigned)total);
    return ok(req);
}

static esp_err_t rotate_post(httpd_req_t *req)
{
    char body[16];
    if (read_body(req, body, sizeof body) < 0) return ESP_FAIL;
    int r = atoi(body);
    if (ui_set_rotation(r) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "rotate must be 0, 90, 180 or 270");
    }
    ESP_LOGI(TAG, "rotate: %d", r);
    return ok(req);
}

static esp_err_t brightness_post(httpd_req_t *req)
{
    char body[16];
    if (read_body(req, body, sizeof body) < 0) return ESP_FAIL;
    char *end;
    long v = strtol(body, &end, 10);
    if (end == body || v < 0 || v > 100) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "brightness must be 0-100 (values below 5 become 5)");
    }
    light_set_bright((int)v);
    ESP_LOGI(TAG, "brightness: %d", light_get_bright());
    return ok(req);
}

static esp_err_t theme_post(httpd_req_t *req)
{
    char body[16];
    if (read_body(req, body, sizeof body) < 0) return ESP_FAIL;
    if (ui_set_theme(body) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "theme must be dark or light");
    }
    ESP_LOGI(TAG, "theme: %s", body);
    return ok(req);
}

esp_err_t http_api_start(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 24;
    cfg.stack_size = 8192;
    cfg.recv_wait_timeout = 10;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/ping",   .method = HTTP_GET,  .handler = ping_get },
        { .uri = "/face",   .method = HTTP_POST, .handler = face_post },
        { .uri = "/say",    .method = HTTP_POST, .handler = say_post },
        { .uri = "/play",   .method = HTTP_POST, .handler = play_post },
        { .uri = "/volume", .method = HTTP_POST, .handler = volume_post },
        { .uri = "/rotate", .method = HTTP_POST, .handler = rotate_post },
        { .uri = "/brightness", .method = HTTP_POST, .handler = brightness_post },
        { .uri = "/theme",  .method = HTTP_POST, .handler = theme_post },
        { .uri = "/heard",  .method = HTTP_POST, .handler = heard_post },
        { .uri = "/buttons", .method = HTTP_POST, .handler = buttons_post },
        { .uri = "/buttons", .method = HTTP_GET,  .handler = buttons_get },
        { .uri = "/anim",   .method = HTTP_POST, .handler = anim_post },
        { .uri = "/chime",  .method = HTTP_POST, .handler = chime_post },
        { .uri = "/peek",   .method = HTTP_POST, .handler = peek_post },
        { .uri = "/snap",   .method = HTTP_GET,  .handler = snap_handler },
        { .uri = "/snap",   .method = HTTP_POST, .handler = snap_handler },
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }
    ESP_LOGI(TAG, "listening on :80");
    return ESP_OK;
}
