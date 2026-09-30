#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct esp_timer *esp_timer_handle_t;
typedef struct { void (*callback)(void *); void *arg; const char *name; } esp_timer_create_args_t;
extern int64_t host_now_us;                       /* set by the test */
extern void (*host_tick_cb)(void *);
static inline int64_t esp_timer_get_time(void) { return host_now_us; }
static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *h) { host_tick_cb = a->callback; *h = (esp_timer_handle_t)1; return ESP_OK; }
static inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t h, uint64_t us) { (void)h; (void)us; return ESP_OK; }
