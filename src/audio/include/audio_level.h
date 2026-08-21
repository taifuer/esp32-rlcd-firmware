#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t sum[2];
    uint64_t square_sum[2];
    uint32_t peak[2];
    uint32_t frame_count;
} audio_level_accumulator_t;

typedef struct {
    uint16_t rms[2];
    uint16_t peak[2];
    uint8_t percent[2];
    bool active[2];
} audio_level_result_t;

void audio_level_init(audio_level_accumulator_t *accumulator);
bool audio_level_add_tdm16(audio_level_accumulator_t *accumulator,
                           const int16_t *samples, size_t frame_count,
                           uint8_t channel_count, uint8_t first_channel,
                           uint8_t second_channel);
bool audio_level_finish(const audio_level_accumulator_t *accumulator,
                        audio_level_result_t *result);

#ifdef __cplusplus
}
#endif
