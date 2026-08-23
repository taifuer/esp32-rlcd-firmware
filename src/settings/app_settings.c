#include "app_settings.h"

#include <stdlib.h>
#include <time.h>

#include "app_storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "settings_record.h"

#define SETTINGS_NAMESPACE "rlcd_cfg"
#define SETTINGS_SCHEMA_KEY "schema"
#define SETTINGS_POWER_KEY "power"
#define SETTINGS_UTC_OFFSET_KEY "utc_offset"
#define SETTINGS_TEMPERATURE_UNIT_KEY "temp_unit"
#define SETTINGS_AUDIO_VOLUME_KEY "audio_vol"
#define SETTINGS_SCHEMA2_RECORD_SLOT_A_KEY "cfg_a"
#define SETTINGS_SCHEMA2_RECORD_SLOT_B_KEY "cfg_b"
#define SETTINGS_RECORD_SLOT_A_KEY "cfg3_a"
#define SETTINGS_RECORD_SLOT_B_KEY "cfg3_b"

static const char *TAG = "app_settings";
static SemaphoreHandle_t s_mutex;
static bool s_initialized;
static bool s_future_schema;
static app_settings_t s_settings;
static settings_record_slot_t s_active_slot;
static uint32_t s_generation;

static bool recoverable_field_error(esp_err_t error)
{
    return error == ESP_ERR_NVS_NOT_FOUND ||
           error == ESP_ERR_NVS_TYPE_MISMATCH;
}

static esp_err_t load_v1_fields(nvs_handle_t handle,
                                app_settings_t *settings)
{
    uint8_t value_u8 = 0U;
    int16_t value_i16 = 0;

    esp_err_t error = nvs_get_u8(handle, SETTINGS_POWER_KEY, &value_u8);
    if (error == ESP_OK && value_u8 <= APP_POWER_MODE_SAVING) {
        settings->power_mode = (app_power_mode_t)value_u8;
    } else if (!recoverable_field_error(error) && error != ESP_OK) {
        return error;
    }

    error = nvs_get_i16(handle, SETTINGS_UTC_OFFSET_KEY, &value_i16);
    if (error == ESP_OK &&
        value_i16 >= APP_SETTINGS_MIN_UTC_OFFSET_MINUTES &&
        value_i16 <= APP_SETTINGS_MAX_UTC_OFFSET_MINUTES &&
        value_i16 % APP_SETTINGS_UTC_OFFSET_STEP_MINUTES == 0) {
        settings->utc_offset_minutes = value_i16;
    } else if (!recoverable_field_error(error) && error != ESP_OK) {
        return error;
    }

    error = nvs_get_u8(handle, SETTINGS_TEMPERATURE_UNIT_KEY, &value_u8);
    if (error == ESP_OK && value_u8 <= APP_TEMPERATURE_UNIT_FAHRENHEIT) {
        settings->temperature_unit = (app_temperature_unit_t)value_u8;
    } else if (!recoverable_field_error(error) && error != ESP_OK) {
        return error;
    }

    error = nvs_get_u8(handle, SETTINGS_AUDIO_VOLUME_KEY, &value_u8);
    if (error == ESP_OK && value_u8 <= 100U) {
        settings->audio_playback_volume = value_u8;
    } else if (!recoverable_field_error(error) && error != ESP_OK) {
        return error;
    }

    return ESP_OK;
}

static const char *record_slot_key(settings_record_slot_t slot)
{
    if (slot == SETTINGS_RECORD_SLOT_A) {
        return SETTINGS_RECORD_SLOT_A_KEY;
    }
    if (slot == SETTINGS_RECORD_SLOT_B) {
        return SETTINGS_RECORD_SLOT_B_KEY;
    }
    return NULL;
}

static esp_err_t write_record_slot(nvs_handle_t handle,
                                   settings_record_slot_t slot,
                                   uint32_t generation,
                                   const app_settings_t *settings)
{
    const char *key = record_slot_key(slot);
    uint8_t encoded[SETTINGS_RECORD_ENCODED_SIZE];
    if (key == NULL || !settings_record_encode(
                           generation, settings, encoded,
                           sizeof(encoded))) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t error =
        nvs_set_blob(handle, key, encoded, sizeof(encoded));
    return error == ESP_OK ? nvs_commit(handle) : error;
}

static esp_err_t read_record_slot(nvs_handle_t handle,
                                  settings_record_slot_t slot,
                                  settings_record_t *record, bool *valid)
{
    const char *key = record_slot_key(slot);
    if (key == NULL || record == NULL || valid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *valid = false;

    size_t encoded_size = 0U;
    esp_err_t error = nvs_get_blob(handle, key, NULL, &encoded_size);
    if (recoverable_field_error(error)) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    if (encoded_size != SETTINGS_RECORD_ENCODED_SIZE) {
        return ESP_OK;
    }

    uint8_t encoded[SETTINGS_RECORD_ENCODED_SIZE];
    error = nvs_get_blob(handle, key, encoded, &encoded_size);
    if (recoverable_field_error(error) ||
        error == ESP_ERR_NVS_INVALID_LENGTH) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    *valid = settings_record_decode(encoded, encoded_size, record);
    return ESP_OK;
}

static esp_err_t read_current_record_slots(
    nvs_handle_t handle, settings_record_t *slot_a, bool *slot_a_valid,
    settings_record_t *slot_b, bool *slot_b_valid)
{
    esp_err_t error = read_record_slot(handle, SETTINGS_RECORD_SLOT_A,
                                       slot_a, slot_a_valid);
    if (error == ESP_OK) {
        error = read_record_slot(handle, SETTINGS_RECORD_SLOT_B,
                                 slot_b, slot_b_valid);
    }
    return error;
}

static const char *schema2_record_slot_key(settings_record_slot_t slot)
{
    if (slot == SETTINGS_RECORD_SLOT_A) {
        return SETTINGS_SCHEMA2_RECORD_SLOT_A_KEY;
    }
    if (slot == SETTINGS_RECORD_SLOT_B) {
        return SETTINGS_SCHEMA2_RECORD_SLOT_B_KEY;
    }
    return NULL;
}

static esp_err_t read_schema2_record_slot(
    nvs_handle_t handle, settings_record_slot_t slot,
    settings_record_t *record, bool *valid)
{
    const char *key = schema2_record_slot_key(slot);
    if (key == NULL || record == NULL || valid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *valid = false;

    size_t encoded_size = 0U;
    esp_err_t error = nvs_get_blob(handle, key, NULL, &encoded_size);
    if (recoverable_field_error(error)) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    if (encoded_size != SETTINGS_RECORD_SCHEMA2_ENCODED_SIZE) {
        return ESP_OK;
    }

    uint8_t encoded[SETTINGS_RECORD_SCHEMA2_ENCODED_SIZE];
    error = nvs_get_blob(handle, key, encoded, &encoded_size);
    if (recoverable_field_error(error) ||
        error == ESP_ERR_NVS_INVALID_LENGTH) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    *valid = settings_record_decode_schema2(encoded, encoded_size,
                                            record);
    return ESP_OK;
}

static esp_err_t load_schema2_fields(nvs_handle_t handle,
                                     app_settings_t *settings,
                                     bool *found)
{
    if (settings == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *found = false;

    settings_record_t slot_a = {0};
    settings_record_t slot_b = {0};
    bool slot_a_valid = false;
    bool slot_b_valid = false;
    esp_err_t error = read_schema2_record_slot(
        handle, SETTINGS_RECORD_SLOT_A, &slot_a, &slot_a_valid);
    if (error == ESP_OK) {
        error = read_schema2_record_slot(
            handle, SETTINGS_RECORD_SLOT_B, &slot_b, &slot_b_valid);
    }
    if (error != ESP_OK) {
        return error;
    }

    const settings_record_slot_t latest = settings_record_select_latest(
        slot_a_valid ? &slot_a : NULL,
        slot_b_valid ? &slot_b : NULL);
    if (latest == SETTINGS_RECORD_SLOT_A) {
        *settings = slot_a.settings;
        *found = true;
    } else if (latest == SETTINGS_RECORD_SLOT_B) {
        *settings = slot_b.settings;
        *found = true;
    }
    return ESP_OK;
}

static esp_err_t load_current_schema_candidate(
    nvs_handle_t handle, app_settings_t *settings,
    settings_record_slot_t *active_slot, uint32_t *generation,
    bool *found)
{
    if (settings == NULL || active_slot == NULL || generation == NULL ||
        found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *active_slot = SETTINGS_RECORD_SLOT_NONE;
    *generation = 0U;
    *found = false;

    settings_record_t slot_a = {0};
    settings_record_t slot_b = {0};
    bool slot_a_valid = false;
    bool slot_b_valid = false;
    const esp_err_t error = read_current_record_slots(
        handle, &slot_a, &slot_a_valid, &slot_b, &slot_b_valid);
    if (error != ESP_OK) {
        return error;
    }

    *active_slot = settings_record_select_latest(
        slot_a_valid ? &slot_a : NULL, slot_b_valid ? &slot_b : NULL);
    if (*active_slot != SETTINGS_RECORD_SLOT_NONE) {
        const settings_record_t *selected =
            *active_slot == SETTINGS_RECORD_SLOT_A ? &slot_a : &slot_b;
        *settings = selected->settings;
        *generation = selected->generation;
        *found = true;
    }
    return ESP_OK;
}

static esp_err_t recover_schema2_or_v1(nvs_handle_t handle,
                                       app_settings_t *settings)
{
    bool schema2_found = false;
    esp_err_t error = load_schema2_fields(handle, settings,
                                          &schema2_found);
    if (error != ESP_OK) {
        return error;
    }

    const settings_migration_source_t source =
        settings_record_select_migration_source(false, schema2_found);
    return source == SETTINGS_MIGRATION_SOURCE_SCHEMA2_RECORD
               ? ESP_OK
               : load_v1_fields(handle, settings);
}

static esp_err_t recover_settings_without_schema(
    nvs_handle_t handle, app_settings_t *settings,
    settings_record_slot_t *active_slot, uint32_t *generation)
{
    bool current_found = false;
    esp_err_t error = load_current_schema_candidate(
        handle, settings, active_slot, generation, &current_found);
    if (error != ESP_OK) {
        return error;
    }

    if (!current_found) {
        *active_slot = SETTINGS_RECORD_SLOT_NONE;
        *generation = 0U;
        return recover_schema2_or_v1(handle, settings);
    }
    return ESP_OK;
}

static esp_err_t load_current_schema(nvs_handle_t handle,
                                     app_settings_t *settings,
                                     settings_record_slot_t *active_slot,
                                     uint32_t *generation)
{
    settings_record_t slot_a = {0};
    settings_record_t slot_b = {0};
    bool slot_a_valid = false;
    bool slot_b_valid = false;
    esp_err_t error = read_current_record_slots(
        handle, &slot_a, &slot_a_valid, &slot_b, &slot_b_valid);
    if (error != ESP_OK) {
        return error;
    }

    settings_record_repair_plan_t repair_plan;
    if (!settings_record_plan_repair(
            slot_a_valid ? &slot_a : NULL,
            slot_b_valid ? &slot_b : NULL, &repair_plan)) {
        return ESP_ERR_INVALID_ARG;
    }

    *active_slot = repair_plan.source_slot;
    *generation = 0U;
    if (*active_slot != SETTINGS_RECORD_SLOT_NONE) {
        const settings_record_t *selected =
            *active_slot == SETTINGS_RECORD_SLOT_A ? &slot_a : &slot_b;
        *settings = selected->settings;
        *generation = selected->generation;
    }

    uint32_t next_generation =
        *active_slot == SETTINGS_RECORD_SLOT_NONE ? 1U : *generation + 1U;
    bool repair_write_succeeded = false;
    esp_err_t last_repair_error = ESP_OK;
    if (repair_plan.write_slot_a) {
        error = write_record_slot(handle, SETTINGS_RECORD_SLOT_A,
                                  next_generation, settings);
        if (error == ESP_OK) {
            *active_slot = SETTINGS_RECORD_SLOT_A;
            *generation = next_generation++;
            repair_write_succeeded = true;
        } else {
            last_repair_error = error;
            ESP_LOGW(TAG, "could not repair settings slot A: %s",
                     esp_err_to_name(error));
        }
    }
    if (repair_plan.write_slot_b) {
        error = write_record_slot(handle, SETTINGS_RECORD_SLOT_B,
                                  next_generation, settings);
        if (error == ESP_OK) {
            *active_slot = SETTINGS_RECORD_SLOT_B;
            *generation = next_generation;
            repair_write_succeeded = true;
        } else {
            last_repair_error = error;
            ESP_LOGW(TAG, "could not repair settings slot B: %s",
                     esp_err_to_name(error));
        }
    }

    /* A readable source or any successful repair write remains authoritative
     * even if the peer could not be restored during this boot. */
    if (!settings_record_repair_is_usable(
            &repair_plan, repair_write_succeeded)) {
        return last_repair_error != ESP_OK ? last_repair_error : ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t write_next_record(nvs_handle_t handle,
                                   settings_record_slot_t active_slot,
                                   uint32_t generation,
                                   const app_settings_t *settings,
                                   settings_record_slot_t *written_slot,
                                   uint32_t *written_generation)
{
    if (settings == NULL || written_slot == NULL ||
        written_generation == NULL ||
        (active_slot != SETTINGS_RECORD_SLOT_NONE &&
         active_slot != SETTINGS_RECORD_SLOT_A &&
         active_slot != SETTINGS_RECORD_SLOT_B)) {
        return ESP_ERR_INVALID_ARG;
    }

    const settings_record_slot_t target_slot =
        active_slot == SETTINGS_RECORD_SLOT_A ? SETTINGS_RECORD_SLOT_B
                                              : SETTINGS_RECORD_SLOT_A;
    const uint32_t next_generation =
        active_slot == SETTINGS_RECORD_SLOT_NONE ? 1U : generation + 1U;
    const esp_err_t error = write_record_slot(
        handle, target_slot, next_generation, settings);
    if (error == ESP_OK) {
        *written_slot = target_slot;
        *written_generation = next_generation;
    }
    return error;
}

static bool settings_equal(const app_settings_t *left,
                           const app_settings_t *right)
{
    return left->schema_version == right->schema_version &&
           left->power_mode == right->power_mode &&
           left->utc_offset_minutes == right->utc_offset_minutes &&
           left->temperature_unit == right->temperature_unit &&
           left->audio_playback_volume ==
               right->audio_playback_volume &&
           left->update_channel == right->update_channel &&
           left->alarm_enabled == right->alarm_enabled &&
           left->alarm_hour == right->alarm_hour &&
           left->alarm_minute == right->alarm_minute &&
           left->alarm_weekdays == right->alarm_weekdays;
}

static esp_err_t migrate_to_current_schema(
    nvs_handle_t handle, const app_settings_t *settings,
    settings_record_slot_t *active_slot, uint32_t *generation)
{
    settings_record_t slot_a = {0};
    settings_record_t slot_b = {0};
    bool slot_a_valid = false;
    bool slot_b_valid = false;
    esp_err_t error = read_record_slot(handle, SETTINGS_RECORD_SLOT_A,
                                       &slot_a, &slot_a_valid);
    if (error == ESP_OK) {
        error = read_record_slot(handle, SETTINGS_RECORD_SLOT_B,
                                 &slot_b, &slot_b_valid);
    }
    if (error != ESP_OK) {
        return error;
    }

    settings_record_slot_t latest_slot = settings_record_select_latest(
        slot_a_valid ? &slot_a : NULL, slot_b_valid ? &slot_b : NULL);
    uint32_t next_generation = 1U;
    if (latest_slot == SETTINGS_RECORD_SLOT_A) {
        next_generation = slot_a.generation + 1U;
    } else if (latest_slot == SETTINGS_RECORD_SLOT_B) {
        next_generation = slot_b.generation + 1U;
    }

    if (!slot_a_valid || !settings_equal(&slot_a.settings, settings)) {
        error = write_record_slot(handle, SETTINGS_RECORD_SLOT_A,
                                  next_generation, settings);
        if (error != ESP_OK) {
            return error;
        }
        slot_a = (settings_record_t){
            .generation = next_generation,
            .settings = *settings,
        };
        slot_a_valid = true;
        next_generation++;
    }
    if (!slot_b_valid || !settings_equal(&slot_b.settings, settings)) {
        error = write_record_slot(handle, SETTINGS_RECORD_SLOT_B,
                                  next_generation, settings);
        if (error != ESP_OK) {
            return error;
        }
        slot_b = (settings_record_t){
            .generation = next_generation,
            .settings = *settings,
        };
        slot_b_valid = true;
    }

    latest_slot = settings_record_select_latest(&slot_a, &slot_b);
    if (error == ESP_OK) {
        error = nvs_set_u16(handle, SETTINGS_SCHEMA_KEY,
                            APP_SETTINGS_SCHEMA_VERSION);
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    if (error == ESP_OK) {
        *active_slot = latest_slot;
        *generation = latest_slot == SETTINGS_RECORD_SLOT_A
                          ? slot_a.generation
                          : slot_b.generation;
    }
    return error;
}

esp_err_t app_settings_init(void)
{
    if (s_initialized) {
        return s_future_schema ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
    }
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_initialized) {
        const esp_err_t result =
            s_future_schema ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
        xSemaphoreGive(s_mutex);
        return result;
    }

    esp_err_t error = app_storage_init();
    nvs_handle_t handle = 0;
    if (error == ESP_OK) {
        error = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    }

    app_settings_t loaded;
    app_settings_defaults(&loaded);
    settings_record_slot_t active_slot = SETTINGS_RECORD_SLOT_NONE;
    uint32_t generation = 0U;
    bool needs_migration = false;
    bool future_schema = false;
    if (error == ESP_OK) {
        uint16_t schema = 0U;
        error = nvs_get_u16(handle, SETTINGS_SCHEMA_KEY, &schema);
        if (recoverable_field_error(error)) {
            error = recover_settings_without_schema(
                handle, &loaded, &active_slot, &generation);
            needs_migration = error == ESP_OK;
        } else if (error == ESP_OK && schema == 1U) {
            error = load_v1_fields(handle, &loaded);
            needs_migration = error == ESP_OK;
        } else if (error == ESP_OK && schema == 2U) {
            error = recover_schema2_or_v1(handle, &loaded);
            needs_migration = error == ESP_OK;
        } else if (error == ESP_OK &&
                   schema < APP_SETTINGS_SCHEMA_VERSION) {
            needs_migration = true;
            error = ESP_OK;
        } else if (error == ESP_OK &&
                   schema > APP_SETTINGS_SCHEMA_VERSION) {
            future_schema = true;
            error = ESP_ERR_NOT_SUPPORTED;
        } else if (error == ESP_OK) {
            bool current_found = false;
            error = load_current_schema_candidate(
                handle, &loaded, &active_slot, &generation,
                &current_found);
            if (error == ESP_OK && current_found) {
                error = load_current_schema(handle, &loaded, &active_slot,
                                            &generation);
            } else if (error == ESP_OK) {
                error = recover_schema2_or_v1(handle, &loaded);
                needs_migration = error == ESP_OK;
            }
        }
    }
    if (error == ESP_OK && needs_migration) {
        error = migrate_to_current_schema(handle, &loaded, &active_slot,
                                          &generation);
    }
    if (handle != 0) {
        nvs_close(handle);
    }

    if (error == ESP_OK || future_schema) {
        s_settings = loaded;
        s_future_schema = future_schema;
        s_active_slot = active_slot;
        s_generation = generation;
        s_initialized = true;
    }
    xSemaphoreGive(s_mutex);

    if (future_schema) {
        ESP_LOGE(TAG,
                 "settings schema is newer than this firmware; using defaults without overwriting NVS");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (error == ESP_OK) {
        const int absolute_offset =
            loaded.utc_offset_minutes < 0
                ? -loaded.utc_offset_minutes
                : loaded.utc_offset_minutes;
        ESP_LOGI(TAG,
                 "settings ready: power=%s UTC%c%02d:%02d unit=%s volume=%u updates=%s alarm=%s %02u:%02u days=0x%02x",
                 loaded.power_mode == APP_POWER_MODE_SAVING ? "saving"
                                                           : "normal",
                 loaded.utc_offset_minutes < 0 ? '-' : '+',
                 absolute_offset / 60, absolute_offset % 60,
                 loaded.temperature_unit ==
                         APP_TEMPERATURE_UNIT_FAHRENHEIT
                     ? "F"
                     : "C",
                 loaded.audio_playback_volume,
                 loaded.update_channel == APP_UPDATE_CHANNEL_BETA
                     ? "beta"
                     : "stable",
                 loaded.alarm_enabled ? "on" : "off",
                 loaded.alarm_hour, loaded.alarm_minute,
                 loaded.alarm_weekdays);
    }
    return error;
}

esp_err_t app_settings_get(app_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *settings = s_settings;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t app_settings_save(const app_settings_t *settings)
{
    if (!app_settings_validate(settings)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_future_schema) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_SUPPORTED;
    }
    nvs_handle_t handle = 0;
    settings_record_slot_t written_slot = SETTINGS_RECORD_SLOT_NONE;
    uint32_t written_generation = 0U;
    esp_err_t error = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = write_next_record(handle, s_active_slot, s_generation,
                                  settings, &written_slot,
                                  &written_generation);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    if (error == ESP_OK) {
        s_settings = *settings;
        s_active_slot = written_slot;
        s_generation = written_generation;
    }
    xSemaphoreGive(s_mutex);
    return error;
}

esp_err_t app_settings_restore_defaults(void)
{
    app_settings_t defaults;
    app_settings_defaults(&defaults);
    return app_settings_save(&defaults);
}

esp_err_t app_settings_apply_timezone(const app_settings_t *settings)
{
    if (!app_settings_validate(settings)) {
        return ESP_ERR_INVALID_ARG;
    }
    char timezone[APP_SETTINGS_POSIX_TZ_CAPACITY];
    if (!app_settings_format_posix_tz(settings->utc_offset_minutes,
                                      timezone, sizeof(timezone))) {
        return ESP_ERR_INVALID_ARG;
    }
    if (setenv("TZ", timezone, 1) != 0) {
        return ESP_FAIL;
    }
    tzset();
    return ESP_OK;
}
