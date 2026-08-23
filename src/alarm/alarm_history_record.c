#include "alarm_history_record.h"

#include <limits.h>

#define ALARM_HISTORY_RECORD_MAGIC UINT16_C(0x4c41)

enum {
    ALARM_HISTORY_RECORD_FORMAT_VERSION = 1U,
    MAGIC_OFFSET = 0U,
    FORMAT_OFFSET = 2U,
    SIZE_OFFSET = 3U,
    SCHEDULE_REVISION_OFFSET = 4U,
    DATE_KEY_OFFSET = 8U,
    CHECKSUM_OFFSET = 12U,
};

_Static_assert(CHECKSUM_OFFSET + sizeof(uint32_t) ==
                   ALARM_HISTORY_RECORD_ENCODED_SIZE,
               "alarm history record size does not match its layout");

static void put_u16(uint8_t *encoded, size_t offset, uint16_t value)
{
    encoded[offset] = (uint8_t)value;
    encoded[offset + 1U] = (uint8_t)(value >> 8U);
}

static uint16_t get_u16(const uint8_t *encoded, size_t offset)
{
    return (uint16_t)((uint16_t)encoded[offset] |
                      ((uint16_t)encoded[offset + 1U] << 8U));
}

static void put_u32(uint8_t *encoded, size_t offset, uint32_t value)
{
    encoded[offset] = (uint8_t)value;
    encoded[offset + 1U] = (uint8_t)(value >> 8U);
    encoded[offset + 2U] = (uint8_t)(value >> 16U);
    encoded[offset + 3U] = (uint8_t)(value >> 24U);
}

static uint32_t get_u32(const uint8_t *encoded, size_t offset)
{
    return (uint32_t)encoded[offset] |
           ((uint32_t)encoded[offset + 1U] << 8U) |
           ((uint32_t)encoded[offset + 2U] << 16U) |
           ((uint32_t)encoded[offset + 3U] << 24U);
}

static uint32_t record_checksum(const uint8_t *encoded, size_t size)
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

static bool is_leap_year(uint32_t year)
{
    return year % 4U == 0U &&
           (year % 100U != 0U || year % 400U == 0U);
}

static bool date_key_is_valid(uint32_t date_key)
{
    const uint32_t year = date_key / 10000U;
    const uint32_t month = date_key / 100U % 100U;
    const uint32_t day = date_key % 100U;
    if (year < 2000U || year > 2099U || month < 1U || month > 12U) {
        return false;
    }

    static const uint8_t days_per_month[12] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };
    uint32_t maximum_day = days_per_month[month - 1U];
    if (month == 2U && is_leap_year(year)) {
        maximum_day = 29U;
    }
    return day >= 1U && day <= maximum_day;
}

bool alarm_history_record_is_valid(
    const alarm_history_record_t *record)
{
    return record != NULL && date_key_is_valid(record->date_key);
}

bool alarm_history_record_encode(
    const alarm_history_record_t *record,
    uint8_t *encoded,
    size_t encoded_size)
{
    if (!alarm_history_record_is_valid(record) || encoded == NULL ||
        encoded_size != ALARM_HISTORY_RECORD_ENCODED_SIZE) {
        return false;
    }

    for (size_t index = 0U; index < encoded_size; ++index) {
        encoded[index] = 0U;
    }
    put_u16(encoded, MAGIC_OFFSET, ALARM_HISTORY_RECORD_MAGIC);
    encoded[FORMAT_OFFSET] = ALARM_HISTORY_RECORD_FORMAT_VERSION;
    encoded[SIZE_OFFSET] = ALARM_HISTORY_RECORD_ENCODED_SIZE;
    put_u32(encoded, SCHEDULE_REVISION_OFFSET,
            record->schedule_revision);
    put_u32(encoded, DATE_KEY_OFFSET, record->date_key);
    put_u32(encoded, CHECKSUM_OFFSET,
            record_checksum(encoded, CHECKSUM_OFFSET));
    return true;
}

bool alarm_history_record_decode(
    const uint8_t *encoded,
    size_t encoded_size,
    alarm_history_record_t *record)
{
    if (encoded == NULL || record == NULL ||
        encoded_size != ALARM_HISTORY_RECORD_ENCODED_SIZE ||
        get_u16(encoded, MAGIC_OFFSET) != ALARM_HISTORY_RECORD_MAGIC ||
        encoded[FORMAT_OFFSET] != ALARM_HISTORY_RECORD_FORMAT_VERSION ||
        encoded[SIZE_OFFSET] != ALARM_HISTORY_RECORD_ENCODED_SIZE ||
        get_u32(encoded, CHECKSUM_OFFSET) !=
            record_checksum(encoded, CHECKSUM_OFFSET)) {
        return false;
    }

    const alarm_history_record_t decoded = {
        .schedule_revision =
            get_u32(encoded, SCHEDULE_REVISION_OFFSET),
        .date_key = get_u32(encoded, DATE_KEY_OFFSET),
    };
    if (!alarm_history_record_is_valid(&decoded)) {
        return false;
    }
    *record = decoded;
    return true;
}
