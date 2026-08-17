#include "shtc3.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SHTC3_COMMAND_READ_ID 0xEFC8
#define SHTC3_COMMAND_SOFT_RESET 0x805D
#define SHTC3_COMMAND_SLEEP 0xB098
#define SHTC3_COMMAND_WAKEUP 0x3517
#define SHTC3_COMMAND_MEASURE_T_RH_POLLING 0x7866

#define SHTC3_I2C_CLOCK_HZ 100000
#define SHTC3_TRANSFER_TIMEOUT_MS 100
#define SHTC3_PRODUCT_ID_MASK 0x083F
#define SHTC3_PRODUCT_ID_VALUE 0x0807

static i2c_master_dev_handle_t s_device;
static uint16_t s_sensor_id;

static uint8_t shtc3_crc(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xffU;
    for (size_t index = 0; index < length; index++) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8U; bit++) {
            crc = (crc & 0x80U) != 0U ? (uint8_t)((crc << 1U) ^ 0x31U)
                                      : (uint8_t)(crc << 1U);
        }
    }
    return crc;
}

static esp_err_t write_command(uint16_t command)
{
    const uint8_t bytes[] = {
        (uint8_t)(command >> 8U),
        (uint8_t)(command & 0xffU),
    };
    return i2c_master_transmit(s_device, bytes, sizeof(bytes), SHTC3_TRANSFER_TIMEOUT_MS);
}

static esp_err_t sleep_sensor(void)
{
    return write_command(SHTC3_COMMAND_SLEEP);
}

esp_err_t shtc3_init(i2c_master_bus_handle_t bus, uint16_t *sensor_id)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_device == NULL) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = SHTC3_I2C_ADDRESS,
            .scl_speed_hz = SHTC3_I2C_CLOCK_HZ,
        };
        esp_err_t error = i2c_master_bus_add_device(bus, &config, &s_device);
        if (error != ESP_OK) {
            return error;
        }
    }

    esp_err_t error = write_command(SHTC3_COMMAND_WAKEUP);
    if (error != ESP_OK) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    error = write_command(SHTC3_COMMAND_SOFT_RESET);
    if (error != ESP_OK) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(2));

    error = write_command(SHTC3_COMMAND_READ_ID);
    if (error != ESP_OK) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t response[3] = {0};
    error = i2c_master_receive(s_device, response, sizeof(response), SHTC3_TRANSFER_TIMEOUT_MS);
    const esp_err_t sleep_error = sleep_sensor();
    if (error != ESP_OK) {
        return error;
    }
    if (shtc3_crc(response, 2) != response[2]) {
        return ESP_ERR_INVALID_CRC;
    }

    s_sensor_id = (uint16_t)((uint16_t)response[0] << 8U) | response[1];
    if (sensor_id != NULL) {
        *sensor_id = s_sensor_id;
    }
    if ((s_sensor_id & SHTC3_PRODUCT_ID_MASK) != SHTC3_PRODUCT_ID_VALUE) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return sleep_error;
}

esp_err_t shtc3_read(shtc3_measurement_t *measurement)
{
    if (measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = write_command(SHTC3_COMMAND_WAKEUP);
    if (error != ESP_OK) {
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    error = write_command(SHTC3_COMMAND_MEASURE_T_RH_POLLING);
    if (error != ESP_OK) {
        (void)sleep_sensor();
        return error;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t response[6] = {0};
    error = i2c_master_receive(s_device, response, sizeof(response), SHTC3_TRANSFER_TIMEOUT_MS);
    const esp_err_t sleep_error = sleep_sensor();
    if (error != ESP_OK) {
        return error;
    }
    if (shtc3_crc(response, 2) != response[2] ||
        shtc3_crc(&response[3], 2) != response[5]) {
        return ESP_ERR_INVALID_CRC;
    }

    const uint16_t raw_temperature = (uint16_t)((uint16_t)response[0] << 8U) | response[1];
    const uint16_t raw_humidity = (uint16_t)((uint16_t)response[3] << 8U) | response[4];
    float humidity = 100.0f * (float)raw_humidity / 65536.0f;
    if (humidity < 0.0f) {
        humidity = 0.0f;
    } else if (humidity > 100.0f) {
        humidity = 100.0f;
    }

    measurement->temperature_c = -45.0f + 175.0f * (float)raw_temperature / 65536.0f;
    measurement->humidity_percent = humidity;
    measurement->sensor_id = s_sensor_id;
    return sleep_error;
}
