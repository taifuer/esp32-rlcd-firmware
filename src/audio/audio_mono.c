#include "audio_mono.h"

#include <stdint.h>

bool audio_tdm_extract_mono16(const int16_t *interleaved,
                              size_t frame_count,
                              uint8_t channel_count,
                              uint8_t selected_channel,
                              int16_t *mono)
{
    if (interleaved == NULL || mono == NULL || frame_count == 0U ||
        channel_count == 0U || selected_channel >= channel_count ||
        frame_count > SIZE_MAX / channel_count) {
        return false;
    }

    for (size_t frame = 0U; frame < frame_count; ++frame) {
        mono[frame] = interleaved[
            frame * channel_count + selected_channel];
    }
    return true;
}
