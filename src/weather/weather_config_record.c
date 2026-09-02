#include "weather_config_record.h"

#include <limits.h>
#include <string.h>

#define RECORD_FORMAT_VERSION 1U

enum {
    MAGIC_OFFSET = 0,
    FORMAT_OFFSET = 4,
    SIZE_OFFSET = 6,
    GENERATION_OFFSET = 8,
    SCHEMA_OFFSET = 12,
    ENABLED_OFFSET = 14,
    RESERVED8_OFFSET = 15,
    PROVINCE_ID_OFFSET = 16,
    CITY_ID_OFFSET = 20,
    API_HOST_LENGTH_OFFSET = 24,
    API_KEY_LENGTH_OFFSET = 26,
    DISTRICT_LENGTH_OFFSET = 28,
    RESERVED16_OFFSET = 30,
    API_HOST_OFFSET = 32,
    API_KEY_OFFSET = API_HOST_OFFSET + WEATHER_API_HOST_MAX_LENGTH,
    DISTRICT_OFFSET = API_KEY_OFFSET + WEATHER_API_KEY_MAX_LENGTH,
    CHECKSUM_OFFSET = DISTRICT_OFFSET + WEATHER_DISTRICT_MAX_LENGTH,
};

static const uint8_t RECORD_MAGIC[4] = {'R', 'L', 'C', 'W'};

_Static_assert(CHECKSUM_OFFSET + sizeof(uint32_t) ==
                   WEATHER_CONFIG_RECORD_ENCODED_SIZE,
               "weather config record layout mismatch");

static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0U;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

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

static bool decode_field(const uint8_t *encoded, size_t offset,
                         size_t field_width, size_t text_length,
                         char *destination, size_t capacity)
{
    if (text_length > field_width || capacity <= text_length) {
        return false;
    }
    for (size_t index = 0U; index < text_length; ++index) {
        if (encoded[offset + index] == 0U) {
            return false;
        }
    }
    for (size_t index = text_length; index < field_width; ++index) {
        if (encoded[offset + index] != 0U) {
            return false;
        }
    }
    memcpy(destination, encoded + offset, text_length);
    destination[text_length] = '\0';
    return true;
}

bool weather_config_record_encode(
    uint32_t generation, const weather_config_t *config,
    uint8_t *encoded, size_t encoded_size)
{
    if (encoded == NULL ||
        encoded_size != WEATHER_CONFIG_RECORD_ENCODED_SIZE) {
        return false;
    }
    memset(encoded, 0, encoded_size);
    if (config == NULL ||
        weather_config_validate(config) != WEATHER_CONFIG_RESULT_OK) {
        return false;
    }

    const size_t api_host_length = bounded_length(
        config->api_host, WEATHER_API_HOST_MAX_LENGTH + 1U);
    const size_t api_key_length = bounded_length(
        config->api_key, WEATHER_API_KEY_MAX_LENGTH + 1U);
    const size_t district_length = bounded_length(
        config->district, WEATHER_DISTRICT_MAX_LENGTH + 1U);

    memcpy(encoded + MAGIC_OFFSET, RECORD_MAGIC, sizeof(RECORD_MAGIC));
    put_u16(encoded, FORMAT_OFFSET, RECORD_FORMAT_VERSION);
    put_u16(encoded, SIZE_OFFSET,
            (uint16_t)WEATHER_CONFIG_RECORD_ENCODED_SIZE);
    put_u32(encoded, GENERATION_OFFSET, generation);
    put_u16(encoded, SCHEMA_OFFSET, config->schema_version);
    encoded[ENABLED_OFFSET] = config->enabled ? 1U : 0U;
    encoded[RESERVED8_OFFSET] = 0U;
    put_u32(encoded, PROVINCE_ID_OFFSET, config->province_id);
    put_u32(encoded, CITY_ID_OFFSET, config->city_id);
    put_u16(encoded, API_HOST_LENGTH_OFFSET,
            (uint16_t)api_host_length);
    put_u16(encoded, API_KEY_LENGTH_OFFSET, (uint16_t)api_key_length);
    put_u16(encoded, DISTRICT_LENGTH_OFFSET,
            (uint16_t)district_length);
    put_u16(encoded, RESERVED16_OFFSET, 0U);
    memcpy(encoded + API_HOST_OFFSET, config->api_host,
           api_host_length);
    memcpy(encoded + API_KEY_OFFSET, config->api_key, api_key_length);
    memcpy(encoded + DISTRICT_OFFSET, config->district,
           district_length);
    put_u32(encoded, CHECKSUM_OFFSET,
            record_checksum(encoded, CHECKSUM_OFFSET));
    return true;
}

bool weather_config_record_decode(
    const uint8_t *encoded, size_t encoded_size,
    weather_config_record_t *record)
{
    if (record == NULL) {
        return false;
    }
    weather_config_clear_sensitive(record, sizeof(*record));
    if (encoded == NULL ||
        encoded_size != WEATHER_CONFIG_RECORD_ENCODED_SIZE ||
        memcmp(encoded + MAGIC_OFFSET, RECORD_MAGIC,
               sizeof(RECORD_MAGIC)) != 0 ||
        get_u16(encoded, FORMAT_OFFSET) != RECORD_FORMAT_VERSION ||
        get_u16(encoded, SIZE_OFFSET) !=
            WEATHER_CONFIG_RECORD_ENCODED_SIZE ||
        get_u16(encoded, SCHEMA_OFFSET) !=
            WEATHER_CONFIG_SCHEMA_VERSION ||
        encoded[ENABLED_OFFSET] > 1U ||
        encoded[RESERVED8_OFFSET] != 0U ||
        get_u16(encoded, RESERVED16_OFFSET) != 0U ||
        get_u32(encoded, CHECKSUM_OFFSET) !=
            record_checksum(encoded, CHECKSUM_OFFSET)) {
        return false;
    }

    weather_config_t config = {
        .schema_version = get_u16(encoded, SCHEMA_OFFSET),
        .enabled = encoded[ENABLED_OFFSET] != 0U,
        .province_id = get_u32(encoded, PROVINCE_ID_OFFSET),
        .city_id = get_u32(encoded, CITY_ID_OFFSET),
    };
    const bool decoded =
        decode_field(encoded, API_HOST_OFFSET,
                     WEATHER_API_HOST_MAX_LENGTH,
                     get_u16(encoded, API_HOST_LENGTH_OFFSET),
                     config.api_host, sizeof(config.api_host)) &&
        decode_field(encoded, API_KEY_OFFSET,
                     WEATHER_API_KEY_MAX_LENGTH,
                     get_u16(encoded, API_KEY_LENGTH_OFFSET),
                     config.api_key, sizeof(config.api_key)) &&
        decode_field(encoded, DISTRICT_OFFSET,
                     WEATHER_DISTRICT_MAX_LENGTH,
                     get_u16(encoded, DISTRICT_LENGTH_OFFSET),
                     config.district, sizeof(config.district));
    if (!decoded || weather_config_validate(&config) !=
                        WEATHER_CONFIG_RESULT_OK) {
        weather_config_clear_sensitive(&config, sizeof(config));
        return false;
    }

    record->generation = get_u32(encoded, GENERATION_OFFSET);
    record->config = config;
    weather_config_clear_sensitive(&config, sizeof(config));
    return true;
}

static bool generation_is_newer(uint32_t candidate, uint32_t reference)
{
    const uint32_t distance = candidate - reference;
    return distance != 0U && distance < UINT32_C(0x80000000);
}

weather_config_record_slot_t weather_config_record_select_latest(
    const weather_config_record_t *slot_a,
    const weather_config_record_t *slot_b)
{
    if (slot_a == NULL) {
        return slot_b == NULL ? WEATHER_CONFIG_RECORD_SLOT_NONE
                              : WEATHER_CONFIG_RECORD_SLOT_B;
    }
    if (slot_b == NULL) {
        return WEATHER_CONFIG_RECORD_SLOT_A;
    }
    return generation_is_newer(slot_b->generation, slot_a->generation)
               ? WEATHER_CONFIG_RECORD_SLOT_B
               : WEATHER_CONFIG_RECORD_SLOT_A;
}
