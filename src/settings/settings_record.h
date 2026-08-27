#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_RECORD_ENCODED_SIZE 32U
#define SETTINGS_RECORD_SCHEMA5_ENCODED_SIZE 32U
#define SETTINGS_RECORD_SCHEMA4_ENCODED_SIZE 32U
#define SETTINGS_RECORD_SCHEMA3_ENCODED_SIZE 28U
#define SETTINGS_RECORD_SCHEMA2_ENCODED_SIZE 24U

typedef enum {
    SETTINGS_RECORD_SLOT_NONE = 0,
    SETTINGS_RECORD_SLOT_A,
    SETTINGS_RECORD_SLOT_B,
} settings_record_slot_t;

typedef struct {
    uint32_t generation;
    app_settings_t settings;
} settings_record_t;

typedef struct {
    settings_record_slot_t source_slot;
    bool write_slot_a;
    bool write_slot_b;
} settings_record_repair_plan_t;

typedef enum {
    SETTINGS_MIGRATION_SOURCE_V1_FIELDS = 0,
    SETTINGS_MIGRATION_SOURCE_SCHEMA2_RECORD,
    SETTINGS_MIGRATION_SOURCE_SCHEMA3_RECORD,
    SETTINGS_MIGRATION_SOURCE_SCHEMA4_RECORD,
    SETTINGS_MIGRATION_SOURCE_SCHEMA5_RECORD,
    SETTINGS_MIGRATION_SOURCE_CURRENT_RECORD,
} settings_migration_source_t;

bool settings_record_encode(uint32_t generation,
                            const app_settings_t *settings,
                            uint8_t *encoded, size_t encoded_size);
bool settings_record_decode(const uint8_t *encoded, size_t encoded_size,
                            settings_record_t *record);
bool settings_record_decode_schema5(const uint8_t *encoded,
                                    size_t encoded_size,
                                    settings_record_t *record);
bool settings_record_decode_schema4(const uint8_t *encoded,
                                    size_t encoded_size,
                                    settings_record_t *record);
bool settings_record_decode_schema3(const uint8_t *encoded,
                                    size_t encoded_size,
                                    settings_record_t *record);
bool settings_record_decode_schema2(const uint8_t *encoded,
                                    size_t encoded_size,
                                    settings_record_t *record);
settings_record_slot_t settings_record_select_latest(
    const settings_record_t *slot_a, const settings_record_t *slot_b);
bool settings_record_plan_repair(
    const settings_record_t *slot_a, const settings_record_t *slot_b,
    settings_record_repair_plan_t *plan);
bool settings_record_repair_is_usable(
    const settings_record_repair_plan_t *plan,
    bool repair_write_succeeded);
settings_migration_source_t settings_record_select_migration_source(
    bool current_record_valid, bool schema5_record_valid,
    bool schema4_record_valid, bool schema3_record_valid,
    bool schema2_record_valid);

#ifdef __cplusplus
}
#endif
