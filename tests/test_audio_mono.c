#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "audio_mono.h"

static void test_four_slot_extraction(void)
{
    const int16_t interleaved[] = {
        10, 11, 12, 13,
        -20, -21, -22, -23,
        INT16_MIN, 31, INT16_MAX, 33,
    };
    int16_t mono[3] = {0};
    assert(audio_tdm_extract_mono16(
        interleaved, 3U, 4U, 0U, mono));
    assert(mono[0] == 10);
    assert(mono[1] == -20);
    assert(mono[2] == INT16_MIN);

    assert(audio_tdm_extract_mono16(
        interleaved, 3U, 4U, 2U, mono));
    assert(mono[0] == 12);
    assert(mono[1] == -22);
    assert(mono[2] == INT16_MAX);
}

static void test_invalid_inputs(void)
{
    const int16_t interleaved[] = {1, 2, 3, 4};
    int16_t mono[1] = {0};
    assert(!audio_tdm_extract_mono16(
        NULL, 1U, 4U, 0U, mono));
    assert(!audio_tdm_extract_mono16(
        interleaved, 1U, 4U, 0U, NULL));
    assert(!audio_tdm_extract_mono16(
        interleaved, 0U, 4U, 0U, mono));
    assert(!audio_tdm_extract_mono16(
        interleaved, 1U, 0U, 0U, mono));
    assert(!audio_tdm_extract_mono16(
        interleaved, 1U, 4U, 4U, mono));
    assert(!audio_tdm_extract_mono16(
        interleaved, SIZE_MAX, 4U, 0U, mono));
}

int main(void)
{
    test_four_slot_extraction();
    test_invalid_inputs();
    puts("audio mono extraction tests passed");
    return 0;
}
