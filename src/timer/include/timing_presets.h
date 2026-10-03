#pragma once

#include <stdint.h>

enum { TIMING_PRESET_CAPACITY = 5U };

/* Four common choices plus at most one saved custom choice. */
static inline unsigned timing_preset_options(
    uint16_t custom, uint16_t options[TIMING_PRESET_CAPACITY])
{
    const uint16_t common[] = {1U, 5U, 10U, 30U};
    unsigned count = 4U;
    for (unsigned i = 0U; i < count; ++i) options[i] = common[i];
    if (custom < 1U || custom > 180U) return count;
    for (unsigned i = 0U; i < count; ++i) {
        if (options[i] == custom) return count;
    }
    options[count++] = custom;
    return count;
}

static inline uint16_t timing_next_preset(uint16_t current, uint16_t custom)
{
    uint16_t options[TIMING_PRESET_CAPACITY];
    const unsigned count = timing_preset_options(custom, options);
    for (unsigned i = 0U; i < count; ++i) {
        if (options[i] == current) return options[(i + 1U) % count];
    }
    return options[0];
}
