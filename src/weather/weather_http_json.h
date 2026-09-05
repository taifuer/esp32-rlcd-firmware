#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a NUL-terminated JSON buffer owned by the caller. API credentials
 * are sent only in the QWeather request header and are never logged here. */
esp_err_t weather_http_get_json(const char *url, const char *api_key,
                                size_t maximum_json_bytes, char **json,
                                size_t *json_length);
/* Exercises the same bounded Gzip decoder used for live responses without
 * opening the network. */
esp_err_t weather_http_json_self_test(void);

#ifdef __cplusplus
}
#endif
