#include "weather_gzip.h"

#include <limits.h>

enum {
    GZIP_HEADER_LENGTH = 10,
    GZIP_TRAILER_LENGTH = 8,
    GZIP_FLAG_FHCRC = 0x02,
    GZIP_FLAG_FEXTRA = 0x04,
    GZIP_FLAG_FNAME = 0x08,
    GZIP_FLAG_FCOMMENT = 0x10,
    GZIP_FLAG_RESERVED = 0xe0,
};

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool skip_zero_terminated(const uint8_t *data, size_t limit,
                                 size_t *offset)
{
    while (*offset < limit && data[*offset] != 0U) {
        ++*offset;
    }
    if (*offset >= limit) {
        return false;
    }
    ++*offset;
    return true;
}

bool weather_gzip_parse_frame(const uint8_t *data, size_t length,
                              weather_gzip_frame_t *frame)
{
    if (data == NULL || frame == NULL ||
        length < GZIP_HEADER_LENGTH + GZIP_TRAILER_LENGTH ||
        data[0] != 0x1fU || data[1] != 0x8bU || data[2] != 8U ||
        (data[3] & GZIP_FLAG_RESERVED) != 0U) {
        return false;
    }

    const size_t trailer_offset = length - GZIP_TRAILER_LENGTH;
    size_t offset = GZIP_HEADER_LENGTH;
    const uint8_t flags = data[3];
    if ((flags & GZIP_FLAG_FEXTRA) != 0U) {
        if (offset > trailer_offset || trailer_offset - offset < 2U) {
            return false;
        }
        const size_t extra_length = read_u16_le(data + offset);
        offset += 2U;
        if (extra_length > trailer_offset - offset) {
            return false;
        }
        offset += extra_length;
    }
    if ((flags & GZIP_FLAG_FNAME) != 0U &&
        !skip_zero_terminated(data, trailer_offset, &offset)) {
        return false;
    }
    if ((flags & GZIP_FLAG_FCOMMENT) != 0U &&
        !skip_zero_terminated(data, trailer_offset, &offset)) {
        return false;
    }
    if ((flags & GZIP_FLAG_FHCRC) != 0U) {
        if (offset > trailer_offset || trailer_offset - offset < 2U) {
            return false;
        }
        offset += 2U;
    }
    if (offset >= trailer_offset) {
        return false;
    }

    frame->deflate_offset = offset;
    frame->deflate_length = trailer_offset - offset;
    frame->expected_crc32 = read_u32_le(data + trailer_offset);
    frame->expected_size = read_u32_le(data + trailer_offset + 4U);
    return true;
}

uint32_t weather_gzip_crc32(const uint8_t *data, size_t length)
{
    if (data == NULL && length != 0U) {
        return 0U;
    }

    uint32_t crc = UINT32_MAX;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}
