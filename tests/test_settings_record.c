#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "settings_model.h"
#include "settings_record.h"

static app_settings_t make_settings(uint8_t volume,
                                    app_update_channel_t channel)
{
    app_settings_t settings;
    app_settings_defaults(&settings);
    const uint8_t legacy_order[] = APP_DAILY_PAGES_LEGACY_ORDER;
    memcpy(settings.daily_pages_order, legacy_order, sizeof(legacy_order));
    settings.manual_saving_requested = true;
    settings.utc_offset_minutes = -300;
    settings.temperature_unit = APP_TEMPERATURE_UNIT_FAHRENHEIT;
    settings.audio_playback_volume = volume;
    settings.update_channel = channel;
    settings.alarm_enabled = true;
    settings.alarm_hour = 6U;
    settings.alarm_minute = 45U;
    settings.alarm_weekdays = APP_SETTINGS_ALARM_WEEKENDS_MASK;
    return settings;
}

static void assert_settings_equal(const app_settings_t *actual,
                                  const app_settings_t *expected)
{
    assert(actual->schema_version == expected->schema_version);
    assert(actual->manual_saving_requested ==
           expected->manual_saving_requested);
    assert(actual->utc_offset_minutes == expected->utc_offset_minutes);
    assert(actual->temperature_unit == expected->temperature_unit);
    assert(actual->audio_playback_volume ==
           expected->audio_playback_volume);
    assert(actual->alarm_volume == expected->alarm_volume);
    assert(actual->update_channel == expected->update_channel);
    assert(actual->alarm_enabled == expected->alarm_enabled);
    assert(actual->alarm_hour == expected->alarm_hour);
    assert(actual->alarm_minute == expected->alarm_minute);
    assert(actual->alarm_weekdays == expected->alarm_weekdays);
    assert(actual->daily_pages_mask == expected->daily_pages_mask);
    assert(memcmp(actual->daily_pages_order, expected->daily_pages_order,
                  sizeof(actual->daily_pages_order)) == 0);
    assert(actual->music_mode == expected->music_mode);
    assert(actual->timer_minutes == expected->timer_minutes);
}

static void test_codec_round_trip_and_layout(void)
{
    const app_settings_t settings =
        make_settings(73U, APP_UPDATE_CHANNEL_BETA);
    uint8_t encoded[SETTINGS_RECORD_ENCODED_SIZE];
    assert(settings_record_encode(UINT32_C(0x78563412), &settings,
                                  encoded, sizeof(encoded)));

    assert(encoded[0] == 'R');
    assert(encoded[1] == 'C');
    assert(encoded[2] == 'F');
    assert(encoded[3] == 'G');
    assert(encoded[4] == 11U && encoded[5] == 0U);
    assert(encoded[6] == SETTINGS_RECORD_ENCODED_SIZE &&
           encoded[7] == 0U);
    assert(encoded[8] == 0x12U && encoded[9] == 0x34U &&
           encoded[10] == 0x56U && encoded[11] == 0x78U);
    assert(encoded[12] == APP_SETTINGS_SCHEMA_VERSION &&
           encoded[13] == 0U);
    assert(encoded[14] == 1U);
    assert(encoded[15] == APP_TEMPERATURE_UNIT_FAHRENHEIT);
    assert(encoded[16] == 0xd4U && encoded[17] == 0xfeU);
    assert(encoded[18] == 73U);
    assert(encoded[19] == APP_UPDATE_CHANNEL_BETA);
    assert(encoded[20] == 1U);
    assert(encoded[21] == 6U);
    assert(encoded[22] == 45U);
    assert(encoded[23] == APP_SETTINGS_ALARM_WEEKENDS_MASK);
    assert(encoded[24] == 0U);
    assert(encoded[25] == 0U); /* retired DISPLAY: canonical CLOCK on write */
    assert(encoded[26] == 68U); /* independent alarm volume, CRC covered */
    assert(encoded[27] == APP_DAILY_PAGES_ALL_MASK);
    assert(encoded[28] == APP_MUSIC_MODE_SEQUENTIAL);
    assert(memcmp(encoded + 29U, settings.daily_pages_order, APP_DAILY_PAGE_COUNT) == 0);
    assert(encoded[36] == APP_SETTINGS_DEFAULT_TIMER_MINUTES);
    assert(encoded[37] == 0U && encoded[38] == 0U && encoded[39] == 0U);

    settings_record_t decoded = {0};
    assert(settings_record_decode(encoded, sizeof(encoded), &decoded));
    assert(decoded.generation == UINT32_C(0x78563412));
    assert_settings_equal(&decoded.settings, &settings);
}

static void test_current_manual_saving_round_trip(void)
{
    const bool requests[] = {false, true};
    for (size_t index = 0U;
         index < sizeof(requests) / sizeof(requests[0]);
         ++index) {
        app_settings_t settings =
            make_settings((uint8_t)(40U + index),
                          APP_UPDATE_CHANNEL_STABLE);
        settings.manual_saving_requested = requests[index];
        uint8_t encoded[SETTINGS_RECORD_ENCODED_SIZE];
        assert(settings_record_encode((uint32_t)(10U + index), &settings,
                                      encoded, sizeof(encoded)));
        assert(encoded[14] == (requests[index] ? 1U : 0U));

        settings_record_t decoded = {0};
        assert(settings_record_decode(encoded, sizeof(encoded), &decoded));
        assert_settings_equal(&decoded.settings, &settings);
    }
}

static uint32_t legacy_checksum(const uint8_t *encoded, size_t size)
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

static void put_legacy_u32(uint8_t *encoded, size_t offset,
                           uint32_t value)
{
    encoded[offset] = (uint8_t)value;
    encoded[offset + 1U] = (uint8_t)(value >> 8U);
    encoded[offset + 2U] = (uint8_t)(value >> 16U);
    encoded[offset + 3U] = (uint8_t)(value >> 24U);
}

static void make_schema5_record(uint8_t legacy_power,
                                uint8_t *encoded)
{
    const uint8_t record[SETTINGS_RECORD_SCHEMA5_ENCODED_SIZE] = {
        'R', 'C', 'F', 'G',
        4U, 0U,
        SETTINGS_RECORD_SCHEMA5_ENCODED_SIZE, 0U,
        0x78U, 0x56U, 0x34U, 0x12U,
        5U, 0U,
        legacy_power,
        APP_TEMPERATURE_UNIT_FAHRENHEIT,
        0xd4U, 0xfeU,
        73U,
        APP_UPDATE_CHANNEL_BETA,
        1U,
        6U,
        45U,
        APP_SETTINGS_ALARM_WEEKENDS_MASK,
        0U,
        0U, 0U, 0U,
        0U, 0U, 0U, 0U,
    };
    for (size_t index = 0U; index < sizeof(record); ++index) {
        encoded[index] = record[index];
    }
    put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
}

static void test_schema6_and_dev1_compatibility(void)
{
    for (uint8_t value = 0U; value <= 2U; ++value) {
        app_settings_t settings = make_settings(39U, APP_UPDATE_CHANNEL_BETA);
        settings.alarm_volume = 39U;
        uint8_t encoded[SETTINGS_RECORD_ENCODED_SIZE];
        assert(settings_record_encode(100U, &settings, encoded, sizeof(encoded)));
        assert(encoded[25] == 0U);
        /* dev.1 used 0=CLOCK, 1=WEATHER, 2=IMAGE. Ignore only this preference,
         * retaining every real setting and the record generation. */
        encoded[25] = value;
        put_legacy_u32(encoded, 40U, legacy_checksum(encoded, 40U));
        settings_record_t decoded = {0};
        assert(settings_record_decode(encoded, sizeof(encoded), &decoded));
        assert_settings_equal(&decoded.settings, &settings);
        assert(decoded.generation == 100U);
        assert(settings_record_encode(101U, &decoded.settings, encoded, sizeof(encoded)));
        assert(encoded[25] == 0U);
        assert(!settings_record_decode_schema6(encoded, sizeof(encoded), &decoded));
        encoded[25] = 3U;
        put_legacy_u32(encoded, 40U, legacy_checksum(encoded, 40U));
        assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
        /* v0.25.0 record: format 5, schema 6, reserved byte must not become
         * a display preference. Every existing setting is retained. */
        encoded[4] = 5U;
        encoded[6] = SETTINGS_RECORD_SCHEMA6_ENCODED_SIZE;
        encoded[12] = 6U;
        put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
        assert(settings_record_decode_schema6(encoded, SETTINGS_RECORD_SCHEMA6_ENCODED_SIZE, &decoded));
        assert_settings_equal(&decoded.settings, &settings);
        assert(decoded.generation == 101U);
        assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
        encoded[18] ^= 1U;
        assert(!settings_record_decode_schema6(encoded, SETTINGS_RECORD_SCHEMA6_ENCODED_SIZE, &decoded));
        assert(!settings_record_decode_schema6(NULL, sizeof(encoded), &decoded));
        assert(!settings_record_decode_schema6(encoded, sizeof(encoded), NULL));
        assert(!settings_record_decode_schema6(encoded, sizeof(encoded) - 1U, &decoded));
    }
    assert(settings_record_select_migration_source(false, true, true, true,
                                                   true, true) ==
           SETTINGS_MIGRATION_SOURCE_SCHEMA6_RECORD);
    assert(settings_record_select_migration_source(true, true, true, true,
                                                   true, true) ==
           SETTINGS_MIGRATION_SOURCE_CURRENT_RECORD);
}

static void test_schema5_record_migration(void)
{
    const bool expected_manual[] = {false, true, false};
    for (uint8_t legacy_power = 0U; legacy_power <= 2U;
         ++legacy_power) {
        uint8_t encoded[SETTINGS_RECORD_SCHEMA5_ENCODED_SIZE];
        make_schema5_record(legacy_power, encoded);

        settings_record_t decoded = {0};
        assert(settings_record_decode_schema5(
            encoded, sizeof(encoded), &decoded));
        assert(!settings_record_decode(encoded, sizeof(encoded),
                                       &decoded));
        assert(decoded.generation == UINT32_C(0x12345678));
        assert(decoded.settings.schema_version ==
               APP_SETTINGS_SCHEMA_VERSION);
        assert(decoded.settings.manual_saving_requested ==
               expected_manual[legacy_power]);
        assert(decoded.settings.utc_offset_minutes == -300);
        assert(decoded.settings.temperature_unit ==
               APP_TEMPERATURE_UNIT_FAHRENHEIT);
        assert(decoded.settings.audio_playback_volume == 73U);
        assert(decoded.settings.update_channel ==
               APP_UPDATE_CHANNEL_BETA);
        assert(decoded.settings.alarm_enabled);
        assert(decoded.settings.alarm_hour == 6U);
        assert(decoded.settings.alarm_minute == 45U);
        assert(decoded.settings.alarm_weekdays ==
               APP_SETTINGS_ALARM_WEEKENDS_MASK);

        uint8_t canonical[SETTINGS_RECORD_ENCODED_SIZE];
        assert(settings_record_encode(decoded.generation,
                                      &decoded.settings, canonical,
                                      sizeof(canonical)));
        assert(canonical[4] == 11U);
        assert(canonical[12] == APP_SETTINGS_SCHEMA_VERSION);
        assert(canonical[14] ==
               (expected_manual[legacy_power] ? 1U : 0U));
    }

    uint8_t encoded[SETTINGS_RECORD_SCHEMA5_ENCODED_SIZE];
    settings_record_t decoded = {0};
    make_schema5_record(3U, encoded);
    assert(!settings_record_decode_schema5(encoded, sizeof(encoded),
                                           &decoded));
    make_schema5_record(0U, encoded);
    encoded[18] ^= 1U;
    assert(!settings_record_decode_schema5(encoded, sizeof(encoded),
                                           &decoded));
    make_schema5_record(0U, encoded);
    encoded[12] = 4U;
    put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
    assert(!settings_record_decode_schema5(encoded, sizeof(encoded),
                                           &decoded));
    assert(!settings_record_decode_schema5(NULL, sizeof(encoded),
                                           &decoded));
    assert(!settings_record_decode_schema5(encoded, sizeof(encoded), NULL));
    assert(!settings_record_decode_schema5(encoded, sizeof(encoded) - 1U,
                                           &decoded));
}

static void test_schema8_migration_and_daily_page_mask(void)
{
    app_settings_t settings = make_settings(0U, APP_UPDATE_CHANNEL_BETA);
    settings.alarm_volume = 81U;
    uint8_t encoded[SETTINGS_RECORD_ENCODED_SIZE];
    settings_record_t decoded = {0};
    for (unsigned mask = 0U; mask <= APP_DAILY_PAGES_ALL_MASK; ++mask) {
        settings.daily_pages_mask = (uint8_t)mask;
        assert(settings_record_encode(42U, &settings, encoded, sizeof(encoded)));
        assert(encoded[27] == mask);
        assert(settings_record_decode(encoded, sizeof(encoded), &decoded));
        assert_settings_equal(&decoded.settings, &settings);
        assert(!settings_record_decode_schema8(encoded, sizeof(encoded), &decoded));
    }
    for (unsigned mask = 128U; mask <= UINT8_MAX; ++mask) {
        encoded[27] = (uint8_t)mask;
        put_legacy_u32(encoded, 40U, legacy_checksum(encoded, 40U));
        assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
    }
    settings.daily_pages_mask = APP_DAILY_PAGES_ALL_MASK;
    assert(settings_record_encode(42U, &settings, encoded, sizeof(encoded)));
    encoded[27] = 0U; /* Valid new value, but invalid unchanged CRC. */
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
    /* Construct v0.28/v0.29's exact format, with independent alarm volume. */
    encoded[4] = 7U;
    encoded[6] = SETTINGS_RECORD_SCHEMA8_ENCODED_SIZE;
    encoded[12] = 8U;
    put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
    assert(settings_record_decode_schema8(encoded, SETTINGS_RECORD_SCHEMA8_ENCODED_SIZE, &decoded));
    assert_settings_equal(&decoded.settings, &settings);
    assert(decoded.generation == 42U);
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
    encoded[26] ^= 1U;
    assert(!settings_record_decode_schema8(encoded, SETTINGS_RECORD_SCHEMA8_ENCODED_SIZE, &decoded));
    assert(!settings_record_decode_schema8(NULL, SETTINGS_RECORD_SCHEMA8_ENCODED_SIZE, &decoded));
    assert(!settings_record_decode_schema8(encoded, SETTINGS_RECORD_SCHEMA8_ENCODED_SIZE, NULL));
    assert(!settings_record_decode_schema8(encoded, SETTINGS_RECORD_SCHEMA8_ENCODED_SIZE - 1U, &decoded));
}

static void test_schema9_migration_and_music_mode(void)
{
    app_settings_t settings = make_settings(21U, APP_UPDATE_CHANNEL_BETA);
    settings.alarm_volume = 91U;
    settings.daily_pages_mask = APP_DAILY_PAGE_CALENDAR | APP_DAILY_PAGE_MUSIC;
    uint8_t encoded[SETTINGS_RECORD_ENCODED_SIZE];
    settings_record_t decoded = {0};
    for (unsigned mode = 0U; mode < 3U; ++mode) {
        settings.music_mode = (app_music_mode_t)mode;
        assert(settings_record_encode(71U, &settings, encoded, sizeof(encoded)));
        assert(encoded[28] == mode);
        assert(settings_record_decode(encoded, sizeof(encoded), &decoded));
        assert_settings_equal(&decoded.settings, &settings);
        assert(!settings_record_decode_schema9(encoded, sizeof(encoded), &decoded));
    }
    for (unsigned mode = 3U; mode <= UINT8_MAX; ++mode) {
        encoded[28] = (uint8_t)mode;
        put_legacy_u32(encoded, 40U, legacy_checksum(encoded, 40U));
        assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
    }
    settings.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
    assert(settings_record_encode(71U, &settings, encoded, sizeof(encoded)));
    encoded[28] = APP_MUSIC_MODE_REPEAT_ONE; /* Valid value, invalid CRC. */
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
    encoded[4] = 8U;
    encoded[6] = SETTINGS_RECORD_SCHEMA9_ENCODED_SIZE;
    encoded[12] = 9U;
    put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
    settings.music_mode = APP_MUSIC_MODE_SEQUENTIAL;
    settings.daily_pages_mask |= APP_DAILY_PAGE_TIMER;
    assert(settings_record_decode_schema9(encoded, SETTINGS_RECORD_SCHEMA9_ENCODED_SIZE, &decoded));
    assert_settings_equal(&decoded.settings, &settings);
    assert(decoded.generation == 71U);
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
    assert(!settings_record_decode_schema9(encoded, sizeof(encoded), &decoded));
    assert(!settings_record_decode_schema9(encoded, SETTINGS_RECORD_SCHEMA9_ENCODED_SIZE - 1U, &decoded));
    assert(!settings_record_decode_schema9(NULL, SETTINGS_RECORD_SCHEMA9_ENCODED_SIZE, &decoded));
    assert(!settings_record_decode_schema9(encoded, SETTINGS_RECORD_SCHEMA9_ENCODED_SIZE, NULL));
    for (unsigned mask = 64U; mask <= UINT8_MAX; ++mask) {
        encoded[27] = (uint8_t)mask;
        put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
        assert(!settings_record_decode_schema9(encoded, SETTINGS_RECORD_SCHEMA9_ENCODED_SIZE, &decoded));
    }
    encoded[27] = 0U;
    put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
    assert(settings_record_decode_schema9(encoded, SETTINGS_RECORD_SCHEMA9_ENCODED_SIZE, &decoded));
    assert(decoded.settings.daily_pages_mask == 0U);
    encoded[26] ^= 1U;
    assert(!settings_record_decode_schema9(encoded, SETTINGS_RECORD_SCHEMA9_ENCODED_SIZE, &decoded));
}

static void test_schema10_migration_and_page_order(void)
{
    app_settings_t settings = make_settings(21U, APP_UPDATE_CHANNEL_BETA);
    settings.alarm_volume = 91U;
    settings.daily_pages_mask = APP_DAILY_PAGE_MUSIC | APP_DAILY_PAGE_CHAT;
    settings.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
    const uint8_t reverse[] = {32U, 16U, 8U, 4U, 2U, 1U, 64U};
    memcpy(settings.daily_pages_order, reverse, sizeof(reverse));
    uint8_t encoded[SETTINGS_RECORD_ENCODED_SIZE];
    settings_record_t decoded;
    assert(settings_record_encode(72U, &settings, encoded, sizeof(encoded)));
    assert(settings_record_decode(encoded, sizeof(encoded), &decoded));
    assert_settings_equal(&decoded.settings, &settings);
    for (size_t index = 0U; index < APP_DAILY_PAGE_COUNT; ++index) {
        for (unsigned id = 0U; id <= UINT8_MAX; ++id) {
            assert(settings_record_encode(72U, &settings, encoded, sizeof(encoded)));
            encoded[29U + index] = (uint8_t)id;
            put_legacy_u32(encoded, 40U, legacy_checksum(encoded, 40U));
            assert(settings_record_decode(encoded, sizeof(encoded), &decoded) ==
                   (id == reverse[index]));
        }
    }
    assert(settings_record_encode(72U, &settings, encoded, sizeof(encoded)));
    /* Real v0.30/31 format preserves music mode/mask; reserved bytes are not IDs. */
    encoded[4] = 9U;
    encoded[6] = SETTINGS_RECORD_SCHEMA10_ENCODED_SIZE;
    encoded[12] = 10U;
    memset(encoded + 29U, 0, 3U);
    put_legacy_u32(encoded, 32U, legacy_checksum(encoded, 32U));
    const uint8_t defaults[] = APP_DAILY_PAGES_LEGACY_ORDER;
    memcpy(settings.daily_pages_order, defaults, sizeof(defaults));
    assert(settings_record_decode_schema10(encoded, SETTINGS_RECORD_SCHEMA10_ENCODED_SIZE, &decoded));
    settings.daily_pages_mask |= APP_DAILY_PAGE_TIMER;
    assert_settings_equal(&decoded.settings, &settings);
    assert(decoded.generation == 72U);
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
    assert(!settings_record_decode_schema10(encoded, sizeof(encoded), &decoded));
    assert(!settings_record_decode_schema10(NULL, SETTINGS_RECORD_SCHEMA10_ENCODED_SIZE, &decoded));
    assert(!settings_record_decode_schema10(encoded, SETTINGS_RECORD_SCHEMA10_ENCODED_SIZE, NULL));
    assert(!settings_record_decode_schema10(encoded, SETTINGS_RECORD_SCHEMA10_ENCODED_SIZE - 1U, &decoded));
    encoded[28] = 3U;
    put_legacy_u32(encoded, 32U, legacy_checksum(encoded, 32U));
    assert(!settings_record_decode_schema10(encoded, SETTINGS_RECORD_SCHEMA10_ENCODED_SIZE, &decoded));
    encoded[28] = APP_MUSIC_MODE_REPEAT_ONE;
    put_legacy_u32(encoded, 32U, legacy_checksum(encoded, 32U));
    encoded[18] ^= 1U;
    assert(!settings_record_decode_schema10(encoded, SETTINGS_RECORD_SCHEMA10_ENCODED_SIZE, &decoded));
}

static void make_schema11_record(uint8_t *encoded, uint8_t mask)
{
    app_settings_t settings = make_settings(21U, APP_UPDATE_CHANNEL_BETA);
    settings.alarm_volume = 91U;
    settings.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
    const uint8_t reverse[] = {32U, 16U, 8U, 4U, 2U, 1U, 64U};
    memcpy(settings.daily_pages_order, reverse, sizeof(reverse));
    assert(settings_record_encode(73U, &settings, encoded, SETTINGS_RECORD_ENCODED_SIZE));
    encoded[4] = 10U;
    encoded[6] = SETTINGS_RECORD_SCHEMA11_ENCODED_SIZE;
    encoded[12] = 11U;
    encoded[27] = mask;
    encoded[35] = 0U; /* v11 reserved byte is not a seventh page ID. */
    put_legacy_u32(encoded, 36U, legacy_checksum(encoded, 36U));
}

static void test_schema11_migration_and_timer_codec(void)
{
    uint8_t encoded[SETTINGS_RECORD_ENCODED_SIZE];
    settings_record_t decoded = {0};
    const uint8_t expected_order[] = {32U, 16U, 8U, 4U, 2U, 1U, 64U};
    for (unsigned mask = 0U; mask <= UINT8_MAX; ++mask) {
        make_schema11_record(encoded, (uint8_t)mask);
        assert(settings_record_decode_schema11(encoded, SETTINGS_RECORD_SCHEMA11_ENCODED_SIZE,
                                               &decoded) == (mask <= 63U));
        if (mask > 63U) continue;
        app_settings_t expected = make_settings(21U, APP_UPDATE_CHANNEL_BETA);
        expected.alarm_volume = 91U;
        expected.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
        expected.daily_pages_mask = mask == 0U ? 0U : (uint8_t)(mask | APP_DAILY_PAGE_TIMER);
        memcpy(expected.daily_pages_order, expected_order, sizeof(expected_order));
        assert_settings_equal(&decoded.settings, &expected);
        assert(decoded.generation == 73U);
    }
    for (size_t index = 0U; index < 6U; ++index) {
        for (unsigned id = 0U; id <= UINT8_MAX; ++id) {
            make_schema11_record(encoded, 63U);
            encoded[29U + index] = (uint8_t)id;
            put_legacy_u32(encoded, 36U, legacy_checksum(encoded, 36U));
            assert(settings_record_decode_schema11(encoded, SETTINGS_RECORD_SCHEMA11_ENCODED_SIZE,
                                                   &decoded) == (id == expected_order[index]));
        }
    }
    make_schema11_record(encoded, 63U);
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
    assert(!settings_record_decode_schema11(encoded, sizeof(encoded), &decoded));
    assert(!settings_record_decode_schema11(NULL, SETTINGS_RECORD_SCHEMA11_ENCODED_SIZE, &decoded));
    assert(!settings_record_decode_schema11(encoded, SETTINGS_RECORD_SCHEMA11_ENCODED_SIZE, NULL));
    assert(!settings_record_decode_schema11(encoded, SETTINGS_RECORD_SCHEMA11_ENCODED_SIZE - 1U, &decoded));
    encoded[18] ^= 1U;
    assert(!settings_record_decode_schema11(encoded, SETTINGS_RECORD_SCHEMA11_ENCODED_SIZE, &decoded));

    app_settings_t settings;
    app_settings_defaults(&settings);
    assert(SETTINGS_RECORD_ENCODED_SIZE == 44U);
    for (unsigned minutes = 0U; minutes <= UINT8_MAX; ++minutes) {
        assert(settings_record_encode(74U, &settings, encoded, sizeof(encoded)));
        encoded[36] = (uint8_t)minutes;
        put_legacy_u32(encoded, 40U, legacy_checksum(encoded, 40U));
        const bool valid = minutes >= 1U && minutes <= 180U;
        assert(settings_record_decode(encoded, sizeof(encoded), &decoded) == valid);
        if (valid) {
            assert(decoded.settings.timer_minutes == minutes);
            assert(settings_record_encode(75U, &decoded.settings, encoded, sizeof(encoded)));
            assert(encoded[36] == minutes);
        }
    }
    assert(settings_record_encode(76U, &settings, encoded, sizeof(encoded)));
    /* Every byte of the new 44-byte record participates in integrity checks. */
    for (size_t index = 0U; index < sizeof(encoded); ++index) {
        encoded[index] ^= 1U;
        assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
        encoded[index] ^= 1U;
    }
}

static void make_schema4_record(uint8_t legacy_power,
                                uint8_t legacy_rotation,
                                uint8_t *encoded)
{
    const uint8_t record[SETTINGS_RECORD_SCHEMA4_ENCODED_SIZE] = {
        'R', 'C', 'F', 'G',
        3U, 0U,
        SETTINGS_RECORD_SCHEMA4_ENCODED_SIZE, 0U,
        0x78U, 0x56U, 0x34U, 0x12U,
        4U, 0U,
        legacy_power,
        APP_TEMPERATURE_UNIT_FAHRENHEIT,
        0xd4U, 0xfeU,
        73U,
        APP_UPDATE_CHANNEL_BETA,
        1U,
        6U,
        45U,
        APP_SETTINGS_ALARM_WEEKENDS_MASK,
        legacy_rotation,
        0U, 0U, 0U,
        0U, 0U, 0U, 0U,
    };
    for (size_t index = 0U; index < sizeof(record); ++index) {
        encoded[index] = record[index];
    }
    put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
}

static void test_schema4_record_migration(void)
{
    for (uint8_t legacy_power = 0U; legacy_power <= 1U;
         ++legacy_power) {
        app_settings_t expected =
            make_settings(73U, APP_UPDATE_CHANNEL_BETA);
        expected.alarm_volume = 73U;
        expected.manual_saving_requested = legacy_power == 1U;

        for (uint8_t legacy_rotation = 0U; legacy_rotation <= 2U;
             ++legacy_rotation) {
            uint8_t encoded[SETTINGS_RECORD_SCHEMA4_ENCODED_SIZE];
            make_schema4_record(legacy_power, legacy_rotation, encoded);

            settings_record_t decoded = {0};
            assert(settings_record_decode_schema4(
                encoded, sizeof(encoded), &decoded));
            assert(!settings_record_decode(encoded, sizeof(encoded),
                                           &decoded));
            assert(decoded.generation == UINT32_C(0x12345678));
            assert_settings_equal(&decoded.settings, &expected);

            uint8_t canonical[SETTINGS_RECORD_ENCODED_SIZE];
            assert(settings_record_encode(decoded.generation,
                                          &decoded.settings, canonical,
                                          sizeof(canonical)));
            assert(canonical[4] == 11U);
            assert(canonical[12] == APP_SETTINGS_SCHEMA_VERSION);
            assert(canonical[14] ==
                   (expected.manual_saving_requested ? 1U : 0U));
            assert(canonical[24] == 0U);
        }
    }

    uint8_t encoded[SETTINGS_RECORD_SCHEMA4_ENCODED_SIZE];
    make_schema4_record(2U, 0U, encoded);
    settings_record_t decoded = {0};
    assert(!settings_record_decode_schema4(encoded, sizeof(encoded),
                                           &decoded));
    make_schema4_record(0U, 3U, encoded);
    assert(!settings_record_decode_schema4(encoded, sizeof(encoded),
                                           &decoded));
    make_schema4_record(0U, 0U, encoded);
    encoded[18] ^= 1U;
    assert(!settings_record_decode_schema4(encoded, sizeof(encoded),
                                           &decoded));
    make_schema4_record(0U, 0U, encoded);
    encoded[12] = 3U;
    put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
    assert(!settings_record_decode_schema4(encoded, sizeof(encoded),
                                           &decoded));
    assert(!settings_record_decode_schema4(NULL, sizeof(encoded), &decoded));
    assert(!settings_record_decode_schema4(encoded, sizeof(encoded), NULL));
    assert(!settings_record_decode_schema4(encoded, sizeof(encoded) - 1U,
                                           &decoded));
}

static void test_schema2_record_migration(void)
{
    uint8_t encoded[SETTINGS_RECORD_SCHEMA2_ENCODED_SIZE] = {
        'R', 'C', 'F', 'G',
        1U, 0U,
        SETTINGS_RECORD_SCHEMA2_ENCODED_SIZE, 0U,
        0x12U, 0x34U, 0x56U, 0x78U,
        2U, 0U,
        1U,
        APP_TEMPERATURE_UNIT_FAHRENHEIT,
        0xd4U, 0xfeU,
        73U,
        APP_UPDATE_CHANNEL_BETA,
        0U, 0U, 0U, 0U,
    };
    put_legacy_u32(encoded, 20U, legacy_checksum(encoded, 20U));

    settings_record_t decoded = {0};
    assert(settings_record_decode_schema2(encoded, sizeof(encoded),
                                          &decoded));
    assert(decoded.generation == UINT32_C(0x78563412));
    assert(decoded.settings.schema_version == APP_SETTINGS_SCHEMA_VERSION);
    assert(decoded.settings.manual_saving_requested);
    assert(decoded.settings.utc_offset_minutes == -300);
    assert(decoded.settings.temperature_unit ==
           APP_TEMPERATURE_UNIT_FAHRENHEIT);
    assert(decoded.settings.audio_playback_volume == 73U);
    assert(decoded.settings.update_channel == APP_UPDATE_CHANNEL_BETA);
    assert(!decoded.settings.alarm_enabled);
    assert(decoded.settings.alarm_hour ==
           APP_SETTINGS_DEFAULT_ALARM_HOUR);
    assert(decoded.settings.alarm_minute ==
           APP_SETTINGS_DEFAULT_ALARM_MINUTE);
    assert(decoded.settings.alarm_weekdays ==
           APP_SETTINGS_ALARM_WEEKDAYS_MASK);

    encoded[14] = 0U;
    put_legacy_u32(encoded, 20U, legacy_checksum(encoded, 20U));
    assert(settings_record_decode_schema2(encoded, sizeof(encoded),
                                          &decoded));
    assert(!decoded.settings.manual_saving_requested);
    encoded[14] = 1U;
    put_legacy_u32(encoded, 20U, legacy_checksum(encoded, 20U));

    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
    encoded[19] ^= 1U;
    assert(!settings_record_decode_schema2(encoded, sizeof(encoded),
                                           &decoded));
    encoded[19] ^= 1U;
    encoded[12] = 1U;
    put_legacy_u32(encoded, 20U, legacy_checksum(encoded, 20U));
    assert(!settings_record_decode_schema2(encoded, sizeof(encoded),
                                           &decoded));
    assert(!settings_record_decode_schema2(NULL, sizeof(encoded),
                                           &decoded));
    assert(!settings_record_decode_schema2(encoded, sizeof(encoded), NULL));
}

static void test_schema3_record_migration(void)
{
    uint8_t encoded[SETTINGS_RECORD_SCHEMA3_ENCODED_SIZE] = {
        'R', 'C', 'F', 'G',
        2U, 0U,
        SETTINGS_RECORD_SCHEMA3_ENCODED_SIZE, 0U,
        0x78U, 0x56U, 0x34U, 0x12U,
        3U, 0U,
        1U,
        APP_TEMPERATURE_UNIT_FAHRENHEIT,
        0xd4U, 0xfeU,
        73U,
        APP_UPDATE_CHANNEL_BETA,
        1U,
        6U,
        45U,
        APP_SETTINGS_ALARM_WEEKENDS_MASK,
        0U, 0U, 0U, 0U,
    };
    put_legacy_u32(encoded, 24U, legacy_checksum(encoded, 24U));

    settings_record_t decoded = {0};
    assert(settings_record_decode_schema3(encoded, sizeof(encoded),
                                          &decoded));
    assert(decoded.generation == UINT32_C(0x12345678));
    assert(decoded.settings.schema_version == APP_SETTINGS_SCHEMA_VERSION);
    assert(decoded.settings.manual_saving_requested);
    assert(decoded.settings.alarm_enabled);
    assert(decoded.settings.alarm_hour == 6U);
    assert(decoded.settings.alarm_minute == 45U);
    assert(decoded.settings.alarm_weekdays ==
           APP_SETTINGS_ALARM_WEEKENDS_MASK);

    encoded[14] = 0U;
    put_legacy_u32(encoded, 24U, legacy_checksum(encoded, 24U));
    assert(settings_record_decode_schema3(encoded, sizeof(encoded),
                                          &decoded));
    assert(!decoded.settings.manual_saving_requested);
    encoded[14] = 1U;
    put_legacy_u32(encoded, 24U, legacy_checksum(encoded, 24U));

    encoded[23] ^= 1U;
    assert(!settings_record_decode_schema3(encoded, sizeof(encoded),
                                           &decoded));
    encoded[23] ^= 1U;
    encoded[12] = 2U;
    put_legacy_u32(encoded, 24U, legacy_checksum(encoded, 24U));
    assert(!settings_record_decode_schema3(encoded, sizeof(encoded),
                                           &decoded));
    assert(!settings_record_decode_schema3(NULL, sizeof(encoded),
                                           &decoded));
    assert(!settings_record_decode_schema3(encoded, sizeof(encoded), NULL));
}

static void test_codec_rejects_invalid_input_and_corruption(void)
{
    app_settings_t settings =
        make_settings(25U, APP_UPDATE_CHANNEL_STABLE);
    uint8_t encoded[SETTINGS_RECORD_ENCODED_SIZE];
    assert(settings_record_encode(7U, &settings, encoded,
                                  sizeof(encoded)));

    assert(!settings_record_encode(7U, NULL, encoded, sizeof(encoded)));
    assert(!settings_record_encode(7U, &settings, NULL, sizeof(encoded)));
    assert(!settings_record_encode(7U, &settings, encoded,
                                   sizeof(encoded) - 1U));
    settings.audio_playback_volume = 101U;
    assert(!settings_record_encode(7U, &settings, encoded,
                                   sizeof(encoded)));

    settings_record_t decoded = {.generation = 99U};
    assert(!settings_record_decode(NULL, sizeof(encoded), &decoded));
    assert(!settings_record_decode(encoded, sizeof(encoded), NULL));
    assert(!settings_record_decode(encoded, sizeof(encoded) - 1U,
                                   &decoded));

    encoded[18] ^= 1U;
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
    assert(decoded.generation == 99U);
    encoded[18] ^= 1U;
    encoded[SETTINGS_RECORD_ENCODED_SIZE - 1U] ^= 1U;
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
    assert(decoded.generation == 99U);

    assert(settings_record_encode(7U, &settings, encoded,
                                  sizeof(encoded)) == false);
    settings = make_settings(25U, APP_UPDATE_CHANNEL_STABLE);
    assert(settings_record_encode(7U, &settings, encoded,
                                  sizeof(encoded)));
    encoded[20] = 2U;
    put_legacy_u32(encoded, 40U, legacy_checksum(encoded, 40U));
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));

    assert(settings_record_encode(7U, &settings, encoded,
                                  sizeof(encoded)));
    encoded[21] = 24U;
    put_legacy_u32(encoded, 40U, legacy_checksum(encoded, 40U));
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));

    assert(settings_record_encode(7U, &settings, encoded,
                                  sizeof(encoded)));
    encoded[23] = 0U;
    put_legacy_u32(encoded, 40U, legacy_checksum(encoded, 40U));
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));

    assert(settings_record_encode(7U, &settings, encoded,
                                  sizeof(encoded)));
    encoded[14] = 2U;
    put_legacy_u32(encoded, 40U, legacy_checksum(encoded, 40U));
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));

    assert(settings_record_encode(7U, &settings, encoded,
                                  sizeof(encoded)));
    encoded[24] = 3U;
    put_legacy_u32(encoded, 40U, legacy_checksum(encoded, 40U));
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));
}

static void test_generation_selection(void)
{
    settings_record_t slot_a = {.generation = 10U};
    settings_record_t slot_b = {.generation = 11U};

    assert(settings_record_select_latest(NULL, NULL) ==
           SETTINGS_RECORD_SLOT_NONE);
    assert(settings_record_select_latest(&slot_a, NULL) ==
           SETTINGS_RECORD_SLOT_A);
    assert(settings_record_select_latest(NULL, &slot_b) ==
           SETTINGS_RECORD_SLOT_B);
    assert(settings_record_select_latest(&slot_a, &slot_b) ==
           SETTINGS_RECORD_SLOT_B);

    slot_a.generation = 12U;
    assert(settings_record_select_latest(&slot_a, &slot_b) ==
           SETTINGS_RECORD_SLOT_A);
    slot_b.generation = 12U;
    assert(settings_record_select_latest(&slot_a, &slot_b) ==
           SETTINGS_RECORD_SLOT_A);

    slot_a.generation = UINT32_MAX;
    slot_b.generation = 0U;
    assert(settings_record_select_latest(&slot_a, &slot_b) ==
           SETTINGS_RECORD_SLOT_B);
    slot_a.generation = 0U;
    slot_b.generation = UINT32_MAX;
    assert(settings_record_select_latest(&slot_a, &slot_b) ==
           SETTINGS_RECORD_SLOT_A);

    slot_a.generation = 0U;
    slot_b.generation = UINT32_C(0x80000000);
    assert(settings_record_select_latest(&slot_a, &slot_b) ==
           SETTINGS_RECORD_SLOT_A);
}

static void test_corrupt_new_slot_falls_back(void)
{
    const app_settings_t older_settings =
        make_settings(31U, APP_UPDATE_CHANNEL_STABLE);
    const app_settings_t newer_settings =
        make_settings(82U, APP_UPDATE_CHANNEL_BETA);
    uint8_t encoded_a[SETTINGS_RECORD_ENCODED_SIZE];
    uint8_t encoded_b[SETTINGS_RECORD_ENCODED_SIZE];
    assert(settings_record_encode(41U, &older_settings, encoded_a,
                                  sizeof(encoded_a)));
    assert(settings_record_encode(42U, &newer_settings, encoded_b,
                                  sizeof(encoded_b)));

    settings_record_t slot_a;
    settings_record_t slot_b;
    assert(settings_record_decode(encoded_a, sizeof(encoded_a), &slot_a));
    assert(settings_record_decode(encoded_b, sizeof(encoded_b), &slot_b));
    assert(settings_record_select_latest(&slot_a, &slot_b) ==
           SETTINGS_RECORD_SLOT_B);

    encoded_b[8] ^= 1U;
    assert(!settings_record_decode(encoded_b, sizeof(encoded_b), &slot_b));
    assert(settings_record_select_latest(&slot_a, NULL) ==
           SETTINGS_RECORD_SLOT_A);
    assert_settings_equal(&slot_a.settings, &older_settings);
}

static void test_repair_plan(void)
{
    settings_record_t slot_a = {.generation = 9U};
    settings_record_t slot_b = {.generation = 10U};
    settings_record_repair_plan_t plan = {0};

    assert(settings_record_plan_repair(NULL, NULL, &plan));
    assert(plan.source_slot == SETTINGS_RECORD_SLOT_NONE);
    assert(plan.write_slot_a);
    assert(plan.write_slot_b);
    assert(!settings_record_repair_is_usable(&plan, false));
    assert(settings_record_repair_is_usable(&plan, true));

    assert(settings_record_plan_repair(&slot_a, NULL, &plan));
    assert(plan.source_slot == SETTINGS_RECORD_SLOT_A);
    assert(!plan.write_slot_a);
    assert(plan.write_slot_b);
    assert(settings_record_repair_is_usable(&plan, false));

    assert(settings_record_plan_repair(NULL, &slot_b, &plan));
    assert(plan.source_slot == SETTINGS_RECORD_SLOT_B);
    assert(plan.write_slot_a);
    assert(!plan.write_slot_b);

    assert(settings_record_plan_repair(&slot_a, &slot_b, &plan));
    assert(plan.source_slot == SETTINGS_RECORD_SLOT_B);
    assert(!plan.write_slot_a);
    assert(!plan.write_slot_b);
    assert(settings_record_repair_is_usable(&plan, false));

    slot_a.generation = UINT32_MAX;
    slot_b.generation = 0U;
    assert(settings_record_plan_repair(&slot_a, &slot_b, &plan));
    assert(plan.source_slot == SETTINGS_RECORD_SLOT_B);
    assert(!plan.write_slot_a);
    assert(!plan.write_slot_b);

    assert(!settings_record_plan_repair(&slot_a, &slot_b, NULL));
    assert(!settings_record_repair_is_usable(NULL, true));
}

static void test_migration_source_priority(void)
{
    assert(settings_record_select_migration_source(true, false, true, true, true,
                                                   true) ==
           SETTINGS_MIGRATION_SOURCE_CURRENT_RECORD);
    assert(settings_record_select_migration_source(true, false, false, false,
                                                   false, false) ==
           SETTINGS_MIGRATION_SOURCE_CURRENT_RECORD);
    assert(settings_record_select_migration_source(false, false, true, true, true,
                                                   true) ==
           SETTINGS_MIGRATION_SOURCE_SCHEMA5_RECORD);
    assert(settings_record_select_migration_source(false, false, true, false,
                                                   false, false) ==
           SETTINGS_MIGRATION_SOURCE_SCHEMA5_RECORD);
    assert(settings_record_select_migration_source(false, false, false, true, true,
                                                   true) ==
           SETTINGS_MIGRATION_SOURCE_SCHEMA4_RECORD);
    assert(settings_record_select_migration_source(false, false, false, true,
                                                   false, false) ==
           SETTINGS_MIGRATION_SOURCE_SCHEMA4_RECORD);
    assert(settings_record_select_migration_source(false, false, false, false,
                                                   true, true) ==
           SETTINGS_MIGRATION_SOURCE_SCHEMA3_RECORD);
    assert(settings_record_select_migration_source(false, false, false, false,
                                                   true, false) ==
           SETTINGS_MIGRATION_SOURCE_SCHEMA3_RECORD);
    assert(settings_record_select_migration_source(false, false, false, false,
                                                   false, true) ==
           SETTINGS_MIGRATION_SOURCE_SCHEMA2_RECORD);
    assert(settings_record_select_migration_source(false, false, false, false,
                                                   false, false) ==
           SETTINGS_MIGRATION_SOURCE_V1_FIELDS);
}

int main(void)
{
    test_schema6_and_dev1_compatibility();
    test_schema8_migration_and_daily_page_mask();
    test_schema9_migration_and_music_mode();
    test_schema10_migration_and_page_order();
    test_schema11_migration_and_timer_codec();
    test_codec_round_trip_and_layout();
    test_current_manual_saving_round_trip();
    test_schema5_record_migration();
    test_schema4_record_migration();
    test_codec_rejects_invalid_input_and_corruption();
    test_schema2_record_migration();
    test_schema3_record_migration();
    test_generation_selection();
    test_corrupt_new_slot_falls_back();
    test_repair_plan();
    test_migration_source_priority();
    puts("settings record tests passed");
    return 0;
}
