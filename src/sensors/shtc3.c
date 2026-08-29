#include "shtc3.h"

#include <stddef.h>

#include "esp_log.h"
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

typedef enum {
    SHTC3_STAGE_NONE = 0,
    SHTC3_STAGE_WAKE,
    SHTC3_STAGE_MEASURE,
    SHTC3_STAGE_READ,
    SHTC3_STAGE_CRC,
    SHTC3_STAGE_SLEEP,
    SHTC3_STAGE_RECOVERY_WAKE,
    SHTC3_STAGE_RECOVERY_RESET,
} shtc3_stage_t;

static const char *TAG = "shtc3";
static i2c_master_dev_handle_t s_device;
static uint16_t s_sensor_id;
static bool s_recovery_required;

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

static const char *stage_name(shtc3_stage_t stage)
{
    switch (stage) {
    case SHTC3_STAGE_WAKE:
        return "wake";
    case SHTC3_STAGE_MEASURE:
        return "measure";
    case SHTC3_STAGE_READ:
        return "read";
    case SHTC3_STAGE_CRC:
        return "crc";
    case SHTC3_STAGE_SLEEP:
        return "sleep";
    case SHTC3_STAGE_RECOVERY_WAKE:
        return "recovery-wake";
    case SHTC3_STAGE_RECOVERY_RESET:
        return "recovery-reset";
    case SHTC3_STAGE_NONE:
    default:
        return "unknown";
    }
}

static void cleanup_failed_attempt(shtc3_stage_t failed_stage)
{
    const esp_err_t sleep_error = sleep_sensor();
    s_recovery_required = sleep_error != ESP_OK;
    if (sleep_error != ESP_OK) {
        ESP_LOGD(TAG,
                 "cleanup sleep failed after %s failure: %s; recovery scheduled",
                 stage_name(failed_stage), esp_err_to_name(sleep_error));
    }
}

/*
 * A failed transfer can leave the sensor asleep, idle, or finishing a
 * conversion. Wait longer than the normal-mode maximum conversion time,
 * then try both paths that can reach a known idle state: wake a sleeping
 * sensor and reset an already-awake sensor. A successful reset leaves the
 * device awake and idle for the following measurement attempt.
 */
static esp_err_t recover_sensor(shtc3_stage_t *failed_stage)
{
    vTaskDelay(pdMS_TO_TICKS(20));

    const esp_err_t wake_error =
        write_command(SHTC3_COMMAND_WAKEUP);
    /* A timed-out transfer may still have reached the sensor. Respect the
     * wake-up time before attempting a reset even when host-side status is
     * uncertain. */
    vTaskDelay(pdMS_TO_TICKS(1));

    const esp_err_t reset_error =
        write_command(SHTC3_COMMAND_SOFT_RESET);
    if (reset_error != ESP_OK) {
        s_recovery_required = true;
        if (wake_error != ESP_OK) {
            *failed_stage = SHTC3_STAGE_RECOVERY_WAKE;
            ESP_LOGD(TAG,
                     "recovery wake failed: %s; reset fallback failed: %s",
                     esp_err_to_name(wake_error),
                     esp_err_to_name(reset_error));
            return wake_error;
        }
        *failed_stage = SHTC3_STAGE_RECOVERY_RESET;
        return reset_error;
    }

    vTaskDelay(pdMS_TO_TICKS(2));
    s_recovery_required = false;
    return ESP_OK;
}

static esp_err_t measure_once(shtc3_measurement_t *measurement,
                              bool wake_first,
                              shtc3_stage_t *failed_stage)
{
    *failed_stage = SHTC3_STAGE_NONE;

    if (wake_first) {
        *failed_stage = SHTC3_STAGE_WAKE;
        esp_err_t error = write_command(SHTC3_COMMAND_WAKEUP);
        if (error != ESP_OK) {
            cleanup_failed_attempt(*failed_stage);
            return error;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    *failed_stage = SHTC3_STAGE_MEASURE;
    esp_err_t error =
        write_command(SHTC3_COMMAND_MEASURE_T_RH_POLLING);
    if (error != ESP_OK) {
        cleanup_failed_attempt(*failed_stage);
        return error;
    }

    /* Normal-mode conversion takes at most 12.1 ms per the SHTC3 data sheet. */
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t response[6] = {0};
    *failed_stage = SHTC3_STAGE_READ;
    error = i2c_master_receive(s_device, response, sizeof(response),
                               SHTC3_TRANSFER_TIMEOUT_MS);
    if (error != ESP_OK) {
        cleanup_failed_attempt(*failed_stage);
        return error;
    }

    *failed_stage = SHTC3_STAGE_CRC;
    if (shtc3_crc(response, 2) != response[2] ||
        shtc3_crc(&response[3], 2) != response[5]) {
        cleanup_failed_attempt(*failed_stage);
        return ESP_ERR_INVALID_CRC;
    }

    const uint16_t raw_temperature =
        (uint16_t)((uint16_t)response[0] << 8U) | response[1];
    const uint16_t raw_humidity =
        (uint16_t)((uint16_t)response[3] << 8U) | response[4];
    float humidity = 100.0f * (float)raw_humidity / 65536.0f;
    if (humidity < 0.0f) {
        humidity = 0.0f;
    } else if (humidity > 100.0f) {
        humidity = 100.0f;
    }

    measurement->temperature_c =
        -45.0f + 175.0f * (float)raw_temperature / 65536.0f;
    measurement->humidity_percent = humidity;
    measurement->sensor_id = s_sensor_id;

    /*
     * The six measurement bytes are authoritative once both CRCs pass.
     * Failure to enter sleep affects power/state cleanup, not data validity.
     */
    *failed_stage = SHTC3_STAGE_SLEEP;
    const esp_err_t sleep_error = sleep_sensor();
    s_recovery_required = sleep_error != ESP_OK;
    if (sleep_error != ESP_OK) {
        ESP_LOGW(TAG,
                 "sleep failed after valid measurement: %s; data retained and recovery scheduled",
                 esp_err_to_name(sleep_error));
    }
    *failed_stage = SHTC3_STAGE_NONE;
    return ESP_OK;
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

    s_recovery_required = false;

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
    s_recovery_required = sleep_error != ESP_OK;
    if (sleep_error != ESP_OK) {
        ESP_LOGW(TAG,
                 "initialization sleep failed: %s; recovery scheduled",
                 esp_err_to_name(sleep_error));
    }
    return ESP_OK;
}

esp_err_t shtc3_read(shtc3_measurement_t *measurement)
{
    if (measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    shtc3_stage_t failed_stage = SHTC3_STAGE_NONE;
    bool sensor_awake = false;
    if (s_recovery_required) {
        const esp_err_t recovery_error =
            recover_sensor(&failed_stage);
        if (recovery_error != ESP_OK) {
            ESP_LOGW(TAG, "read failed at %s: %s",
                     stage_name(failed_stage),
                     esp_err_to_name(recovery_error));
            return recovery_error;
        }
        sensor_awake = true;
    }

    esp_err_t error =
        measure_once(measurement, !sensor_awake, &failed_stage);
    if (error == ESP_OK) {
        return ESP_OK;
    }

    const shtc3_stage_t initial_failed_stage = failed_stage;
    const esp_err_t initial_error = error;
    ESP_LOGD(TAG,
             "measurement attempt failed at %s: %s; recovering before one retry",
             stage_name(initial_failed_stage),
             esp_err_to_name(initial_error));

    const esp_err_t recovery_error = recover_sensor(&failed_stage);
    if (recovery_error != ESP_OK) {
        ESP_LOGW(TAG, "measurement recovery failed at %s: %s",
                 stage_name(failed_stage),
                 esp_err_to_name(recovery_error));
        return recovery_error;
    }

    error = measure_once(measurement, false, &failed_stage);
    if (error != ESP_OK) {
        ESP_LOGW(
            TAG,
            "measurement failed after retry: first=%s/%s retry=%s/%s",
            stage_name(initial_failed_stage),
            esp_err_to_name(initial_error), stage_name(failed_stage),
            esp_err_to_name(error));
    } else {
        ESP_LOGW(TAG, "measurement recovered after %s failure: %s",
                 stage_name(initial_failed_stage),
                 esp_err_to_name(initial_error));
    }
    return error;
}
