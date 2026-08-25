#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "settings_model.h"
#include "settings_record.h"

static app_settings_t make_settings(uint8_t volume,
                                    app_update_channel_t channel)
{
    app_settings_t settings;
    app_settings_defaults(&settings);
    settings.power_mode = APP_POWER_MODE_SAVING;
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
    assert(actual->power_mode == expected->power_mode);
    assert(actual->utc_offset_minutes == expected->utc_offset_minutes);
    assert(actual->temperature_unit == expected->temperature_unit);
    assert(actual->audio_playback_volume ==
           expected->audio_playback_volume);
    assert(actual->update_channel == expected->update_channel);
    assert(actual->alarm_enabled == expected->alarm_enabled);
    assert(actual->alarm_hour == expected->alarm_hour);
    assert(actual->alarm_minute == expected->alarm_minute);
    assert(actual->alarm_weekdays == expected->alarm_weekdays);
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
    assert(encoded[4] == 3U && encoded[5] == 0U);
    assert(encoded[6] == SETTINGS_RECORD_ENCODED_SIZE &&
           encoded[7] == 0U);
    assert(encoded[8] == 0x12U && encoded[9] == 0x34U &&
           encoded[10] == 0x56U && encoded[11] == 0x78U);
    assert(encoded[12] == APP_SETTINGS_SCHEMA_VERSION &&
           encoded[13] == 0U);
    assert(encoded[14] == APP_POWER_MODE_SAVING);
    assert(encoded[15] == APP_TEMPERATURE_UNIT_FAHRENHEIT);
    assert(encoded[16] == 0xd4U && encoded[17] == 0xfeU);
    assert(encoded[18] == 73U);
    assert(encoded[19] == APP_UPDATE_CHANNEL_BETA);
    assert(encoded[20] == 1U);
    assert(encoded[21] == 6U);
    assert(encoded[22] == 45U);
    assert(encoded[23] == APP_SETTINGS_ALARM_WEEKENDS_MASK);
    assert(encoded[24] == 0U);

    settings_record_t decoded = {0};
    assert(settings_record_decode(encoded, sizeof(encoded), &decoded));
    assert(decoded.generation == UINT32_C(0x78563412));
    assert_settings_equal(&decoded.settings, &settings);
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

static void test_dev3_schema4_obsolete_image_mode_migration(void)
{
    const app_settings_t expected =
        make_settings(73U, APP_UPDATE_CHANNEL_BETA);

    for (uint8_t legacy_rotation = 0U; legacy_rotation <= 2U;
         ++legacy_rotation) {
        uint8_t encoded[SETTINGS_RECORD_ENCODED_SIZE];
        assert(settings_record_encode(UINT32_C(0x12345678), &expected,
                                      encoded, sizeof(encoded)));

        /* v0.15.0-dev.3 used byte 24 for obsolete mode values 0..2. */
        encoded[24] = legacy_rotation;
        put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));

        settings_record_t decoded = {0};
        assert(settings_record_decode(encoded, sizeof(encoded), &decoded));
        assert(decoded.generation == UINT32_C(0x12345678));
        assert_settings_equal(&decoded.settings, &expected);

        uint8_t canonical[SETTINGS_RECORD_ENCODED_SIZE];
        assert(settings_record_encode(decoded.generation,
                                      &decoded.settings, canonical,
                                      sizeof(canonical)));
        assert(canonical[24] == 0U);
    }
}

static void test_schema2_record_migration(void)
{
    uint8_t encoded[SETTINGS_RECORD_SCHEMA2_ENCODED_SIZE] = {
        'R', 'C', 'F', 'G',
        1U, 0U,
        SETTINGS_RECORD_SCHEMA2_ENCODED_SIZE, 0U,
        0x12U, 0x34U, 0x56U, 0x78U,
        2U, 0U,
        APP_POWER_MODE_SAVING,
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
    assert(decoded.settings.power_mode == APP_POWER_MODE_SAVING);
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
        APP_POWER_MODE_SAVING,
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
    assert(decoded.settings.alarm_enabled);
    assert(decoded.settings.alarm_hour == 6U);
    assert(decoded.settings.alarm_minute == 45U);
    assert(decoded.settings.alarm_weekdays ==
           APP_SETTINGS_ALARM_WEEKENDS_MASK);

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
    put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));

    assert(settings_record_encode(7U, &settings, encoded,
                                  sizeof(encoded)));
    encoded[21] = 24U;
    put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));

    assert(settings_record_encode(7U, &settings, encoded,
                                  sizeof(encoded)));
    encoded[23] = 0U;
    put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
    assert(!settings_record_decode(encoded, sizeof(encoded), &decoded));

    assert(settings_record_encode(7U, &settings, encoded,
                                  sizeof(encoded)));
    encoded[24] = 3U;
    put_legacy_u32(encoded, 28U, legacy_checksum(encoded, 28U));
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
    assert(settings_record_select_migration_source(true, true, true) ==
           SETTINGS_MIGRATION_SOURCE_CURRENT_RECORD);
    assert(settings_record_select_migration_source(true, false, false) ==
           SETTINGS_MIGRATION_SOURCE_CURRENT_RECORD);
    assert(settings_record_select_migration_source(false, true, true) ==
           SETTINGS_MIGRATION_SOURCE_SCHEMA3_RECORD);
    assert(settings_record_select_migration_source(false, true, false) ==
           SETTINGS_MIGRATION_SOURCE_SCHEMA3_RECORD);
    assert(settings_record_select_migration_source(false, false, true) ==
           SETTINGS_MIGRATION_SOURCE_SCHEMA2_RECORD);
    assert(settings_record_select_migration_source(false, false, false) ==
           SETTINGS_MIGRATION_SOURCE_V1_FIELDS);
}

int main(void)
{
    test_codec_round_trip_and_layout();
    test_dev3_schema4_obsolete_image_mode_migration();
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
