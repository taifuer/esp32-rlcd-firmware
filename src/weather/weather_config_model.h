#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_CONFIG_SCHEMA_VERSION 1U
#define WEATHER_API_HOST_MAX_LENGTH 127U
#define WEATHER_API_KEY_MAX_LENGTH 256U
#define WEATHER_DISTRICT_MAX_LENGTH 48U

/* application/x-www-form-urlencoded can expand every accepted byte to
 * three bytes. This bound also covers the legacy district form field. */
#define WEATHER_CONFIG_FORM_MAX_LENGTH 1664U

typedef struct {
    uint16_t schema_version;
    bool enabled;
    char api_host[WEATHER_API_HOST_MAX_LENGTH + 1U];
    char api_key[WEATHER_API_KEY_MAX_LENGTH + 1U];
    uint32_t province_id;
    uint32_t city_id;
    /* Kept in schema v1 so existing records remain readable. New settings
     * are canonicalized to city level and leave this field empty. */
    char district[WEATHER_DISTRICT_MAX_LENGTH + 1U];
} weather_config_t;

/* An empty api_key explicitly means "preserve the stored key". Clearing
 * credentials is only supported by weather_config_clear(). */
typedef struct {
    bool enabled;
    char api_host[WEATHER_API_HOST_MAX_LENGTH + 1U];
    char api_key[WEATHER_API_KEY_MAX_LENGTH + 1U];
    uint32_t province_id;
    uint32_t city_id;
    /* Legacy input compatibility only; weather_config_apply_update() ignores
     * this field and stores an empty district. */
    char district[WEATHER_DISTRICT_MAX_LENGTH + 1U];
} weather_config_update_t;

/* Presentation code never receives the API key. */
typedef struct {
    bool enabled;
    bool configured;
    bool key_saved;
    char api_host[WEATHER_API_HOST_MAX_LENGTH + 1U];
    uint32_t province_id;
    uint32_t city_id;
    /* Retained for source compatibility and always empty. */
    char district[WEATHER_DISTRICT_MAX_LENGTH + 1U];
    uint32_t generation;
} weather_config_status_t;

typedef enum {
    WEATHER_CONFIG_RESULT_OK = 0,
    WEATHER_CONFIG_RESULT_INVALID_ARGUMENT,
    WEATHER_CONFIG_RESULT_INVALID_API_HOST,
    WEATHER_CONFIG_RESULT_API_KEY_REQUIRED,
    WEATHER_CONFIG_RESULT_INVALID_API_KEY,
    WEATHER_CONFIG_RESULT_LOCATION_REQUIRED,
    WEATHER_CONFIG_RESULT_INVALID_LOCATION,
    WEATHER_CONFIG_RESULT_INVALID_DISTRICT,
    WEATHER_CONFIG_RESULT_INVALID_FORM,
    WEATHER_CONFIG_RESULT_INVALID_ENCODING,
    WEATHER_CONFIG_RESULT_MISSING_FIELD,
    WEATHER_CONFIG_RESULT_DUPLICATE_FIELD,
    WEATHER_CONFIG_RESULT_UNEXPECTED_FIELD,
} weather_config_result_t;

void weather_config_defaults(weather_config_t *config);
void weather_config_reset(weather_config_t *config);
void weather_config_clear_sensitive(void *memory, size_t size);
bool weather_api_host_is_allowed(const char *api_host);
bool weather_config_is_configured(const weather_config_t *config);
weather_config_result_t weather_config_validate(
    const weather_config_t *config);
weather_config_result_t weather_config_apply_update(
    const weather_config_t *current,
    const weather_config_update_t *update,
    weather_config_t *candidate);
weather_config_result_t weather_config_parse_form(
    const char *form, size_t form_length,
    weather_config_update_t *update);
void weather_config_make_status(
    const weather_config_t *config, uint32_t generation,
    weather_config_status_t *status);
const char *weather_config_result_name(weather_config_result_t result);

#ifdef __cplusplus
}
#endif
