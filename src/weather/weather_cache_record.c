#include "weather_cache_record.h"

#include <limits.h>
#include <string.h>

#define WEATHER_CACHE_RECORD_MAGIC UINT32_C(0x57434c52)
#define WEATHER_CACHE_RECORD_FORMAT_VERSION 1U

enum {
    MAGIC_OFFSET = 0,
    FORMAT_OFFSET = 4,
    SIZE_OFFSET = 6,
    GENERATION_OFFSET = 8,
    FINGERPRINT_OFFSET = 12,
    COORDINATES_VALID_OFFSET = 16,
    LOCATION_LENGTH_OFFSET = 17,
    FORECAST_COUNT_OFFSET = 18,
    RESERVED_OFFSET = 19,
    LATITUDE_OFFSET = 20,
    LONGITUDE_OFFSET = 24,
    CURRENT_FETCHED_OFFSET = 28,
    DAILY_FETCHED_OFFSET = 36,
    LOCATION_OFFSET = 44,
    CURRENT_TEMPERATURE_OFFSET = 92,
    CURRENT_FEELS_LIKE_OFFSET = 94,
    CURRENT_CODE_OFFSET = 96,
    CURRENT_TEXT_LENGTH_OFFSET = 98,
    CURRENT_RESERVED_OFFSET = 99,
    CURRENT_TEXT_OFFSET = 100,
    FORECAST_OFFSET = 131,
    FORECAST_DATE_BYTES = 10,
    FORECAST_RECORD_BYTES = 49,
    FORECAST_HIGH_OFFSET = 10,
    FORECAST_LOW_OFFSET = 12,
    FORECAST_CODE_OFFSET = 14,
    FORECAST_TEXT_LENGTH_OFFSET = 16,
    FORECAST_PRECIPITATION_OFFSET = 17,
    FORECAST_TEXT_OFFSET = 18,
    CHECKSUM_OFFSET = 278,
};

_Static_assert(CHECKSUM_OFFSET + 4U ==
                   WEATHER_CACHE_RECORD_ENCODED_SIZE,
               "weather cache record layout mismatch");

static size_t bounded_length(const char *text, size_t limit)
{
    size_t length = 0U;
    if (text == NULL) {
        return limit;
    }
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool valid_utf8(const char *text, size_t maximum_bytes)
{
    const size_t length = bounded_length(text, maximum_bytes + 1U);
    if (length == 0U || length > maximum_bytes) {
        return false;
    }
    size_t offset = 0U;
    while (offset < length) {
        const uint8_t first = (uint8_t)text[offset];
        size_t continuation = 0U;
        uint32_t codepoint = 0U;
        if (first >= 0x20U && first <= 0x7eU) {
            ++offset;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            continuation = 1U;
            codepoint = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuation = 2U;
            codepoint = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation = 3U;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (continuation > length - offset - 1U) {
            return false;
        }
        for (size_t index = 1U; index <= continuation; ++index) {
            const uint8_t next = (uint8_t)text[offset + index];
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((continuation == 2U && codepoint < 0x800U) ||
            (continuation == 3U && codepoint < 0x10000U) ||
            codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return false;
        }
        offset += continuation + 1U;
    }
    return true;
}

static bool valid_date(const char *date)
{
    if (date == NULL || bounded_length(date, WEATHER_DATE_CAPACITY) != 10U ||
        date[4] != '-' || date[7] != '-') {
        return false;
    }
    for (size_t index = 0U; index < 10U; ++index) {
        if (index != 4U && index != 7U &&
            (date[index] < '0' || date[index] > '9')) {
            return false;
        }
    }
    return true;
}

static bool valid_temperature(int16_t value)
{
    return value >= -1200 && value <= 1000;
}

bool weather_cache_data_is_valid(const weather_cache_data_t *data)
{
    if (data == NULL || data->location_fingerprint == 0U ||
        data->latitude_ten_thousandths < -900000 ||
        data->latitude_ten_thousandths > 900000 ||
        data->longitude_ten_thousandths < -1800000 ||
        data->longitude_ten_thousandths > 1800000 ||
        !valid_utf8(data->location_name,
                    WEATHER_CACHE_LOCATION_NAME_MAX_BYTES) ||
        data->snapshot.fetched_at_epoch_seconds <= 0 ||
        data->daily_fetched_at_epoch_seconds <= 0 ||
        data->snapshot.daily.day_count == 0U ||
        data->snapshot.daily.day_count > WEATHER_FORECAST_DAY_LIMIT ||
        !valid_temperature(data->snapshot.current.temperature_tenths_celsius) ||
        !valid_temperature(data->snapshot.current.feels_like_tenths_celsius) ||
        data->snapshot.current.condition_code == 0U ||
        data->snapshot.current.condition_code > 999U ||
        !valid_utf8(data->snapshot.current.condition_text,
                    WEATHER_CONDITION_TEXT_MAX_BYTES)) {
        return false;
    }
    for (size_t index = 0U;
         index < data->snapshot.daily.day_count; ++index) {
        const weather_forecast_day_t *day =
            &data->snapshot.daily.days[index];
        if (!valid_date(day->date) ||
            !valid_temperature(day->temperature_high_tenths_celsius) ||
            !valid_temperature(day->temperature_low_tenths_celsius) ||
            day->daytime_condition_code == 0U ||
            day->daytime_condition_code > 999U ||
            day->precipitation_probability_percent > 100U ||
            !valid_utf8(day->daytime_condition_text,
                        WEATHER_CONDITION_TEXT_MAX_BYTES)) {
            return false;
        }
    }
    return true;
}

static void put_u16(uint8_t *encoded, size_t offset, uint16_t value)
{
    encoded[offset] = (uint8_t)value;
    encoded[offset + 1U] = (uint8_t)(value >> 8U);
}

static uint16_t get_u16(const uint8_t *encoded, size_t offset)
{
    return (uint16_t)encoded[offset] |
           ((uint16_t)encoded[offset + 1U] << 8U);
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

static void put_u64(uint8_t *encoded, size_t offset, uint64_t value)
{
    for (size_t index = 0U; index < 8U; ++index) {
        encoded[offset + index] = (uint8_t)(value >> (index * 8U));
    }
}

static uint64_t get_u64(const uint8_t *encoded, size_t offset)
{
    uint64_t value = 0U;
    for (size_t index = 0U; index < 8U; ++index) {
        value |= (uint64_t)encoded[offset + index] << (index * 8U);
    }
    return value;
}

static uint32_t checksum(const uint8_t *encoded, size_t length)
{
    uint32_t value = UINT32_MAX;
    for (size_t index = 0U; index < length; ++index) {
        value ^= encoded[index];
        for (unsigned int bit = 0U; bit < CHAR_BIT; ++bit) {
            const uint32_t mask = 0U - (value & 1U);
            value = (value >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~value;
}

static bool decode_text(const uint8_t *encoded, size_t offset,
                        size_t width, size_t length, char *output,
                        size_t capacity)
{
    if (length == 0U || length > width || capacity <= length) {
        return false;
    }
    for (size_t index = length; index < width; ++index) {
        if (encoded[offset + index] != 0U) {
            return false;
        }
    }
    memcpy(output, encoded + offset, length);
    output[length] = '\0';
    return memchr(output, '\0', length) == NULL;
}

bool weather_cache_record_encode(uint32_t generation,
                                 const weather_cache_data_t *data,
                                 uint8_t *encoded, size_t encoded_size)
{
    if (!weather_cache_data_is_valid(data) || encoded == NULL ||
        encoded_size != WEATHER_CACHE_RECORD_ENCODED_SIZE) {
        return false;
    }
    memset(encoded, 0, encoded_size);
    const size_t location_length = strlen(data->location_name);
    const size_t current_text_length =
        strlen(data->snapshot.current.condition_text);

    put_u32(encoded, MAGIC_OFFSET, WEATHER_CACHE_RECORD_MAGIC);
    put_u16(encoded, FORMAT_OFFSET, WEATHER_CACHE_RECORD_FORMAT_VERSION);
    put_u16(encoded, SIZE_OFFSET, WEATHER_CACHE_RECORD_ENCODED_SIZE);
    put_u32(encoded, GENERATION_OFFSET, generation);
    put_u32(encoded, FINGERPRINT_OFFSET, data->location_fingerprint);
    encoded[COORDINATES_VALID_OFFSET] = 1U;
    encoded[LOCATION_LENGTH_OFFSET] = (uint8_t)location_length;
    encoded[FORECAST_COUNT_OFFSET] =
        (uint8_t)data->snapshot.daily.day_count;
    put_u32(encoded, LATITUDE_OFFSET,
            (uint32_t)data->latitude_ten_thousandths);
    put_u32(encoded, LONGITUDE_OFFSET,
            (uint32_t)data->longitude_ten_thousandths);
    put_u64(encoded, CURRENT_FETCHED_OFFSET,
            (uint64_t)data->snapshot.fetched_at_epoch_seconds);
    put_u64(encoded, DAILY_FETCHED_OFFSET,
            (uint64_t)data->daily_fetched_at_epoch_seconds);
    memcpy(encoded + LOCATION_OFFSET, data->location_name, location_length);
    put_u16(encoded, CURRENT_TEMPERATURE_OFFSET,
            (uint16_t)data->snapshot.current.temperature_tenths_celsius);
    put_u16(encoded, CURRENT_FEELS_LIKE_OFFSET,
            (uint16_t)data->snapshot.current.feels_like_tenths_celsius);
    put_u16(encoded, CURRENT_CODE_OFFSET,
            data->snapshot.current.condition_code);
    encoded[CURRENT_TEXT_LENGTH_OFFSET] = (uint8_t)current_text_length;
    memcpy(encoded + CURRENT_TEXT_OFFSET,
           data->snapshot.current.condition_text, current_text_length);

    for (size_t index = 0U;
         index < data->snapshot.daily.day_count; ++index) {
        const weather_forecast_day_t *day =
            &data->snapshot.daily.days[index];
        const size_t base = FORECAST_OFFSET +
                            index * FORECAST_RECORD_BYTES;
        const size_t text_length = strlen(day->daytime_condition_text);
        memcpy(encoded + base, day->date, FORECAST_DATE_BYTES);
        put_u16(encoded, base + FORECAST_HIGH_OFFSET,
                (uint16_t)day->temperature_high_tenths_celsius);
        put_u16(encoded, base + FORECAST_LOW_OFFSET,
                (uint16_t)day->temperature_low_tenths_celsius);
        put_u16(encoded, base + FORECAST_CODE_OFFSET,
                day->daytime_condition_code);
        encoded[base + FORECAST_TEXT_LENGTH_OFFSET] =
            (uint8_t)text_length;
        encoded[base + FORECAST_PRECIPITATION_OFFSET] =
            day->precipitation_probability_percent;
        memcpy(encoded + base + FORECAST_TEXT_OFFSET,
               day->daytime_condition_text, text_length);
    }
    put_u32(encoded, CHECKSUM_OFFSET,
            checksum(encoded, CHECKSUM_OFFSET));
    return true;
}

bool weather_cache_record_decode(const uint8_t *encoded,
                                 size_t encoded_size,
                                 weather_cache_record_t *record)
{
    if (encoded == NULL || record == NULL ||
        encoded_size != WEATHER_CACHE_RECORD_ENCODED_SIZE ||
        get_u32(encoded, MAGIC_OFFSET) != WEATHER_CACHE_RECORD_MAGIC ||
        get_u16(encoded, FORMAT_OFFSET) !=
            WEATHER_CACHE_RECORD_FORMAT_VERSION ||
        get_u16(encoded, SIZE_OFFSET) != WEATHER_CACHE_RECORD_ENCODED_SIZE ||
        encoded[COORDINATES_VALID_OFFSET] != 1U ||
        encoded[RESERVED_OFFSET] != 0U ||
        encoded[CURRENT_RESERVED_OFFSET] != 0U ||
        get_u32(encoded, CHECKSUM_OFFSET) !=
            checksum(encoded, CHECKSUM_OFFSET)) {
        return false;
    }

    weather_cache_record_t candidate = {
        .generation = get_u32(encoded, GENERATION_OFFSET),
        .data = {
            .location_fingerprint = get_u32(encoded, FINGERPRINT_OFFSET),
            .latitude_ten_thousandths =
                (int32_t)get_u32(encoded, LATITUDE_OFFSET),
            .longitude_ten_thousandths =
                (int32_t)get_u32(encoded, LONGITUDE_OFFSET),
            .daily_fetched_at_epoch_seconds =
                (int64_t)get_u64(encoded, DAILY_FETCHED_OFFSET),
            .snapshot = {
                .fetched_at_epoch_seconds =
                    (int64_t)get_u64(encoded, CURRENT_FETCHED_OFFSET),
                .daily = {
                    .day_count = encoded[FORECAST_COUNT_OFFSET],
                },
                .current = {
                    .temperature_tenths_celsius =
                        (int16_t)get_u16(encoded,
                                        CURRENT_TEMPERATURE_OFFSET),
                    .feels_like_tenths_celsius =
                        (int16_t)get_u16(encoded,
                                        CURRENT_FEELS_LIKE_OFFSET),
                    .condition_code = get_u16(encoded,
                                              CURRENT_CODE_OFFSET),
                },
            },
        },
    };
    if (!decode_text(encoded, LOCATION_OFFSET,
                     WEATHER_CACHE_LOCATION_NAME_MAX_BYTES,
                     encoded[LOCATION_LENGTH_OFFSET],
                     candidate.data.location_name,
                     sizeof(candidate.data.location_name)) ||
        !decode_text(encoded, CURRENT_TEXT_OFFSET,
                     WEATHER_CONDITION_TEXT_MAX_BYTES,
                     encoded[CURRENT_TEXT_LENGTH_OFFSET],
                     candidate.data.snapshot.current.condition_text,
                     sizeof(candidate.data.snapshot.current.condition_text))) {
        return false;
    }

    for (size_t index = 0U;
         index < candidate.data.snapshot.daily.day_count &&
         index < WEATHER_FORECAST_DAY_LIMIT; ++index) {
        weather_forecast_day_t *day =
            &candidate.data.snapshot.daily.days[index];
        const size_t base = FORECAST_OFFSET +
                            index * FORECAST_RECORD_BYTES;
        memcpy(day->date, encoded + base, FORECAST_DATE_BYTES);
        day->date[FORECAST_DATE_BYTES] = '\0';
        day->temperature_high_tenths_celsius =
            (int16_t)get_u16(encoded, base + FORECAST_HIGH_OFFSET);
        day->temperature_low_tenths_celsius =
            (int16_t)get_u16(encoded, base + FORECAST_LOW_OFFSET);
        day->daytime_condition_code =
            get_u16(encoded, base + FORECAST_CODE_OFFSET);
        day->precipitation_probability_percent =
            encoded[base + FORECAST_PRECIPITATION_OFFSET];
        if (!decode_text(encoded, base + FORECAST_TEXT_OFFSET,
                         WEATHER_CONDITION_TEXT_MAX_BYTES,
                         encoded[base + FORECAST_TEXT_LENGTH_OFFSET],
                         day->daytime_condition_text,
                         sizeof(day->daytime_condition_text))) {
            return false;
        }
    }
    if (!weather_cache_data_is_valid(&candidate.data)) {
        return false;
    }
    *record = candidate;
    return true;
}

static bool generation_is_newer(uint32_t candidate, uint32_t reference)
{
    const uint32_t distance = candidate - reference;
    return distance != 0U && distance < UINT32_C(0x80000000);
}

weather_cache_record_slot_t weather_cache_record_select_latest(
    const weather_cache_record_t *slot_a,
    const weather_cache_record_t *slot_b)
{
    if (slot_a == NULL) {
        return slot_b == NULL ? WEATHER_CACHE_RECORD_SLOT_NONE
                              : WEATHER_CACHE_RECORD_SLOT_B;
    }
    if (slot_b == NULL) {
        return WEATHER_CACHE_RECORD_SLOT_A;
    }
    return generation_is_newer(slot_b->generation, slot_a->generation)
               ? WEATHER_CACHE_RECORD_SLOT_B
               : WEATHER_CACHE_RECORD_SLOT_A;
}
