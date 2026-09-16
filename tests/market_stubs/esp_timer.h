#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef void *esp_timer_handle_t;
typedef struct {
    void (*callback)(void *);
    void *arg;
    int dispatch_method;
    const char *name;
} esp_timer_create_args_t;
#define ESP_TIMER_TASK 0
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,
                           esp_timer_handle_t *handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t timeout_us);
esp_err_t esp_timer_stop(esp_timer_handle_t handle);
esp_err_t esp_timer_delete(esp_timer_handle_t handle);
int64_t esp_timer_get_time(void);
