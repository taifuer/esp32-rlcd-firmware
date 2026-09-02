#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "weather_gzip.h"

static void write_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

int main(void)
{
    static const uint8_t text[] = "123456789";
    assert(weather_gzip_crc32(text, sizeof(text) - 1U) == 0xcbf43926U);
    assert(weather_gzip_crc32(NULL, 0U) == 0U);
    assert(weather_gzip_crc32(NULL, 1U) == 0U);

    uint8_t frame_data[40] = {
        0x1fU, 0x8bU, 8U,
        0x1eU, /* FHCRC | FEXTRA | FNAME | FCOMMENT */
        0U, 0U, 0U, 0U, 0U, 0U,
        2U, 0U, 0xaaU, 0xbbU,
        'a', '.', 'j', 's', 'o', 'n', 0U,
        'q', 'w', 0U,
        0U, 0U,
        0x03U, 0x00U,
    };
    const size_t trailer = 28U;
    write_u32_le(frame_data + trailer, 0x11223344U);
    write_u32_le(frame_data + trailer + 4U, 0U);

    weather_gzip_frame_t frame = {0};
    assert(weather_gzip_parse_frame(frame_data, trailer + 8U, &frame));
    assert(frame.deflate_offset == 26U);
    assert(frame.deflate_length == 2U);
    assert(frame.expected_crc32 == 0x11223344U);
    assert(frame.expected_size == 0U);

    uint8_t invalid[sizeof(frame_data)];
    memcpy(invalid, frame_data, sizeof(invalid));
    invalid[0] = 0U;
    assert(!weather_gzip_parse_frame(invalid, trailer + 8U, &frame));
    memcpy(invalid, frame_data, sizeof(invalid));
    invalid[3] |= 0x20U;
    assert(!weather_gzip_parse_frame(invalid, trailer + 8U, &frame));
    memcpy(invalid, frame_data, sizeof(invalid));
    invalid[10] = 0xffU;
    invalid[11] = 0xffU;
    assert(!weather_gzip_parse_frame(invalid, trailer + 8U, &frame));
    assert(!weather_gzip_parse_frame(NULL, trailer + 8U, &frame));
    assert(!weather_gzip_parse_frame(frame_data, 10U, &frame));
    assert(!weather_gzip_parse_frame(frame_data, trailer + 8U, NULL));

    puts("weather gzip tests passed");
    return 0;
}
