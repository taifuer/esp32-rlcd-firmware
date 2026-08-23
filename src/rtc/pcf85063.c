#include "pcf85063.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define PCF85063_CONTROL_1_REGISTER 0x00
#define PCF85063_SECONDS_REGISTER 0x04
#define PCF85063_CONTROL_1_STOP (1U << 5U)
#define PCF85063_CONTROL_1_12_HOUR_MODE (1U << 1U)
#define PCF85063_TRANSFER_TIMEOUT_MS 100
#define PCF85063_I2C_CLOCK_HZ 100000

static i2c_master_dev_handle_t s_device;
static SemaphoreHandle_t s_mutex;

static bool bcd_decode(uint8_t value, uint8_t *decoded)
{
    const uint8_t ones = value & 0x0fU;
    const uint8_t tens = (value >> 4U) & 0x0fU;
    if (ones > 9U || tens > 9U) {
        return false;
    }
    *decoded = (uint8_t)(tens * 10U + ones);
    return true;
}

static uint8_t bcd_encode(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 0U || month > 12U) {
        return 0U;
    }

    uint8_t result = days[month - 1U];
    const bool leap = ((year % 4U) == 0U && (year % 100U) != 0U) || (year % 400U) == 0U;
    if (month == 2U && leap) {
        result++;
    }
    return result;
}

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_device != NULL) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_I2C_ADDRESS,
        .scl_speed_hz = PCF85063_I2C_CLOCK_HZ,
    };
    const esp_err_t error =
        i2c_master_bus_add_device(bus, &config, &s_device);
    if (error != ESP_OK) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    return error;
}

esp_err_t pcf85063_read(pcf85063_datetime_t *datetime)
{
    if (datetime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_device == NULL || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const uint8_t start_register = PCF85063_SECONDS_REGISTER;
    uint8_t raw[7] = {0};
    esp_err_t error = i2c_master_transmit_receive(
        s_device, &start_register, sizeof(start_register), raw, sizeof(raw),
        PCF85063_TRANSFER_TIMEOUT_MS);
    if (error != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return error;
    }

    pcf85063_datetime_t value = {
        .year = 2000,
        .weekday = raw[4] & 0x07U,
        .clock_integrity = (raw[0] & 0x80U) == 0U,
    };

    uint8_t year = 0;
    if (!bcd_decode(raw[0] & 0x7fU, &value.second) ||
        !bcd_decode(raw[1] & 0x7fU, &value.minute) ||
        !bcd_decode(raw[2] & 0x3fU, &value.hour) ||
        !bcd_decode(raw[3] & 0x3fU, &value.day) ||
        !bcd_decode(raw[5] & 0x1fU, &value.month) ||
        !bcd_decode(raw[6], &year)) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_RESPONSE;
    }
    value.year = (uint16_t)(2000U + year);

    if (!pcf85063_datetime_is_valid(&value)) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *datetime = value;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t pcf85063_write(const pcf85063_datetime_t *datetime)
{
    if (datetime == NULL || !pcf85063_datetime_is_valid(datetime)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_device == NULL || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const uint8_t control_register = PCF85063_CONTROL_1_REGISTER;
    uint8_t control_1 = 0;
    esp_err_t error = i2c_master_transmit_receive(
        s_device, &control_register, sizeof(control_register), &control_1, sizeof(control_1),
        PCF85063_TRANSFER_TIMEOUT_MS);
    if (error != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return error;
    }

    /* Freeze the prescaler while all seven time registers are updated together. */
    const uint8_t stopped_control_1 =
        (uint8_t)((control_1 & ~PCF85063_CONTROL_1_12_HOUR_MODE) | PCF85063_CONTROL_1_STOP);
    const uint8_t stop_write[] = {
        PCF85063_CONTROL_1_REGISTER,
        stopped_control_1,
    };
    error = i2c_master_transmit(
        s_device, stop_write, sizeof(stop_write), PCF85063_TRANSFER_TIMEOUT_MS);
    if (error != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return error;
    }

    const uint8_t raw[] = {
        PCF85063_SECONDS_REGISTER,
        (uint8_t)(bcd_encode(datetime->second) & 0x7fU),
        (uint8_t)(bcd_encode(datetime->minute) & 0x7fU),
        (uint8_t)(bcd_encode(datetime->hour) & 0x3fU),
        (uint8_t)(bcd_encode(datetime->day) & 0x3fU),
        (uint8_t)(datetime->weekday & 0x07U),
        (uint8_t)(bcd_encode(datetime->month) & 0x1fU),
        bcd_encode((uint8_t)(datetime->year % 100U)),
    };
    const esp_err_t time_error =
        i2c_master_transmit(s_device, raw, sizeof(raw), PCF85063_TRANSFER_TIMEOUT_MS);

    /* Always release STOP, even when the time-register write failed. */
    const uint8_t start_write[] = {
        PCF85063_CONTROL_1_REGISTER,
        (uint8_t)(stopped_control_1 & ~PCF85063_CONTROL_1_STOP),
    };
    const esp_err_t start_error = i2c_master_transmit(
        s_device, start_write, sizeof(start_write), PCF85063_TRANSFER_TIMEOUT_MS);
    xSemaphoreGive(s_mutex);
    return time_error != ESP_OK ? time_error : start_error;
}

esp_err_t pcf85063_calculate_weekday(uint16_t year, uint8_t month, uint8_t day,
                                     uint8_t *weekday)
{
    static const uint8_t month_offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (weekday == NULL || year < 2000U || year > 2099U || month < 1U || month > 12U ||
        day < 1U || day > days_in_month(year, month)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t adjusted_year = year;
    if (month < 3U) {
        adjusted_year--;
    }
    *weekday = (uint8_t)((adjusted_year + adjusted_year / 4U - adjusted_year / 100U +
                          adjusted_year / 400U + month_offsets[month - 1U] + day) % 7U);
    return ESP_OK;
}

bool pcf85063_datetime_is_valid(const pcf85063_datetime_t *datetime)
{
    if (datetime == NULL || datetime->year < 2000U || datetime->year > 2099U ||
        datetime->month < 1U || datetime->month > 12U || datetime->weekday > 6U ||
        datetime->hour > 23U || datetime->minute > 59U || datetime->second > 59U) {
        return false;
    }
    return datetime->day >= 1U &&
           datetime->day <= days_in_month(datetime->year, datetime->month);
}
