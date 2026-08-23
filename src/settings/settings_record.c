#include "settings_record.h"

#include <limits.h>

#define SETTINGS_RECORD_MAGIC UINT32_C(0x47464352)
#define SETTINGS_RECORD_FORMAT_VERSION 1U

enum {
    MAGIC_OFFSET = 0,
    FORMAT_OFFSET = 4,
    SIZE_OFFSET = 6,
    GENERATION_OFFSET = 8,
    SCHEMA_OFFSET = 12,
    POWER_OFFSET = 14,
    TEMPERATURE_UNIT_OFFSET = 15,
    UTC_OFFSET = 16,
    AUDIO_VOLUME_OFFSET = 18,
    UPDATE_CHANNEL_OFFSET = 19,
    CHECKSUM_OFFSET = 20,
};

_Static_assert(CHECKSUM_OFFSET + sizeof(uint32_t) ==
                   SETTINGS_RECORD_ENCODED_SIZE,
               "settings record size does not match its layout");

static void put_u16(uint8_t *encoded, size_t offset, uint16_t value)
{
    encoded[offset] = (uint8_t)value;
    encoded[offset + 1U] = (uint8_t)(value >> 8U);
}

static uint16_t get_u16(const uint8_t *encoded, size_t offset)
{
    return (uint16_t)((uint16_t)encoded[offset] |
                      ((uint16_t)encoded[offset + 1U] << 8U));
}

static void put_u32(uint8_t *encoded, size_t offset, uint32_t value)
{
    encoded[offset] = (uint8_t)value;
    encoded[offset + 1U] = (uint8_t)(value >> 8U);
    encoded[offset + 2U] = (uint8_t)(value >> 16U);
    encoded[offset + 3U] = (uint8_t)(value >> 24U);
}

static uint32_t get_u32(const uint8_t *encoded, size_t offset)
{
    return (uint32_t)encoded[offset] |
           ((uint32_t)encoded[offset + 1U] << 8U) |
           ((uint32_t)encoded[offset + 2U] << 16U) |
           ((uint32_t)encoded[offset + 3U] << 24U);
}

static uint32_t record_checksum(const uint8_t *encoded, size_t size)
{
    uint32_t checksum = UINT32_MAX;
    for (size_t index = 0U; index < size; ++index) {
        checksum ^= encoded[index];
        for (unsigned int bit = 0U; bit < CHAR_BIT; ++bit) {
            const uint32_t mask =
                (uint32_t)(0U - (checksum & UINT32_C(1)));
            checksum = (checksum >> 1U) ^
                       (UINT32_C(0xedb88320) & mask);
        }
    }
    return checksum ^ UINT32_MAX;
}

bool settings_record_encode(uint32_t generation,
                            const app_settings_t *settings,
                            uint8_t *encoded, size_t encoded_size)
{
    if (!app_settings_validate(settings) || encoded == NULL ||
        encoded_size != SETTINGS_RECORD_ENCODED_SIZE) {
        return false;
    }

    put_u32(encoded, MAGIC_OFFSET, SETTINGS_RECORD_MAGIC);
    put_u16(encoded, FORMAT_OFFSET, SETTINGS_RECORD_FORMAT_VERSION);
    put_u16(encoded, SIZE_OFFSET, SETTINGS_RECORD_ENCODED_SIZE);
    put_u32(encoded, GENERATION_OFFSET, generation);
    put_u16(encoded, SCHEMA_OFFSET, settings->schema_version);
    encoded[POWER_OFFSET] = (uint8_t)settings->power_mode;
    encoded[TEMPERATURE_UNIT_OFFSET] =
        (uint8_t)settings->temperature_unit;
    put_u16(encoded, UTC_OFFSET,
            (uint16_t)settings->utc_offset_minutes);
    encoded[AUDIO_VOLUME_OFFSET] = settings->audio_playback_volume;
    encoded[UPDATE_CHANNEL_OFFSET] =
        (uint8_t)settings->update_channel;
    put_u32(encoded, CHECKSUM_OFFSET,
            record_checksum(encoded, CHECKSUM_OFFSET));
    return true;
}

bool settings_record_decode(const uint8_t *encoded, size_t encoded_size,
                            settings_record_t *record)
{
    if (encoded == NULL || record == NULL ||
        encoded_size != SETTINGS_RECORD_ENCODED_SIZE ||
        get_u32(encoded, MAGIC_OFFSET) != SETTINGS_RECORD_MAGIC ||
        get_u16(encoded, FORMAT_OFFSET) !=
            SETTINGS_RECORD_FORMAT_VERSION ||
        get_u16(encoded, SIZE_OFFSET) != SETTINGS_RECORD_ENCODED_SIZE ||
        get_u32(encoded, CHECKSUM_OFFSET) !=
            record_checksum(encoded, CHECKSUM_OFFSET)) {
        return false;
    }

    const uint16_t raw_utc_offset = get_u16(encoded, UTC_OFFSET);
    const int32_t signed_utc_offset =
        raw_utc_offset <= INT16_MAX
            ? (int32_t)raw_utc_offset
            : (int32_t)raw_utc_offset - INT32_C(65536);
    const app_settings_t settings = {
        .schema_version = get_u16(encoded, SCHEMA_OFFSET),
        .power_mode = (app_power_mode_t)encoded[POWER_OFFSET],
        .utc_offset_minutes = (int16_t)signed_utc_offset,
        .temperature_unit =
            (app_temperature_unit_t)encoded[TEMPERATURE_UNIT_OFFSET],
        .audio_playback_volume = encoded[AUDIO_VOLUME_OFFSET],
        .update_channel =
            (app_update_channel_t)encoded[UPDATE_CHANNEL_OFFSET],
    };
    if (!app_settings_validate(&settings)) {
        return false;
    }

    *record = (settings_record_t){
        .generation = get_u32(encoded, GENERATION_OFFSET),
        .settings = settings,
    };
    return true;
}

static bool generation_is_newer(uint32_t candidate, uint32_t reference)
{
    const uint32_t distance = candidate - reference;
    return distance != 0U && distance < UINT32_C(0x80000000);
}

settings_record_slot_t settings_record_select_latest(
    const settings_record_t *slot_a, const settings_record_t *slot_b)
{
    if (slot_a == NULL) {
        return slot_b == NULL ? SETTINGS_RECORD_SLOT_NONE
                              : SETTINGS_RECORD_SLOT_B;
    }
    if (slot_b == NULL) {
        return SETTINGS_RECORD_SLOT_A;
    }
    return generation_is_newer(slot_b->generation, slot_a->generation)
               ? SETTINGS_RECORD_SLOT_B
               : SETTINGS_RECORD_SLOT_A;
}

bool settings_record_plan_repair(
    const settings_record_t *slot_a, const settings_record_t *slot_b,
    settings_record_repair_plan_t *plan)
{
    if (plan == NULL) {
        return false;
    }

    *plan = (settings_record_repair_plan_t){
        .source_slot = settings_record_select_latest(slot_a, slot_b),
        .write_slot_a = slot_a == NULL,
        .write_slot_b = slot_b == NULL,
    };
    return true;
}

bool settings_record_repair_is_usable(
    const settings_record_repair_plan_t *plan,
    bool repair_write_succeeded)
{
    return plan != NULL &&
           (plan->source_slot != SETTINGS_RECORD_SLOT_NONE ||
            repair_write_succeeded);
}
