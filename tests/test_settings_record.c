#include <assert.h>
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
    assert(encoded[4] == 1U && encoded[5] == 0U);
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

    settings_record_t decoded = {0};
    assert(settings_record_decode(encoded, sizeof(encoded), &decoded));
    assert(decoded.generation == UINT32_C(0x78563412));
    assert_settings_equal(&decoded.settings, &settings);
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

int main(void)
{
    test_codec_round_trip_and_layout();
    test_codec_rejects_invalid_input_and_corruption();
    test_generation_selection();
    test_corrupt_new_slot_falls_back();
    test_repair_plan();
    puts("settings record tests passed");
    return 0;
}
