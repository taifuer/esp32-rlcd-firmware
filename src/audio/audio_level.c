#include "audio_level.h"

#include <limits.h>
#include <string.h>

enum {
    AUDIO_LEVEL_ACTIVITY_RMS = 64,
    AUDIO_LEVEL_DISPLAY_FLOOR_RMS = 32,
    AUDIO_LEVEL_DISPLAY_FULL_RMS = 4096,
};

static uint32_t absolute_sample(int16_t sample)
{
    return sample == INT16_MIN ? 32768U
                               : (uint32_t)(sample < 0 ? -sample : sample);
}

static uint32_t integer_sqrt(uint64_t value)
{
    uint64_t bit = (uint64_t)1U << 62;
    uint64_t result = 0U;

    while (bit > value) {
        bit >>= 2U;
    }
    while (bit != 0U) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1U) + bit;
        } else {
            result >>= 1U;
        }
        bit >>= 2U;
    }
    return result > UINT32_MAX ? UINT32_MAX : (uint32_t)result;
}

static uint8_t level_percent(uint32_t rms)
{
    if (rms <= AUDIO_LEVEL_DISPLAY_FLOOR_RMS) {
        return 0U;
    }
    if (rms >= AUDIO_LEVEL_DISPLAY_FULL_RMS) {
        return 100U;
    }
    const uint32_t scaled =
        ((rms - AUDIO_LEVEL_DISPLAY_FLOOR_RMS) * 100U) /
        (AUDIO_LEVEL_DISPLAY_FULL_RMS - AUDIO_LEVEL_DISPLAY_FLOOR_RMS);
    return (uint8_t)(scaled == 0U ? 1U : scaled);
}

void audio_level_init(audio_level_accumulator_t *accumulator)
{
    if (accumulator != NULL) {
        memset(accumulator, 0, sizeof(*accumulator));
    }
}

bool audio_level_add_tdm16(audio_level_accumulator_t *accumulator,
                           const int16_t *samples, size_t frame_count,
                           uint8_t channel_count, uint8_t first_channel,
                           uint8_t second_channel)
{
    if (accumulator == NULL || samples == NULL || frame_count == 0U ||
        channel_count == 0U || first_channel >= channel_count ||
        second_channel >= channel_count ||
        frame_count > UINT32_MAX - accumulator->frame_count) {
        return false;
    }

    for (size_t frame = 0; frame < frame_count; ++frame) {
        const int16_t selected[2] = {
            samples[frame * channel_count + first_channel],
            samples[frame * channel_count + second_channel],
        };
        for (size_t microphone = 0; microphone < 2U; ++microphone) {
            const int32_t value = selected[microphone];
            const uint32_t magnitude = absolute_sample(selected[microphone]);
            accumulator->sum[microphone] += value;
            accumulator->square_sum[microphone] +=
                (uint64_t)((int64_t)value * value);
            if (magnitude > accumulator->peak[microphone]) {
                accumulator->peak[microphone] = magnitude;
            }
        }
    }
    accumulator->frame_count += (uint32_t)frame_count;
    return true;
}

bool audio_level_finish(const audio_level_accumulator_t *accumulator,
                        audio_level_result_t *result)
{
    if (accumulator == NULL || result == NULL ||
        accumulator->frame_count == 0U) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    for (size_t microphone = 0; microphone < 2U; ++microphone) {
        const int64_t mean =
            accumulator->sum[microphone] /
            (int64_t)accumulator->frame_count;
        const uint64_t mean_square =
            accumulator->square_sum[microphone] /
            accumulator->frame_count;
        const uint64_t dc_square = (uint64_t)(mean * mean);
        const uint64_t ac_square =
            mean_square > dc_square ? mean_square - dc_square : 0U;
        const uint32_t rms = integer_sqrt(ac_square);
        result->rms[microphone] =
            rms > UINT16_MAX ? UINT16_MAX : (uint16_t)rms;
        result->peak[microphone] =
            accumulator->peak[microphone] > UINT16_MAX
                ? UINT16_MAX
                : (uint16_t)accumulator->peak[microphone];
        result->percent[microphone] = level_percent(rms);
        result->active[microphone] = rms >= AUDIO_LEVEL_ACTIVITY_RMS;
    }
    return true;
}
