#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "conversation_config_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONVERSATION_CONFIG_RECORD_ENCODED_SIZE 409U

typedef enum {
    CONVERSATION_CONFIG_RECORD_SLOT_NONE = 0,
    CONVERSATION_CONFIG_RECORD_SLOT_A,
    CONVERSATION_CONFIG_RECORD_SLOT_B,
} conversation_config_record_slot_t;

typedef struct {
    uint32_t generation;
    conversation_config_t config;
} conversation_config_record_t;

bool conversation_config_record_encode(
    uint32_t generation, const conversation_config_t *config,
    uint8_t *encoded, size_t encoded_size);
bool conversation_config_record_decode(
    const uint8_t *encoded, size_t encoded_size,
    conversation_config_record_t *record);
conversation_config_record_slot_t conversation_config_record_select_latest(
    const conversation_config_record_t *slot_a,
    const conversation_config_record_t *slot_b);

#ifdef __cplusplus
}
#endif
