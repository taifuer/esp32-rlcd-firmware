#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "monochrome_image.h"

enum {
    TEST_BMP_PIXEL_OFFSET = 62,
    TEST_BMP_ROW_BYTES = 52,
    TEST_BMP_PIXEL_BYTES = TEST_BMP_ROW_BYTES * MONO_IMAGE_HEIGHT,
    TEST_BMP_FILE_BYTES = TEST_BMP_PIXEL_OFFSET + TEST_BMP_PIXEL_BYTES,
};

static uint8_t source_buffer[MONO_IMAGE_MAX_FILE_BYTES + 1U];
static uint8_t bitmap_buffer[MONO_IMAGE_BITMAP_BYTES];
static uint8_t expected_bitmap[MONO_IMAGE_BITMAP_BYTES];
static uint8_t bitmap_snapshot[MONO_IMAGE_BITMAP_BYTES];

static void put_u16_le(uint8_t *data, size_t offset, uint16_t value)
{
    data[offset] = (uint8_t)value;
    data[offset + 1U] = (uint8_t)(value >> 8U);
}

static void put_u32_le(uint8_t *data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t)value;
    data[offset + 1U] = (uint8_t)(value >> 8U);
    data[offset + 2U] = (uint8_t)(value >> 16U);
    data[offset + 3U] = (uint8_t)(value >> 24U);
}

static size_t build_pbm(const char *header)
{
    const size_t header_size = strlen(header);

    assert(header_size + MONO_IMAGE_BITMAP_BYTES <= sizeof(source_buffer));
    memcpy(source_buffer, header, header_size);
    for (size_t offset = 0U; offset < MONO_IMAGE_BITMAP_BYTES; ++offset) {
        source_buffer[header_size + offset] =
            (uint8_t)((offset * 37U + 11U) & 0xffU);
    }
    return header_size + MONO_IMAGE_BITMAP_BYTES;
}

static void expect_error(
    const uint8_t *source,
    size_t source_size,
    mono_image_format_t format,
    mono_image_result_t expected_result)
{
    mono_image_info_t info;

    memset(bitmap_buffer, 0xa5, sizeof(bitmap_buffer));
    memcpy(bitmap_snapshot, bitmap_buffer, sizeof(bitmap_snapshot));
    memset(&info, 0xa5, sizeof(info));
    assert(mono_image_decode(source, source_size, format, bitmap_buffer,
                             sizeof(bitmap_buffer), &info) ==
           expected_result);
    assert(memcmp(bitmap_buffer, bitmap_snapshot, sizeof(bitmap_buffer)) == 0);
    assert(info.format == MONO_IMAGE_FORMAT_PBM_P4);
    assert(!info.source_top_down);
}

static void expect_any_error_without_output(
    const uint8_t *source,
    size_t source_size,
    mono_image_format_t format)
{
    mono_image_info_t info;

    memset(bitmap_buffer, 0x3c, sizeof(bitmap_buffer));
    memcpy(bitmap_snapshot, bitmap_buffer, sizeof(bitmap_snapshot));
    memset(&info, 0xa5, sizeof(info));
    assert(mono_image_decode(source, source_size, format, bitmap_buffer,
                             sizeof(bitmap_buffer), &info) != MONO_IMAGE_OK);
    assert(memcmp(bitmap_buffer, bitmap_snapshot, sizeof(bitmap_buffer)) == 0);
    assert(info.format == MONO_IMAGE_FORMAT_PBM_P4);
    assert(!info.source_top_down);
}

static void test_pbm_valid_and_polarity(void)
{
    static const char canonical_header[] = "P4\n400 300\n";
    const size_t source_size = build_pbm(canonical_header);
    const size_t header_size = sizeof(canonical_header) - 1U;
    mono_image_info_t info = {0};

    for (size_t offset = 0U; offset < MONO_IMAGE_BITMAP_BYTES; ++offset) {
        expected_bitmap[offset] =
            (uint8_t)~source_buffer[header_size + offset];
    }
    assert(mono_image_decode(
               source_buffer, source_size, MONO_IMAGE_FORMAT_PBM_P4,
               bitmap_buffer, sizeof(bitmap_buffer), &info) == MONO_IMAGE_OK);
    assert(info.format == MONO_IMAGE_FORMAT_PBM_P4);
    assert(info.source_top_down);
    assert(memcmp(bitmap_buffer, expected_bitmap, sizeof(bitmap_buffer)) == 0);

    /* Raster bytes which look like header whitespace or comments are data. */
    source_buffer[header_size] = '\n';
    source_buffer[header_size + 1U] = '#';
    assert(mono_image_decode(
               source_buffer, source_size, MONO_IMAGE_FORMAT_PBM_P4,
               bitmap_buffer, sizeof(bitmap_buffer), NULL) == MONO_IMAGE_OK);
    assert(bitmap_buffer[0] == (uint8_t)~(uint8_t)'\n');
    assert(bitmap_buffer[1] == (uint8_t)~(uint8_t)'#');
}

static void test_pbm_header_variants(void)
{
    static const char commented_header[] =
        "P4 \n# generated image\r\n\t00400 \n# height follows\n00300\r\n";
    const size_t source_size = build_pbm(commented_header);
    mono_image_info_t info = {0};

    assert(mono_image_decode(
               source_buffer, source_size, MONO_IMAGE_FORMAT_PBM_P4,
               bitmap_buffer, sizeof(bitmap_buffer), &info) == MONO_IMAGE_OK);
    assert(info.source_top_down);

    size_t malformed_size = build_pbm("P4\n399 300\n");
    expect_error(source_buffer, malformed_size, MONO_IMAGE_FORMAT_PBM_P4,
                 MONO_IMAGE_WRONG_DIMENSIONS);
    malformed_size = build_pbm("P4\n400 301\n");
    expect_error(source_buffer, malformed_size, MONO_IMAGE_FORMAT_PBM_P4,
                 MONO_IMAGE_WRONG_DIMENSIONS);
    malformed_size = build_pbm("P4400 300\n");
    expect_error(source_buffer, malformed_size, MONO_IMAGE_FORMAT_PBM_P4,
                 MONO_IMAGE_INVALID_HEADER);
    malformed_size = build_pbm("P4\n+400 300\n");
    expect_error(source_buffer, malformed_size, MONO_IMAGE_FORMAT_PBM_P4,
                 MONO_IMAGE_INVALID_HEADER);
    malformed_size = build_pbm("P4\n400 -300\n");
    expect_error(source_buffer, malformed_size, MONO_IMAGE_FORMAT_PBM_P4,
                 MONO_IMAGE_INVALID_HEADER);
    malformed_size = build_pbm("P4\n42949672960 300\n");
    expect_error(source_buffer, malformed_size, MONO_IMAGE_FORMAT_PBM_P4,
                 MONO_IMAGE_INVALID_HEADER);

    memset(source_buffer, ' ', sizeof(source_buffer));
    source_buffer[0] = 'P';
    source_buffer[1] = '4';
    expect_error(source_buffer, MONO_IMAGE_MAX_PBM_HEADER_BYTES + 1U,
                 MONO_IMAGE_FORMAT_PBM_P4, MONO_IMAGE_INVALID_HEADER);

    memcpy(source_buffer, "P4\n# unfinished", 15U);
    expect_error(source_buffer, 15U, MONO_IMAGE_FORMAT_PBM_P4,
                 MONO_IMAGE_TRUNCATED);
    memcpy(source_buffer, "P1\n400 300\n", 11U);
    expect_error(source_buffer, 11U, MONO_IMAGE_FORMAT_PBM_P4,
                 MONO_IMAGE_UNSUPPORTED_ENCODING);
}

static void test_pbm_size_bounds(void)
{
    const size_t source_size = build_pbm("P4\n400 300\n");

    for (size_t truncated_size = 0U; truncated_size < source_size;
         ++truncated_size) {
        expect_any_error_without_output(
            source_buffer, truncated_size, MONO_IMAGE_FORMAT_PBM_P4);
    }
    source_buffer[source_size] = 0U;
    expect_error(source_buffer, source_size + 1U,
                 MONO_IMAGE_FORMAT_PBM_P4, MONO_IMAGE_TRAILING_DATA);
}

static void set_bmp_palette_entry(
    uint8_t *data,
    size_t offset,
    uint8_t component)
{
    data[offset] = component;
    data[offset + 1U] = component;
    data[offset + 2U] = component;
    data[offset + 3U] = 0U;
}

static void set_bmp_index(
    uint8_t *data,
    bool top_down,
    size_t x,
    size_t y,
    bool index_one)
{
    assert(x < MONO_IMAGE_WIDTH);
    assert(y < MONO_IMAGE_HEIGHT);
    const size_t file_row = top_down ? y : MONO_IMAGE_HEIGHT - 1U - y;
    const size_t offset = TEST_BMP_PIXEL_OFFSET +
                          file_row * TEST_BMP_ROW_BYTES + x / 8U;
    const uint8_t mask = (uint8_t)(UINT8_C(0x80) >> (x % 8U));

    if (index_one) {
        data[offset] |= mask;
    } else {
        data[offset] &= (uint8_t)~mask;
    }
}

static size_t build_bmp(bool top_down, bool palette_zero_is_white)
{
    const bool black_index_is_one = palette_zero_is_white;
    const bool white_index_is_one = !palette_zero_is_white;

    memset(source_buffer, 0, TEST_BMP_FILE_BYTES);
    source_buffer[0] = 'B';
    source_buffer[1] = 'M';
    put_u32_le(source_buffer, 2U, TEST_BMP_FILE_BYTES);
    put_u32_le(source_buffer, 10U, TEST_BMP_PIXEL_OFFSET);
    put_u32_le(source_buffer, 14U, 40U);
    put_u32_le(source_buffer, 18U, MONO_IMAGE_WIDTH);
    put_u32_le(source_buffer, 22U,
               top_down ? UINT32_C(0xfffffed4) : MONO_IMAGE_HEIGHT);
    put_u16_le(source_buffer, 26U, 1U);
    put_u16_le(source_buffer, 28U, 1U);
    put_u32_le(source_buffer, 30U, 0U);
    put_u32_le(source_buffer, 34U, TEST_BMP_PIXEL_BYTES);
    put_u32_le(source_buffer, 46U, 2U);
    put_u32_le(source_buffer, 50U, 2U);
    set_bmp_palette_entry(source_buffer, 54U,
                          palette_zero_is_white ? UINT8_MAX : 0U);
    set_bmp_palette_entry(source_buffer, 58U,
                          palette_zero_is_white ? 0U : UINT8_MAX);

    for (size_t file_row = 0U; file_row < MONO_IMAGE_HEIGHT; ++file_row) {
        uint8_t *row = source_buffer + TEST_BMP_PIXEL_OFFSET +
                       file_row * TEST_BMP_ROW_BYTES;
        memset(row, black_index_is_one ? UINT8_MAX : 0U,
               MONO_IMAGE_ROW_BYTES);
        row[MONO_IMAGE_ROW_BYTES] = 0xa5U;
        row[MONO_IMAGE_ROW_BYTES + 1U] = 0x5aU;
    }

    set_bmp_index(source_buffer, top_down, 0U, 0U, white_index_is_one);
    set_bmp_index(source_buffer, top_down, 399U, 0U, white_index_is_one);
    set_bmp_index(source_buffer, top_down, 200U, 123U, white_index_is_one);
    set_bmp_index(source_buffer, top_down, 8U, 299U, white_index_is_one);
    return TEST_BMP_FILE_BYTES;
}

static void build_expected_bmp(void)
{
    memset(expected_bitmap, 0, sizeof(expected_bitmap));
    expected_bitmap[0] |= 0x80U;
    expected_bitmap[49] |= 0x01U;
    expected_bitmap[123U * MONO_IMAGE_ROW_BYTES + 25U] |= 0x80U;
    expected_bitmap[299U * MONO_IMAGE_ROW_BYTES + 1U] |= 0x80U;
}

static void test_bmp_valid_orientation_and_palette(void)
{
    build_expected_bmp();
    for (unsigned int top_down_value = 0U; top_down_value <= 1U;
         ++top_down_value) {
        for (unsigned int palette_value = 0U; palette_value <= 1U;
             ++palette_value) {
            const bool top_down = top_down_value != 0U;
            const bool palette_zero_is_white = palette_value != 0U;
            const size_t source_size =
                build_bmp(top_down, palette_zero_is_white);
            mono_image_info_t info = {0};

            assert(mono_image_decode(
                       source_buffer, source_size,
                       MONO_IMAGE_FORMAT_BMP_1BPP, bitmap_buffer,
                       sizeof(bitmap_buffer), &info) == MONO_IMAGE_OK);
            assert(info.format == MONO_IMAGE_FORMAT_BMP_1BPP);
            assert(info.source_top_down == top_down);
            assert(memcmp(bitmap_buffer, expected_bitmap,
                          sizeof(bitmap_buffer)) == 0);
        }
    }

    const size_t source_size = build_bmp(false, false);
    put_u32_le(source_buffer, 34U, 0U);
    put_u32_le(source_buffer, 46U, 0U);
    put_u32_le(source_buffer, 50U, 0U);
    assert(mono_image_decode(
               source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
               bitmap_buffer, sizeof(bitmap_buffer), NULL) == MONO_IMAGE_OK);
    assert(memcmp(bitmap_buffer, expected_bitmap, sizeof(bitmap_buffer)) == 0);
}

static void test_bmp_header_rejections(void)
{
    size_t source_size = build_bmp(false, false);

    put_u32_le(source_buffer, 2U, TEST_BMP_FILE_BYTES - 1U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_INVALID_HEADER);
    source_size = build_bmp(false, false);
    put_u16_le(source_buffer, 6U, 1U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_INVALID_HEADER);
    source_size = build_bmp(false, false);
    put_u32_le(source_buffer, 10U, TEST_BMP_PIXEL_OFFSET + 1U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_INVALID_HEADER);
    source_size = build_bmp(false, false);
    put_u32_le(source_buffer, 14U, 108U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_INVALID_HEADER);

    source_size = build_bmp(false, false);
    put_u32_le(source_buffer, 18U, 399U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_WRONG_DIMENSIONS);
    source_size = build_bmp(false, false);
    put_u32_le(source_buffer, 22U, UINT32_C(0x80000000));
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_WRONG_DIMENSIONS);

    source_size = build_bmp(false, false);
    put_u16_le(source_buffer, 26U, 2U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_UNSUPPORTED_ENCODING);
    source_size = build_bmp(false, false);
    put_u16_le(source_buffer, 28U, 8U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_UNSUPPORTED_ENCODING);
    source_size = build_bmp(false, false);
    put_u32_le(source_buffer, 30U, 1U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_UNSUPPORTED_ENCODING);
    source_size = build_bmp(false, false);
    put_u32_le(source_buffer, 34U, 1U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_UNSUPPORTED_ENCODING);
    source_size = build_bmp(false, false);
    put_u32_le(source_buffer, 46U, 1U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_UNSUPPORTED_ENCODING);
    source_size = build_bmp(false, false);
    put_u32_le(source_buffer, 50U, 3U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_UNSUPPORTED_ENCODING);

    source_size = build_bmp(false, false);
    source_buffer[54U] = 127U;
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_INVALID_PALETTE);
    source_size = build_bmp(false, false);
    source_buffer[57U] = 1U;
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_INVALID_PALETTE);
    source_size = build_bmp(false, false);
    set_bmp_palette_entry(source_buffer, 58U, 0U);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_INVALID_PALETTE);
}

static void test_bmp_size_bounds(void)
{
    const size_t source_size = build_bmp(false, false);

    for (size_t truncated_size = 0U; truncated_size < source_size;
         ++truncated_size) {
        expect_any_error_without_output(
            source_buffer, truncated_size, MONO_IMAGE_FORMAT_BMP_1BPP);
    }
    source_buffer[source_size] = 0U;
    expect_error(source_buffer, source_size + 1U,
                 MONO_IMAGE_FORMAT_BMP_1BPP, MONO_IMAGE_TRAILING_DATA);
}

static void test_common_validation(void)
{
    mono_image_info_t info;
    size_t source_size = build_pbm("P4\n400 300\n");

    expect_error(source_buffer, 0U, MONO_IMAGE_FORMAT_PBM_P4,
                 MONO_IMAGE_EMPTY);
    expect_error(source_buffer, 1U, MONO_IMAGE_FORMAT_PBM_P4,
                 MONO_IMAGE_TRUNCATED);

    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_FORMAT_MISMATCH);
    source_size = build_bmp(false, false);
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_PBM_P4,
                 MONO_IMAGE_FORMAT_MISMATCH);

    source_buffer[0] = 'Z';
    source_buffer[1] = 'Z';
    expect_error(source_buffer, source_size, MONO_IMAGE_FORMAT_BMP_1BPP,
                 MONO_IMAGE_UNSUPPORTED_FORMAT);

    memset(bitmap_buffer, 0x69, sizeof(bitmap_buffer));
    memcpy(bitmap_snapshot, bitmap_buffer, sizeof(bitmap_snapshot));
    memset(&info, 0xa5, sizeof(info));
    assert(mono_image_decode(NULL, 1U, MONO_IMAGE_FORMAT_PBM_P4,
                             bitmap_buffer, sizeof(bitmap_buffer), &info) ==
           MONO_IMAGE_INVALID_ARGUMENT);
    assert(memcmp(bitmap_buffer, bitmap_snapshot, sizeof(bitmap_buffer)) == 0);
    assert(info.format == MONO_IMAGE_FORMAT_PBM_P4);
    assert(!info.source_top_down);

    source_size = build_pbm("P4\n400 300\n");
    assert(mono_image_decode(source_buffer, source_size,
                             MONO_IMAGE_FORMAT_PBM_P4, NULL,
                             sizeof(bitmap_buffer), &info) ==
           MONO_IMAGE_INVALID_ARGUMENT);
    expect_error(source_buffer, source_size, (mono_image_format_t)99,
                 MONO_IMAGE_INVALID_ARGUMENT);

    memset(bitmap_buffer, 0x27, sizeof(bitmap_buffer));
    memcpy(bitmap_snapshot, bitmap_buffer, sizeof(bitmap_snapshot));
    assert(mono_image_decode(
               source_buffer, source_size, MONO_IMAGE_FORMAT_PBM_P4,
               bitmap_buffer, MONO_IMAGE_BITMAP_BYTES - 1U, &info) ==
           MONO_IMAGE_OUTPUT_TOO_SMALL);
    assert(memcmp(bitmap_buffer, bitmap_snapshot, sizeof(bitmap_buffer)) == 0);

    source_buffer[0] = 'P';
    source_buffer[1] = '4';
    expect_error(source_buffer, MONO_IMAGE_MAX_FILE_BYTES + 1U,
                 MONO_IMAGE_FORMAT_PBM_P4, MONO_IMAGE_TOO_LARGE);

    assert(strcmp(mono_image_result_name(MONO_IMAGE_OK), "ok") == 0);
    assert(strcmp(mono_image_result_name(MONO_IMAGE_INVALID_PALETTE),
                  "invalid palette") == 0);
    assert(strcmp(mono_image_result_name((mono_image_result_t)99),
                  "unknown") == 0);
}

int main(void)
{
    assert(MONO_IMAGE_WIDTH == 400U);
    assert(MONO_IMAGE_HEIGHT == 300U);
    assert(MONO_IMAGE_ROW_BYTES == 50U);
    assert(MONO_IMAGE_BITMAP_BYTES == 15000U);
    assert(TEST_BMP_FILE_BYTES == 15662U);

    test_pbm_valid_and_polarity();
    test_pbm_header_variants();
    test_pbm_size_bounds();
    test_bmp_valid_orientation_and_palette();
    test_bmp_header_rejections();
    test_bmp_size_bounds();
    test_common_validation();

    puts("monochrome image tests passed");
    return 0;
}
