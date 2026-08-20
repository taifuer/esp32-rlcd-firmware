#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PAGE_SECONDARY_TIMEOUT_MS 30000U
#define APP_PAGE_DEVICE_STATUS_TIMEOUT_MS 15000U

typedef enum {
    APP_PAGE_HOME = 0,
    APP_PAGE_CALENDAR,
    APP_PAGE_FIRMWARE,
    APP_PAGE_DEVICE_STATUS,
} app_page_t;

typedef struct {
    app_page_t current;
    app_page_t status_return;
    uint32_t inactive_ms;
} app_page_state_t;

void app_page_state_init(app_page_state_t *state);
app_page_t app_page_state_current(const app_page_state_t *state);
void app_page_state_boot_short_press(app_page_state_t *state);
void app_page_state_key_short_press(app_page_state_t *state);
void app_page_state_note_activity(app_page_state_t *state);
bool app_page_state_tick(app_page_state_t *state, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif
