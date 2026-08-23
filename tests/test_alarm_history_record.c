#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "alarm_history_record.h"

enum {
    TEST_MAGIC_OFFSET = 0U,
    TEST_FORMAT_OFFSET = 2U,
    TEST_SIZE_OFFSET = 3U,
    TEST_REVISION_OFFSET = 4U,
    TEST_DATE_OFFSET = 8U,
    TEST_CHECKSUM_OFFSET = 12U,
};

static void put_u16(uint8_t *encoded, size_t offset, uint16_t value)
{
    encoded[offset] = (uint8_t)value;
    encoded[offset + 1U] = (uint8_t)(value >> 8U);
}

static void put_u32(uint8_t *encoded, size_t offset, uint32_t value)
{
    encoded[offset] = (uint8_t)value;
    encoded[offset + 1U] = (uint8_t)(value >> 8U);
    encoded[offset + 2U] = (uint8_t)(value >> 16U);
    encoded[offset + 3U] = (uint8_t)(value >> 24U);
}

static uint32_t test_checksum(const uint8_t *encoded, size_t size)
{
    uint32_t checksum = UINT32_MAX;
    for (size_t index = 0U; index < size; ++index) {
        checksum ^= encoded[index];
        for (unsigned int bit = 0U; bit < CHAR_BIT; ++bit) {
            const uint32_t mask =
                (uint32_t)(0U - (checksum & UINT32_C(1)));
            checksum = (checksum >> 1U) ^
                       (UINT32_C(0xedb88320) & mask);
        }
    }
    return checksum ^ UINT32_MAX;
}

static void refresh_checksum(uint8_t *encoded)
{
    put_u32(encoded, TEST_CHECKSUM_OFFSET,
            test_checksum(encoded, TEST_CHECKSUM_OFFSET));
}

static void test_round_trip_and_fixed_layout(void)
{
    const alarm_history_record_t source = {
        .schedule_revision = UINT32_C(0x78563412),
        .date_key = 20260824U,
    };
    uint8_t encoded[ALARM_HISTORY_RECORD_ENCODED_SIZE];
    assert(alarm_history_record_encode(&source, encoded,
                                       sizeof(encoded)));
    assert(sizeof(encoded) == 16U);
    assert(encoded[0] == 'A' && encoded[1] == 'L');
    assert(encoded[TEST_FORMAT_OFFSET] == 1U);
    assert(encoded[TEST_SIZE_OFFSET] == 16U);
    assert(encoded[TEST_REVISION_OFFSET] == 0x12U &&
           encoded[TEST_REVISION_OFFSET + 1U] == 0x34U &&
           encoded[TEST_REVISION_OFFSET + 2U] == 0x56U &&
           encoded[TEST_REVISION_OFFSET + 3U] == 0x78U);
    assert(encoded[TEST_DATE_OFFSET] == 0xd8U &&
           encoded[TEST_DATE_OFFSET + 1U] == 0x27U &&
           encoded[TEST_DATE_OFFSET + 2U] == 0x35U &&
           encoded[TEST_DATE_OFFSET + 3U] == 0x01U);
    assert(encoded[TEST_CHECKSUM_OFFSET] == 0x57U &&
           encoded[TEST_CHECKSUM_OFFSET + 1U] == 0x5bU &&
           encoded[TEST_CHECKSUM_OFFSET + 2U] == 0x50U &&
           encoded[TEST_CHECKSUM_OFFSET + 3U] == 0x35U);

    alarm_history_record_t decoded = {0};
    assert(alarm_history_record_decode(encoded, sizeof(encoded),
                                       &decoded));
    assert(decoded.schedule_revision == source.schedule_revision);
    assert(decoded.date_key == source.date_key);
}

static void test_date_validation(void)
{
    alarm_history_record_t record = {
        .schedule_revision = 0U,
        .date_key = 20240229U,
    };
    assert(alarm_history_record_is_valid(&record));

    const uint32_t invalid_dates[] = {
        0U,
        19991231U,
        21000101U,
        20230001U,
        20231301U,
        20230229U,
        20260431U,
        20260800U,
    };
    for (size_t index = 0U;
         index < sizeof(invalid_dates) / sizeof(invalid_dates[0]);
         ++index) {
        record.date_key = invalid_dates[index];
        assert(!alarm_history_record_is_valid(&record));
    }
    assert(!alarm_history_record_is_valid(NULL));
}

static void test_rejects_invalid_arguments_and_corruption(void)
{
    const alarm_history_record_t source = {
        .schedule_revision = 17U,
        .date_key = 20260824U,
    };
    uint8_t encoded[ALARM_HISTORY_RECORD_ENCODED_SIZE];
    assert(alarm_history_record_encode(&source, encoded,
                                       sizeof(encoded)));
    assert(!alarm_history_record_encode(NULL, encoded, sizeof(encoded)));
    assert(!alarm_history_record_encode(&source, NULL, sizeof(encoded)));
    assert(!alarm_history_record_encode(&source, encoded,
                                        sizeof(encoded) - 1U));

    alarm_history_record_t decoded = {
        .schedule_revision = 99U,
        .date_key = 20260101U,
    };
    assert(!alarm_history_record_decode(NULL, sizeof(encoded), &decoded));
    assert(!alarm_history_record_decode(encoded, sizeof(encoded), NULL));
    assert(!alarm_history_record_decode(encoded, sizeof(encoded) - 1U,
                                        &decoded));

    for (size_t index = 0U; index < sizeof(encoded); ++index) {
        uint8_t corrupted[ALARM_HISTORY_RECORD_ENCODED_SIZE];
        memcpy(corrupted, encoded, sizeof(corrupted));
        corrupted[index] ^= 1U;
        assert(!alarm_history_record_decode(corrupted,
                                            sizeof(corrupted), &decoded));
        assert(decoded.schedule_revision == 99U);
        assert(decoded.date_key == 20260101U);
    }
}

static void test_strict_headers_and_semantic_validation(void)
{
    const alarm_history_record_t source = {
        .schedule_revision = 1U,
        .date_key = 20260824U,
    };
    uint8_t encoded[ALARM_HISTORY_RECORD_ENCODED_SIZE];
    assert(alarm_history_record_encode(&source, encoded,
                                       sizeof(encoded)));
    alarm_history_record_t decoded = {0};

    put_u16(encoded, TEST_MAGIC_OFFSET, UINT16_C(0x4c42));
    refresh_checksum(encoded);
    assert(!alarm_history_record_decode(encoded, sizeof(encoded),
                                        &decoded));
    assert(alarm_history_record_encode(&source, encoded,
                                       sizeof(encoded)));
    encoded[TEST_FORMAT_OFFSET] = 2U;
    refresh_checksum(encoded);
    assert(!alarm_history_record_decode(encoded, sizeof(encoded),
                                        &decoded));
    assert(alarm_history_record_encode(&source, encoded,
                                       sizeof(encoded)));
    encoded[TEST_SIZE_OFFSET] = 15U;
    refresh_checksum(encoded);
    assert(!alarm_history_record_decode(encoded, sizeof(encoded),
                                        &decoded));
    assert(alarm_history_record_encode(&source, encoded,
                                       sizeof(encoded)));
    put_u32(encoded, TEST_DATE_OFFSET, 20230229U);
    refresh_checksum(encoded);
    assert(!alarm_history_record_decode(encoded, sizeof(encoded),
                                        &decoded));
}

int main(void)
{
    test_round_trip_and_fixed_layout();
    test_date_validation();
    test_rejects_invalid_arguments_and_corruption();
    test_strict_headers_and_semantic_validation();
    puts("alarm history record tests passed");
    return 0;
}
