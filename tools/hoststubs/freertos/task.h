#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
static inline BaseType_t xTaskCreatePinnedToCore(void (*f)(void *), const char *n, uint32_t s, void *a, int p, TaskHandle_t *h, int c)
{ (void)f; (void)n; (void)s; (void)a; (void)p; (void)h; (void)c; return pdPASS; }   /* tests drive ui_touch() directly */
static inline void vTaskDelay(TickType_t t) { (void)t; }
