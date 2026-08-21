#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PAGE_SECONDARY_TIMEOUT_MS 30000U
#define APP_PAGE_MANUAL_SYNC_HOLD_MS 2000U
#define APP_PAGE_AUDIO_TEST_HOLD_MS 2000U
#define APP_PAGE_WIFI_RESET_HOLD_MS 5000U
#define APP_PAGE_FIRMWARE_UPDATE_HOLD_MS 3000U

typedef enum {
    APP_PAGE_HOME = 0,
    APP_PAGE_CALENDAR,
    APP_PAGE_DEVICE_HEALTH,
    APP_PAGE_NETWORK_TIME,
    APP_PAGE_AUDIO,
    APP_PAGE_WIFI_MAINTENANCE,
    APP_PAGE_ABOUT_UPDATE,
} app_page_t;

typedef enum {
    APP_PAGE_ACTION_NONE = 0,
    APP_PAGE_ACTION_SYNC_TIME,
    APP_PAGE_ACTION_TEST_AUDIO,
    APP_PAGE_ACTION_RESET_WIFI,
    APP_PAGE_ACTION_START_UPDATE,
} app_page_action_t;

typedef struct {
    app_page_t current;
    uint32_t inactive_ms;
} app_page_state_t;

void app_page_state_init(app_page_state_t *state);
app_page_t app_page_state_current(const app_page_state_t *state);
bool app_page_is_daily(app_page_t page);
bool app_page_is_system(app_page_t page);
app_page_action_t app_page_key_hold_action(app_page_t page);
uint32_t app_page_key_hold_threshold_ms(app_page_t page);
void app_page_state_boot_short_press(app_page_state_t *state);
void app_page_state_key_short_press(app_page_state_t *state);
void app_page_state_note_activity(app_page_state_t *state);
bool app_page_state_tick(app_page_state_t *state, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif
