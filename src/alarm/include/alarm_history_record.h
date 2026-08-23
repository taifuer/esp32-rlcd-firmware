#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALARM_HISTORY_RECORD_ENCODED_SIZE 16U

/* Only a completed scheduled occurrence is persistent. */
typedef struct {
    uint32_t schedule_revision;
    uint32_t date_key;
} alarm_history_record_t;

bool alarm_history_record_is_valid(
    const alarm_history_record_t *record);
bool alarm_history_record_encode(
    const alarm_history_record_t *record,
    uint8_t *encoded,
    size_t encoded_size);
bool alarm_history_record_decode(
    const uint8_t *encoded,
    size_t encoded_size,
    alarm_history_record_t *record);

#ifdef __cplusplus
}
#endif
