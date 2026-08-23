#include "monochrome_image.h"

#include <limits.h>
#include <string.h>

enum {
    BMP_FILE_HEADER_BYTES = 14,
    BMP_DIB_HEADER_BYTES = 40,
    BMP_PALETTE_BYTES = 8,
    BMP_PIXEL_OFFSET = BMP_FILE_HEADER_BYTES + BMP_DIB_HEADER_BYTES +
                       BMP_PALETTE_BYTES,
    BMP_ROW_BYTES = 52,
    BMP_PIXEL_BYTES = BMP_ROW_BYTES * MONO_IMAGE_HEIGHT,
    BMP_FILE_BYTES = BMP_PIXEL_OFFSET + BMP_PIXEL_BYTES,
};

typedef struct {
    mono_image_format_t format;
    size_t pixel_offset;
    bool source_top_down;
    bool bmp_palette_zero_is_white;
} image_layout_t;

_Static_assert(MONO_IMAGE_ROW_BYTES * MONO_IMAGE_HEIGHT ==
                   MONO_IMAGE_BITMAP_BYTES,
               "monochrome bitmap size does not match its dimensions");
_Static_assert(BMP_FILE_BYTES <= MONO_IMAGE_MAX_FILE_BYTES,
               "supported BMP does not fit within the file limit");

static bool format_is_valid(mono_image_format_t format)
{
    return format == MONO_IMAGE_FORMAT_PBM_P4 ||
           format == MONO_IMAGE_FORMAT_BMP_1BPP;
}

static bool ascii_is_space(uint8_t character)
{
    return character == ' ' || character == '\t' || character == '\r' ||
           character == '\n' || character == '\v' || character == '\f';
}

static uint16_t get_u16_le(const uint8_t *data, size_t offset)
{
    return (uint16_t)((uint16_t)data[offset] |
                      ((uint16_t)data[offset + 1U] << 8U));
}

static uint32_t get_u32_le(const uint8_t *data, size_t offset)
{
    return (uint32_t)data[offset] |
           ((uint32_t)data[offset + 1U] << 8U) |
           ((uint32_t)data[offset + 2U] << 16U) |
           ((uint32_t)data[offset + 3U] << 24U);
}

static mono_image_result_t identify_format(
    const uint8_t *source,
    size_t source_size,
    mono_image_format_t *format)
{
    if (source_size < 2U) {
        return MONO_IMAGE_TRUNCATED;
    }
    if (source[0] == 'P' && source[1] == '4') {
        *format = MONO_IMAGE_FORMAT_PBM_P4;
        return MONO_IMAGE_OK;
    }
    if (source[0] == 'B' && source[1] == 'M') {
        *format = MONO_IMAGE_FORMAT_BMP_1BPP;
        return MONO_IMAGE_OK;
    }
    if (source[0] == 'P') {
        return MONO_IMAGE_UNSUPPORTED_ENCODING;
    }
    return MONO_IMAGE_UNSUPPORTED_FORMAT;
}

static mono_image_result_t pbm_skip_separators(
    const uint8_t *source,
    size_t source_size,
    size_t *position)
{
    bool consumed = false;

    while (*position < source_size) {
        if (*position >= MONO_IMAGE_MAX_PBM_HEADER_BYTES) {
            return MONO_IMAGE_INVALID_HEADER;
        }
        if (ascii_is_space(source[*position])) {
            consumed = true;
            ++*position;
            continue;
        }
        if (source[*position] != '#') {
            break;
        }
        /* A comment is only accepted after an actual token separator. */
        if (!consumed) {
            return MONO_IMAGE_INVALID_HEADER;
        }
        while (*position < source_size &&
               source[*position] != '\r' && source[*position] != '\n') {
            if (*position >= MONO_IMAGE_MAX_PBM_HEADER_BYTES) {
                return MONO_IMAGE_INVALID_HEADER;
            }
            ++*position;
        }
        if (*position >= source_size) {
            return MONO_IMAGE_TRUNCATED;
        }
        if (*position >= MONO_IMAGE_MAX_PBM_HEADER_BYTES) {
            return MONO_IMAGE_INVALID_HEADER;
        }
        if (source[*position] == '\r') {
            ++*position;
            if (*position < source_size && source[*position] == '\n') {
                if (*position >= MONO_IMAGE_MAX_PBM_HEADER_BYTES) {
                    return MONO_IMAGE_INVALID_HEADER;
                }
                ++*position;
            }
        } else {
            ++*position;
        }
    }
    return consumed ? MONO_IMAGE_OK : MONO_IMAGE_INVALID_HEADER;
}

static mono_image_result_t pbm_parse_dimension(
    const uint8_t *source,
    size_t source_size,
    size_t *position,
    uint32_t *value)
{
    uint32_t parsed = 0U;
    bool has_digit = false;

    while (*position < source_size) {
        const uint8_t character = source[*position];

        if (character < '0' || character > '9') {
            break;
        }
        if (*position >= MONO_IMAGE_MAX_PBM_HEADER_BYTES) {
            return MONO_IMAGE_INVALID_HEADER;
        }
        const uint32_t digit = (uint32_t)(character - '0');
        if (parsed > (UINT32_MAX - digit) / 10U) {
            return MONO_IMAGE_INVALID_HEADER;
        }
        parsed = parsed * 10U + digit;
        has_digit = true;
        ++*position;
    }
    if (!has_digit) {
        return *position >= source_size ? MONO_IMAGE_TRUNCATED
                                        : MONO_IMAGE_INVALID_HEADER;
    }
    *value = parsed;
    return MONO_IMAGE_OK;
}

static mono_image_result_t parse_pbm(
    const uint8_t *source,
    size_t source_size,
    image_layout_t *layout)
{
    size_t position = 2U;
    uint32_t width = 0U;
    uint32_t height = 0U;
    mono_image_result_t result = pbm_skip_separators(
        source, source_size, &position);

    if (result != MONO_IMAGE_OK) {
        return result;
    }
    result = pbm_parse_dimension(source, source_size, &position, &width);
    if (result != MONO_IMAGE_OK) {
        return result;
    }
    result = pbm_skip_separators(source, source_size, &position);
    if (result != MONO_IMAGE_OK) {
        return result;
    }
    result = pbm_parse_dimension(source, source_size, &position, &height);
    if (result != MONO_IMAGE_OK) {
        return result;
    }
    if (width != MONO_IMAGE_WIDTH || height != MONO_IMAGE_HEIGHT) {
        return MONO_IMAGE_WRONG_DIMENSIONS;
    }
    if (position >= source_size) {
        return MONO_IMAGE_TRUNCATED;
    }
    if (position >= MONO_IMAGE_MAX_PBM_HEADER_BYTES ||
        !ascii_is_space(source[position])) {
        return MONO_IMAGE_INVALID_HEADER;
    }

    /* Do not skip arbitrary whitespace here: the next byte is image data. */
    if (source[position] == '\r' && position + 1U < source_size &&
        source[position + 1U] == '\n') {
        position += 2U;
    } else {
        ++position;
    }
    if (position > MONO_IMAGE_MAX_PBM_HEADER_BYTES) {
        return MONO_IMAGE_INVALID_HEADER;
    }
    if (source_size - position < MONO_IMAGE_BITMAP_BYTES) {
        return MONO_IMAGE_TRUNCATED;
    }
    if (source_size - position > MONO_IMAGE_BITMAP_BYTES) {
        return MONO_IMAGE_TRAILING_DATA;
    }

    *layout = (image_layout_t){
        .format = MONO_IMAGE_FORMAT_PBM_P4,
        .pixel_offset = position,
        .source_top_down = true,
    };
    return MONO_IMAGE_OK;
}

static bool bmp_palette_entry_is(
    const uint8_t *source,
    size_t offset,
    uint8_t component)
{
    return source[offset] == component &&
           source[offset + 1U] == component &&
           source[offset + 2U] == component &&
           source[offset + 3U] == 0U;
}

static mono_image_result_t parse_bmp(
    const uint8_t *source,
    size_t source_size,
    image_layout_t *layout)
{
    if (source_size < BMP_PIXEL_OFFSET) {
        return MONO_IMAGE_TRUNCATED;
    }

    const uint32_t declared_file_size = get_u32_le(source, 2U);
    const uint32_t pixel_offset = get_u32_le(source, 10U);
    const uint32_t dib_size = get_u32_le(source, 14U);
    const uint32_t width = get_u32_le(source, 18U);
    const uint32_t raw_height = get_u32_le(source, 22U);
    const uint16_t planes = get_u16_le(source, 26U);
    const uint16_t bits_per_pixel = get_u16_le(source, 28U);
    const uint32_t compression = get_u32_le(source, 30U);
    const uint32_t image_size = get_u32_le(source, 34U);
    const uint32_t colors_used = get_u32_le(source, 46U);
    const uint32_t important_colors = get_u32_le(source, 50U);

    if (get_u16_le(source, 6U) != 0U || get_u16_le(source, 8U) != 0U ||
        declared_file_size != BMP_FILE_BYTES ||
        pixel_offset != BMP_PIXEL_OFFSET || dib_size != BMP_DIB_HEADER_BYTES) {
        return MONO_IMAGE_INVALID_HEADER;
    }
    if (width != MONO_IMAGE_WIDTH ||
        (raw_height != MONO_IMAGE_HEIGHT &&
         raw_height != UINT32_C(0xfffffed4))) {
        return MONO_IMAGE_WRONG_DIMENSIONS;
    }
    if (planes != 1U || bits_per_pixel != 1U || compression != 0U ||
        (image_size != 0U && image_size != BMP_PIXEL_BYTES) ||
        (colors_used != 0U && colors_used != 2U) ||
        (important_colors != 0U && important_colors != 2U)) {
        return MONO_IMAGE_UNSUPPORTED_ENCODING;
    }

    const bool palette_zero_is_black =
        bmp_palette_entry_is(source, 54U, 0U);
    const bool palette_zero_is_white =
        bmp_palette_entry_is(source, 54U, UINT8_MAX);
    const bool palette_one_is_black =
        bmp_palette_entry_is(source, 58U, 0U);
    const bool palette_one_is_white =
        bmp_palette_entry_is(source, 58U, UINT8_MAX);
    if (!((palette_zero_is_black && palette_one_is_white) ||
          (palette_zero_is_white && palette_one_is_black))) {
        return MONO_IMAGE_INVALID_PALETTE;
    }
    if (source_size < BMP_FILE_BYTES) {
        return MONO_IMAGE_TRUNCATED;
    }
    if (source_size > BMP_FILE_BYTES) {
        return MONO_IMAGE_TRAILING_DATA;
    }

    *layout = (image_layout_t){
        .format = MONO_IMAGE_FORMAT_BMP_1BPP,
        .pixel_offset = BMP_PIXEL_OFFSET,
        .source_top_down = raw_height == UINT32_C(0xfffffed4),
        .bmp_palette_zero_is_white = palette_zero_is_white,
    };
    return MONO_IMAGE_OK;
}

static void decode_pbm(
    const uint8_t *source,
    const image_layout_t *layout,
    uint8_t *bitmap)
{
    for (size_t offset = 0U; offset < MONO_IMAGE_BITMAP_BYTES; ++offset) {
        bitmap[offset] = (uint8_t)~source[layout->pixel_offset + offset];
    }
}

static void decode_bmp(
    const uint8_t *source,
    const image_layout_t *layout,
    uint8_t *bitmap)
{
    for (size_t file_row = 0U; file_row < MONO_IMAGE_HEIGHT; ++file_row) {
        const size_t output_row = layout->source_top_down
                                      ? file_row
                                      : MONO_IMAGE_HEIGHT - 1U - file_row;
        const uint8_t *input = source + layout->pixel_offset +
                               file_row * BMP_ROW_BYTES;
        uint8_t *output = bitmap + output_row * MONO_IMAGE_ROW_BYTES;

        for (size_t column = 0U; column < MONO_IMAGE_ROW_BYTES; ++column) {
            output[column] = layout->bmp_palette_zero_is_white
                                 ? (uint8_t)~input[column]
                                 : input[column];
        }
    }
}

mono_image_result_t mono_image_decode(
    const uint8_t *source,
    size_t source_size,
    mono_image_format_t expected_format,
    uint8_t *bitmap,
    size_t bitmap_size,
    mono_image_info_t *info)
{
    image_layout_t layout = {0};
    mono_image_format_t actual_format = MONO_IMAGE_FORMAT_PBM_P4;
    mono_image_result_t result;

    if (info != NULL) {
        memset(info, 0, sizeof(*info));
    }
    if (source == NULL || bitmap == NULL || !format_is_valid(expected_format)) {
        return MONO_IMAGE_INVALID_ARGUMENT;
    }
    if (bitmap_size < MONO_IMAGE_BITMAP_BYTES) {
        return MONO_IMAGE_OUTPUT_TOO_SMALL;
    }
    if (source_size == 0U) {
        return MONO_IMAGE_EMPTY;
    }
    if (source_size > MONO_IMAGE_MAX_FILE_BYTES) {
        return MONO_IMAGE_TOO_LARGE;
    }

    result = identify_format(source, source_size, &actual_format);
    if (result != MONO_IMAGE_OK) {
        return result;
    }
    if (actual_format != expected_format) {
        return MONO_IMAGE_FORMAT_MISMATCH;
    }

    result = actual_format == MONO_IMAGE_FORMAT_PBM_P4
                 ? parse_pbm(source, source_size, &layout)
                 : parse_bmp(source, source_size, &layout);
    if (result != MONO_IMAGE_OK) {
        return result;
    }

    if (layout.format == MONO_IMAGE_FORMAT_PBM_P4) {
        decode_pbm(source, &layout, bitmap);
    } else {
        decode_bmp(source, &layout, bitmap);
    }
    if (info != NULL) {
        *info = (mono_image_info_t){
            .format = layout.format,
            .source_top_down = layout.source_top_down,
        };
    }
    return MONO_IMAGE_OK;
}

const char *mono_image_result_name(mono_image_result_t result)
{
    switch (result) {
    case MONO_IMAGE_OK:
        return "ok";
    case MONO_IMAGE_INVALID_ARGUMENT:
        return "invalid argument";
    case MONO_IMAGE_EMPTY:
        return "empty";
    case MONO_IMAGE_TOO_LARGE:
        return "too large";
    case MONO_IMAGE_UNSUPPORTED_FORMAT:
        return "unsupported format";
    case MONO_IMAGE_FORMAT_MISMATCH:
        return "format mismatch";
    case MONO_IMAGE_INVALID_HEADER:
        return "invalid header";
    case MONO_IMAGE_WRONG_DIMENSIONS:
        return "wrong dimensions";
    case MONO_IMAGE_UNSUPPORTED_ENCODING:
        return "unsupported encoding";
    case MONO_IMAGE_INVALID_PALETTE:
        return "invalid palette";
    case MONO_IMAGE_TRUNCATED:
        return "truncated";
    case MONO_IMAGE_TRAILING_DATA:
        return "trailing data";
    case MONO_IMAGE_OUTPUT_TOO_SMALL:
        return "output too small";
    default:
        return "unknown";
    }
}
