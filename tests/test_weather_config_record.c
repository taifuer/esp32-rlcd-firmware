#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "weather_config_model.h"
#include "weather_config_record.h"

enum {
    TEST_ENABLED_OFFSET = 14,
    TEST_RESERVED8_OFFSET = 15,
    TEST_API_HOST_OFFSET = 32,
    TEST_API_KEY_OFFSET = TEST_API_HOST_OFFSET +
                          WEATHER_API_HOST_MAX_LENGTH,
    TEST_DISTRICT_OFFSET = TEST_API_KEY_OFFSET +
                           WEATHER_API_KEY_MAX_LENGTH,
    TEST_CHECKSUM_OFFSET = TEST_DISTRICT_OFFSET +
                           WEATHER_DISTRICT_MAX_LENGTH,
};

static void put_u32(uint8_t *encoded, size_t offset, uint32_t value)
{
    encoded[offset] = (uint8_t)value;
    encoded[offset + 1U] = (uint8_t)(value >> 8U);
    encoded[offset + 2U] = (uint8_t)(value >> 16U);
    encoded[offset + 3U] = (uint8_t)(value >> 24U);
}

static uint32_t checksum(const uint8_t *encoded, size_t size)
{
    uint32_t value = UINT32_MAX;
    for (size_t index = 0U; index < size; ++index) {
        value ^= encoded[index];
        for (unsigned int bit = 0U; bit < CHAR_BIT; ++bit) {
            const uint32_t mask =
                (uint32_t)(0U - (value & UINT32_C(1)));
            value = (value >> 1U) ^
                    (UINT32_C(0xedb88320) & mask);
        }
    }
    return value ^ UINT32_MAX;
}

static weather_config_t make_config(bool enabled)
{
    weather_config_t base;
    weather_config_defaults(&base);
    const weather_config_update_t update = {
        .enabled = enabled,
        .api_host = "demo.qweatherapi.com",
        .api_key = "weather-record-secret",
        .province_id = UINT32_C(310000),
        .city_id = UINT32_C(310100),
        .district = "浦东新区",
    };
    weather_config_t config;
    assert(weather_config_apply_update(&base, &update, &config) ==
           WEATHER_CONFIG_RESULT_OK);
    /* Schema/record v1 stored an optional district. Keep decoding those
     * records even though new settings are canonicalized to city level. */
    memcpy(config.district, "浦东新区", sizeof("浦东新区"));
    assert(weather_config_validate(&config) == WEATHER_CONFIG_RESULT_OK);
    weather_config_reset(&base);
    return config;
}

static void assert_configs_equal(const weather_config_t *left,
                                 const weather_config_t *right)
{
    assert(left->schema_version == right->schema_version);
    assert(left->enabled == right->enabled);
    assert(strcmp(left->api_host, right->api_host) == 0);
    assert(strcmp(left->api_key, right->api_key) == 0);
    assert(left->province_id == right->province_id);
    assert(left->city_id == right->city_id);
    assert(strcmp(left->district, right->district) == 0);
}

static void test_round_trip_and_integrity(void)
{
    weather_config_t config = make_config(true);
    uint8_t encoded[WEATHER_CONFIG_RECORD_ENCODED_SIZE];
    assert(weather_config_record_encode(
        UINT32_C(0x78563412), &config, encoded, sizeof(encoded)));
    assert(encoded[0] == 'R' && encoded[1] == 'L' &&
           encoded[2] == 'C' && encoded[3] == 'W');
    assert(encoded[8] == 0x12U && encoded[9] == 0x34U &&
           encoded[10] == 0x56U && encoded[11] == 0x78U);

    weather_config_record_t record = {0};
    assert(weather_config_record_decode(encoded, sizeof(encoded),
                                        &record));
    assert(record.generation == UINT32_C(0x78563412));
    assert_configs_equal(&record.config, &config);

    encoded[TEST_API_KEY_OFFSET] ^= 1U;
    assert(!weather_config_record_decode(encoded, sizeof(encoded),
                                         &record));
    encoded[TEST_API_KEY_OFFSET] ^= 1U;
    assert(!weather_config_record_decode(encoded,
                                         sizeof(encoded) - 1U,
                                         &record));
    assert(!weather_config_record_decode(NULL, sizeof(encoded), &record));
    assert(!weather_config_record_decode(encoded, sizeof(encoded), NULL));
    assert(!weather_config_record_encode(1U, NULL, encoded,
                                         sizeof(encoded)));
    assert(!weather_config_record_encode(1U, &config, encoded,
                                         sizeof(encoded) - 1U));

    weather_config_clear_sensitive(encoded, sizeof(encoded));
    weather_config_clear_sensitive(&record, sizeof(record));
    weather_config_reset(&config);
}

static void test_disabled_empty_record(void)
{
    weather_config_t config;
    weather_config_defaults(&config);
    uint8_t encoded[WEATHER_CONFIG_RECORD_ENCODED_SIZE];
    assert(weather_config_record_encode(0U, &config, encoded,
                                        sizeof(encoded)));
    weather_config_record_t record;
    assert(weather_config_record_decode(encoded, sizeof(encoded),
                                        &record));
    assert(record.generation == 0U);
    assert_configs_equal(&record.config, &config);
    weather_config_clear_sensitive(encoded, sizeof(encoded));
    weather_config_clear_sensitive(&record, sizeof(record));
    weather_config_reset(&config);
}

static void test_canonical_layout_is_enforced(void)
{
    weather_config_t config = make_config(false);
    uint8_t encoded[WEATHER_CONFIG_RECORD_ENCODED_SIZE];
    weather_config_record_t record;
    assert(weather_config_record_encode(1U, &config, encoded,
                                        sizeof(encoded)));

    encoded[TEST_ENABLED_OFFSET] = 2U;
    put_u32(encoded, TEST_CHECKSUM_OFFSET,
            checksum(encoded, TEST_CHECKSUM_OFFSET));
    assert(!weather_config_record_decode(encoded, sizeof(encoded),
                                         &record));

    assert(weather_config_record_encode(1U, &config, encoded,
                                        sizeof(encoded)));
    encoded[TEST_RESERVED8_OFFSET] = 1U;
    put_u32(encoded, TEST_CHECKSUM_OFFSET,
            checksum(encoded, TEST_CHECKSUM_OFFSET));
    assert(!weather_config_record_decode(encoded, sizeof(encoded),
                                         &record));

    assert(weather_config_record_encode(1U, &config, encoded,
                                        sizeof(encoded)));
    const size_t host_length = strlen(config.api_host);
    encoded[TEST_API_HOST_OFFSET + host_length] = 'x';
    put_u32(encoded, TEST_CHECKSUM_OFFSET,
            checksum(encoded, TEST_CHECKSUM_OFFSET));
    assert(!weather_config_record_decode(encoded, sizeof(encoded),
                                         &record));

    weather_config_clear_sensitive(encoded, sizeof(encoded));
    weather_config_clear_sensitive(&record, sizeof(record));
    weather_config_reset(&config);
}

static void test_selection_handles_generation_wrap(void)
{
    weather_config_record_t slot_a = {.generation = 10U};
    weather_config_record_t slot_b = {.generation = 11U};
    assert(weather_config_record_select_latest(&slot_a, &slot_b) ==
           WEATHER_CONFIG_RECORD_SLOT_B);
    assert(weather_config_record_select_latest(&slot_a, NULL) ==
           WEATHER_CONFIG_RECORD_SLOT_A);
    assert(weather_config_record_select_latest(NULL, &slot_b) ==
           WEATHER_CONFIG_RECORD_SLOT_B);
    assert(weather_config_record_select_latest(NULL, NULL) ==
           WEATHER_CONFIG_RECORD_SLOT_NONE);

    slot_a.generation = UINT32_MAX;
    slot_b.generation = 0U;
    assert(weather_config_record_select_latest(&slot_a, &slot_b) ==
           WEATHER_CONFIG_RECORD_SLOT_B);
}

int main(void)
{
    test_round_trip_and_integrity();
    test_disabled_empty_record();
    test_canonical_layout_is_enforced();
    test_selection_handles_generation_wrap();
    puts("weather config record tests passed");
    return 0;
}
