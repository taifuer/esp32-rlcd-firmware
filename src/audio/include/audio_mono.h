#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool audio_tdm_extract_mono16(const int16_t *interleaved,
                              size_t frame_count,
                              uint8_t channel_count,
                              uint8_t selected_channel,
                              int16_t *mono);

#ifdef __cplusplus
}
#endif
