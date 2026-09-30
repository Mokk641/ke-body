#include "http_api.h"
#include "ui.h"
#include "ui_render.h"
#include "audio.h"
#include "light.h"
#include "cam_ui.h"
#include "png.h"
#include "ink.h"
#include "pics.h"
#include "imgdec.h"
#include "storage.h"
#include "music.h"

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

/* Read a big binary body (PNG, JPEG ...) into a PSRAM buffer. false = an error reply has been sent. */
static bool recv_alloc(httpd_req_t *req, size_t max, uint8_t **buf, size_t *len)
{
    size_t total = req->content_len;
    if (total == 0 || total > max) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, total ? "body too big" : "empty body");
        return false;
    }
    uint8_t *b = heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM);
    if (!b) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory"); return false; }
    size_t got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, (char *)b + got, total - got);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            free(b);
            return false;
        }
        got += (size_t)r;
    }
    b[got] = 0;
    *buf = b;
    *len = got;
    return true;
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

/* POST /ink: a PNG of Ke's handwriting -> a picture bubble on Ke's side (white ink, whatever the PNG's colours) */
static esp_err_t ink_post(httpd_req_t *req)
{
    uint8_t *buf;
    size_t n;
    if (!recv_alloc(req, 1024 * 1024, &buf, &n)) return ESP_FAIL;
    int bw, bh;
    ui_chat_picture_box(&bw, &bh);
    if (bw > INK_THUMB_MAX_W) bw = INK_THUMB_MAX_W;
    if (bh > INK_THUMB_MAX_H) bh = INK_THUMB_MAX_H;
    uint8_t *mask = NULL;
    int w, h;
    bool good = png_decode_ink_mask(buf, n, bw, bh, &mask, &w, &h);
    free(buf);
    if (!good) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "not a PNG the board can read (no interlacing; max 8192 px, 6 MB unpacked)");
    int slot = ink_thumb_store(mask, w, h);
    free(mask);
    if (slot <= 0) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    ESP_LOGI(TAG, "ink from Ke: %u bytes -> %dx%d", (unsigned)n, w, h);
    ui_ke_ink(slot);
    return ok(req);
}

/* POST /image: a JPEG or PNG from Ke -> thumbnail bubble on his side (tap: full screen); a copy goes to /sdcard/FROMKE */
static esp_err_t image_post(httpd_req_t *req)
{
    uint8_t *buf;
    size_t n;
    if (!recv_alloc(req, 3 * 1024 * 1024, &buf, &n)) return ESP_FAIL;
    int W, H, w, h;
    ui_screen_size(&W, &H);
    uint16_t *rgb = NULL;
    const char *ext = imgdec_ext(buf, n);
    bool good = imgdec_decode(buf, n, W, H, &rgb, &w, &h);
    if (!good) {
        free(buf);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "not a picture the board can decode (baseline JPEG, or PNG without interlacing)");
    }
    storage_init();
    char name[STORAGE_NAME_LEN];
    if (ext && storage_save_fromke(buf, n, ext, name, sizeof name) == ESP_OK) ESP_LOGI(TAG, "kept /FROMKE/%s", name);
    free(buf);
    int id = pics_store(rgb, w, h);
    if (id <= 0) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    ESP_LOGI(TAG, "picture from Ke: %dx%d", w, h);
    ui_ke_picture(id);
    return ok(req);
}

static void percent_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (s[0] == '%' && s[1] && s[2]) {
            char hex[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 2;
        } else {
            *o++ = s[0] == '+' ? ' ' : s[0];
        }
    }
    *o = 0;
}

/* POST /music: an MP3 from the PC (header X-Title = the name, percent-encoded UTF-8). It goes to /sdcard/MUSIC, or to RAM
 * when there is no card, starts playing at once and appears on top of the list. */
static esp_err_t music_post(httpd_req_t *req)
{
    char title[MUSIC_TITLE_LEN + 32] = "song";
    if (httpd_req_get_hdr_value_str(req, "X-Title", title, sizeof title) == ESP_OK) percent_decode(title);
    size_t total = req->content_len;
    if (total < 1024) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "that is not an MP3");
    music_sink_t *sink = music_sink_open(title, total);
    if (!sink) {
        httpd_resp_set_status(req, "507 Insufficient Storage");
        return httpd_resp_sendstr(req, "no room: put an SD card in (without a card a song may be at most 5 MB)");
    }
    uint8_t *chunk = malloc(4096);
    if (!chunk) { music_sink_finish(sink, false); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory"); }
    size_t got = 0;
    bool good = true;
    while (got < total && good) {
        int r = httpd_req_recv(req, (char *)chunk, total - got < 4096 ? total - got : 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { good = false; break; }
        good = music_sink_write(sink, chunk, (size_t)r);
        got += (size_t)r;
    }
    free(chunk);
    char shown[MUSIC_TITLE_LEN + 4];
    snprintf(shown, sizeof shown, "%s", music_sink_title(sink));
    if (!good) { music_sink_finish(sink, false); return ESP_FAIL; }
    if (!music_sink_finish(sink, true)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not keep the song");
    ESP_LOGI(TAG, "song from Ke: %s (%u bytes)", shown, (unsigned)got);
    char line[MUSIC_TITLE_LEN + 8];
    snprintf(line, sizeof line, "♪ %s", shown);
    ui_set_say(line);
    return ok(req);
}

static esp_err_t buttons_post(httpd_req_t *req)
{
    char *body = malloc(BUTTONS_JSON_MAX + 1);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    int n = read_body(req, body, BUTTONS_JSON_MAX + 1);
    if (n < 0) { free(body); return ESP_FAIL; }
    esp_err_t err = (strcmp(body, "reset") == 0) ? ui_reset_buttons() : ui_set_buttons_json(body);
    free(body);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
            "expect JSON: {\"text\":[\"想你了\",...],\"emoji\":[\"♡\",...],\"shake\":\"想你了\"} (max 8 phrases / 30 emoji, <2KB) or the word reset");
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

/* body: "on" | "off" (master), "<name> on|off" (blink blush zzz shake flash), "status" */
static esp_err_t anim_post(httpd_req_t *req)
{
    char body[48];
    if (read_body(req, body, sizeof body) < 0) return ESP_FAIL;
    if (strcmp(body, "status") == 0) {
        char st[128];
        ui_anim_status(st, sizeof st);
        return httpd_resp_sendstr(req, st);
    }
    char name[16] = "all", val[8];
    if (sscanf(body, "%15s %7s", name, val) != 2) {
        snprintf(val, sizeof val, "%.7s", body);
        snprintf(name, sizeof name, "all");
    }
    if (strcmp(val, "on") && strcmp(val, "off"))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body: on|off  or  <blink|blush|zzz|shake|flash> on|off");
    if (!ui_anim_set(name, strcmp(val, "on") == 0))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown animation (blink blush zzz shake flash all)");
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
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "theme must be light, dark or auto");
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
        { .uri = "/ink",    .method = HTTP_POST, .handler = ink_post },
        { .uri = "/image",  .method = HTTP_POST, .handler = image_post },
        { .uri = "/music",  .method = HTTP_POST, .handler = music_post },
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
