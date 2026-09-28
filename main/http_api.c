#include "http_api.h"
#include "ui.h"
#include "ui_render.h"

#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "http";
static httpd_handle_t s_server;

#define BODY_MAX 512

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
    ui_set_say(body);
    return ok(req);
}

esp_err_t http_api_start(void)
{
    if (s_server) return ESP_OK;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }
    const httpd_uri_t routes[] = {
        { .uri = "/ping", .method = HTTP_GET,  .handler = ping_get },
        { .uri = "/face", .method = HTTP_POST, .handler = face_post },
        { .uri = "/say",  .method = HTTP_POST, .handler = say_post },
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }
    ESP_LOGI(TAG, "listening on :80  (GET /ping, POST /face, POST /say)");
    return ESP_OK;
}
