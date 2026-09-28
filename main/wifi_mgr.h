#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    WIFI_MGR_NO_CREDS,      /* nothing in NVS: waiting for `wifi <ssid> <password>` */
    WIFI_MGR_CONNECTING,
    WIFI_MGR_CONNECTED,     /* ip is valid */
    WIFI_MGR_DISCONNECTED,  /* lost link, retrying */
} wifi_mgr_state_t;

typedef void (*wifi_mgr_cb_t)(wifi_mgr_state_t state, const char *ip);

/* Reads credentials from NVS and starts STA mode. Calls cb from the event task. */
esp_err_t wifi_mgr_start(wifi_mgr_cb_t cb);

/* Persist credentials (does not reconnect; caller usually restarts). */
esp_err_t wifi_mgr_save_creds(const char *ssid, const char *pass);
esp_err_t wifi_mgr_clear_creds(void);
bool wifi_mgr_get_ssid(char *out, unsigned out_len);

wifi_mgr_state_t wifi_mgr_state(void);
const char *wifi_mgr_ip(void);   /* "" when not connected */
