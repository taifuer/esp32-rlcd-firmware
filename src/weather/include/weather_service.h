#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "weather_cache_record.h"
#include "weather_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEATHER_SERVICE_STATE_DISABLED = 0,
    WEATHER_SERVICE_STATE_NO_DATA,
    WEATHER_SERVICE_STATE_REFRESHING,
    WEATHER_SERVICE_STATE_READY,
    WEATHER_SERVICE_STATE_FAILED,
} weather_service_state_t;

typedef enum {
    WEATHER_FAILURE_STAGE_NONE = 0,
    WEATHER_FAILURE_STAGE_NETWORK,
    WEATHER_FAILURE_STAGE_LOCATION,
    WEATHER_FAILURE_STAGE_CURRENT,
    WEATHER_FAILURE_STAGE_DAILY,
    WEATHER_FAILURE_STAGE_CACHE,
} weather_failure_stage_t;

typedef struct {
    weather_service_state_t state;
    bool config_enabled;
    bool automatic_refresh_enabled;
    bool data_available;
    bool interactive_refresh;
    /* Current conditions and daily forecast age independently. */
    weather_freshness_t freshness;
    weather_freshness_t daily_freshness;
    int64_t daily_fetched_at_epoch_seconds;
    weather_failure_stage_t failure_stage;
    esp_err_t last_error;
    uint32_t revision;
    char location_name[WEATHER_CACHE_LOCATION_NAME_CAPACITY];
    weather_snapshot_t snapshot;
} weather_service_status_t;

esp_err_t weather_service_init(bool automatic_refresh_enabled);
esp_err_t weather_service_get_status(weather_service_status_t *status,
                                     int64_t now_epoch_seconds,
                                     bool rtc_valid);
esp_err_t weather_service_set_automatic_refresh_enabled(bool enabled);
/* Start one user-visible refresh. A queued or running refresh is not
 * duplicated and returns ESP_ERR_INVALID_STATE. */
esp_err_t weather_service_request_refresh(void);
/* Called only after the settings HTTP response has been sent and its SoftAP
 * has stopped. The worker atomically takes over the maintenance session. */
esp_err_t weather_service_request_refresh_from_maintenance(void);
/* Dismiss the terminal result of a user-visible refresh. */
esp_err_t weather_service_acknowledge_interactive_refresh(void);
/* Re-evaluate enablement immediately after a save that does not fetch. */
esp_err_t weather_service_notify_configuration_changed(void);
/* Clear cached weather together with a user-requested config clear. */
esp_err_t weather_service_clear_cache(void);

const char *weather_service_state_name(weather_service_state_t state);

#ifdef __cplusplus
}
#endif
