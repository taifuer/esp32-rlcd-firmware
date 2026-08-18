#include "battery.h"

#include <stddef.h>

#include "battery_level.h"
#include "board_pins.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define BATTERY_ADC_ATTEN ADC_ATTEN_DB_12
#define BATTERY_SAMPLE_COUNT 16U
#define BATTERY_TRIM_COUNT 4U
#define BATTERY_DIVIDER_MULTIPLIER 3U
#define BATTERY_PRESENT_MIN_MV 2500U
#define BATTERY_VALID_MAX_MV 4600U
#define ADC_FALLBACK_FULL_SCALE_MV 3100U
#define ADC_FALLBACK_RAW_MAX 4095U

static const char *TAG = "battery";
static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_calibration_handle;
static adc_channel_t s_adc_channel;
static bool s_initialized;
static bool s_calibrated;

static void sort_samples(int *samples, size_t count)
{
    for (size_t index = 1U; index < count; ++index) {
        const int value = samples[index];
        size_t position = index;
        while (position > 0U && samples[position - 1U] > value) {
            samples[position] = samples[position - 1U];
            --position;
        }
        samples[position] = value;
    }
}

esp_err_t battery_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    adc_unit_t unit;
    esp_err_t error = adc_oneshot_io_to_channel(BOARD_BATTERY_ADC_GPIO, &unit, &s_adc_channel);
    if (error != ESP_OK) {
        return error;
    }

    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    error = adc_oneshot_new_unit(&unit_config, &s_adc_handle);
    if (error != ESP_OK) {
        return error;
    }

    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    error = adc_oneshot_config_channel(s_adc_handle, s_adc_channel, &channel_config);
    if (error != ESP_OK) {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return error;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = unit,
        .chan = s_adc_channel,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    error = adc_cali_create_scheme_curve_fitting(&calibration_config, &s_calibration_handle);
    if (error == ESP_OK) {
        s_calibrated = true;
    } else if (error == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "ADC calibration eFuse unavailable; using nominal conversion");
    } else {
        adc_oneshot_del_unit(s_adc_handle);
        s_adc_handle = NULL;
        return error;
    }
#endif

    s_initialized = true;
    return ESP_OK;
}

esp_err_t battery_read(battery_measurement_t *measurement)
{
    if (measurement == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *measurement = (battery_measurement_t){0};
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    int samples[BATTERY_SAMPLE_COUNT];
    for (size_t index = 0U; index < BATTERY_SAMPLE_COUNT; ++index) {
        const esp_err_t error = adc_oneshot_read(s_adc_handle, s_adc_channel, &samples[index]);
        if (error != ESP_OK) {
            return error;
        }
    }
    sort_samples(samples, BATTERY_SAMPLE_COUNT);

    uint32_t raw_sum = 0U;
    for (size_t index = BATTERY_TRIM_COUNT;
         index < BATTERY_SAMPLE_COUNT - BATTERY_TRIM_COUNT; ++index) {
        raw_sum += (uint32_t)samples[index];
    }
    const uint32_t kept_samples = BATTERY_SAMPLE_COUNT - 2U * BATTERY_TRIM_COUNT;
    const int filtered_raw = (int)((raw_sum + kept_samples / 2U) / kept_samples);

    int adc_voltage_mv;
    if (s_calibrated) {
        const esp_err_t error = adc_cali_raw_to_voltage(s_calibration_handle, filtered_raw,
                                                        &adc_voltage_mv);
        if (error != ESP_OK) {
            return error;
        }
    } else {
        adc_voltage_mv = (int)(((uint32_t)filtered_raw * ADC_FALLBACK_FULL_SCALE_MV +
                                ADC_FALLBACK_RAW_MAX / 2U) /
                               ADC_FALLBACK_RAW_MAX);
    }

    const uint32_t battery_voltage_mv = (uint32_t)adc_voltage_mv * BATTERY_DIVIDER_MULTIPLIER;
    if (battery_voltage_mv < BATTERY_PRESENT_MIN_MV || battery_voltage_mv > BATTERY_VALID_MAX_MV) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    measurement->voltage_mv = (uint16_t)battery_voltage_mv;
    measurement->percent = battery_level_from_voltage_mv(measurement->voltage_mv);
    measurement->calibrated = s_calibrated;
    return ESP_OK;
}
