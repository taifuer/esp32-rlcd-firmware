#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "weather_config_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_CONFIG_RECORD_ENCODED_SIZE 467U

typedef enum {
    WEATHER_CONFIG_RECORD_SLOT_NONE = 0,
    WEATHER_CONFIG_RECORD_SLOT_A,
    WEATHER_CONFIG_RECORD_SLOT_B,
} weather_config_record_slot_t;

typedef struct {
    uint32_t generation;
    weather_config_t config;
} weather_config_record_t;

bool weather_config_record_encode(
    uint32_t generation, const weather_config_t *config,
    uint8_t *encoded, size_t encoded_size);
bool weather_config_record_decode(
    const uint8_t *encoded, size_t encoded_size,
    weather_config_record_t *record);
weather_config_record_slot_t weather_config_record_select_latest(
    const weather_config_record_t *slot_a,
    const weather_config_record_t *slot_b);

#ifdef __cplusplus
}
#endif
