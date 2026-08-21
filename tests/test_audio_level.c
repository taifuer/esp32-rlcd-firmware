#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "audio_level.h"

static void test_channel_selection_and_dc_removal(void)
{
    const int16_t samples[] = {
        1100, 7, 2200, 9,
        -900, 7, -1800, 9,
        1100, 7, 2200, 9,
        -900, 7, -1800, 9,
    };
    audio_level_accumulator_t accumulator;
    audio_level_result_t result;
    audio_level_init(&accumulator);
    assert(audio_level_add_tdm16(&accumulator, samples, 4U, 4U, 0U, 2U));
    assert(audio_level_finish(&accumulator, &result));
    assert(result.rms[0] == 1000U);
    assert(result.rms[1] == 2000U);
    assert(result.peak[0] == 1100U);
    assert(result.peak[1] == 2200U);
    assert(result.active[0]);
    assert(result.active[1]);
    assert(result.percent[1] > result.percent[0]);
}

static void test_constant_dc_is_not_activity(void)
{
    const int16_t samples[] = {
        1200, -2400,
        1200, -2400,
        1200, -2400,
        1200, -2400,
    };
    audio_level_accumulator_t accumulator;
    audio_level_result_t result;
    audio_level_init(&accumulator);
    assert(audio_level_add_tdm16(&accumulator, samples, 4U, 2U, 0U, 1U));
    assert(audio_level_finish(&accumulator, &result));
    assert(result.rms[0] == 0U);
    assert(result.rms[1] == 0U);
    assert(result.percent[0] == 0U);
    assert(result.percent[1] == 0U);
    assert(!result.active[0]);
    assert(!result.active[1]);
}

static void test_activity_threshold_and_saturation(void)
{
    const int16_t samples[] = {
        64, 5000,
        -64, -5000,
        64, 5000,
        -64, -5000,
    };
    audio_level_accumulator_t accumulator;
    audio_level_result_t result;
    audio_level_init(&accumulator);
    assert(audio_level_add_tdm16(&accumulator, samples, 4U, 2U, 0U, 1U));
    assert(audio_level_finish(&accumulator, &result));
    assert(result.rms[0] == 64U);
    assert(result.active[0]);
    assert(result.percent[0] >= 1U);
    assert(result.rms[1] == 5000U);
    assert(result.percent[1] == 100U);
}

static void test_minimum_sample_and_invalid_inputs(void)
{
    const int16_t samples[] = {
        INT16_MIN, 0,
        0, 0,
    };
    audio_level_accumulator_t accumulator;
    audio_level_result_t result;
    audio_level_init(&accumulator);
    assert(audio_level_add_tdm16(&accumulator, samples, 2U, 2U, 0U, 1U));
    assert(audio_level_finish(&accumulator, &result));
    assert(result.peak[0] == 32768U);

    audio_level_init(&accumulator);
    assert(!audio_level_finish(&accumulator, &result));
    assert(!audio_level_add_tdm16(NULL, samples, 2U, 2U, 0U, 1U));
    assert(!audio_level_add_tdm16(&accumulator, NULL, 2U, 2U, 0U, 1U));
    assert(!audio_level_add_tdm16(&accumulator, samples, 0U, 2U, 0U, 1U));
    assert(!audio_level_add_tdm16(&accumulator, samples, 2U, 0U, 0U, 0U));
    assert(!audio_level_add_tdm16(&accumulator, samples, 2U, 2U, 2U, 0U));
}

int main(void)
{
    test_channel_selection_and_dc_removal();
    test_constant_dc_is_not_activity();
    test_activity_threshold_and_saturation();
    test_minimum_sample_and_invalid_inputs();
    puts("audio level tests passed");
    return 0;
}
