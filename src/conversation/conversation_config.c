#include "conversation_config.h"

#include <stdbool.h>
#include <string.h>

#include "app_storage.h"
#include "conversation_config_record.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define CONVERSATION_CONFIG_NAMESPACE "rlcd_cloud"
#define CONVERSATION_CONFIG_SLOT_A_KEY "cfg_a"
#define CONVERSATION_CONFIG_SLOT_B_KEY "cfg_b"

static SemaphoreHandle_t s_mutex;
static bool s_initialized;
static conversation_config_t s_config;
static conversation_config_record_slot_t s_active_slot;
static uint32_t s_generation;
static conversation_config_migration_t s_migration;

static bool recoverable_record_error(esp_err_t error)
{
    return error == ESP_ERR_NVS_NOT_FOUND ||
           error == ESP_ERR_NVS_TYPE_MISMATCH ||
           error == ESP_ERR_NVS_INVALID_LENGTH;
}

static const char *record_slot_key(
    conversation_config_record_slot_t slot)
{
    if (slot == CONVERSATION_CONFIG_RECORD_SLOT_A) {
        return CONVERSATION_CONFIG_SLOT_A_KEY;
    }
    if (slot == CONVERSATION_CONFIG_RECORD_SLOT_B) {
        return CONVERSATION_CONFIG_SLOT_B_KEY;
    }
    return NULL;
}

static esp_err_t read_record_slot(
    nvs_handle_t handle, conversation_config_record_slot_t slot,
    conversation_config_record_t *record, bool *valid)
{
    const char *key = record_slot_key(slot);
    if (key == NULL || record == NULL || valid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *valid = false;
    conversation_config_clear_sensitive(record, sizeof(*record));

    size_t encoded_size = 0U;
    esp_err_t error = nvs_get_blob(handle, key, NULL, &encoded_size);
    if (recoverable_record_error(error)) {
        return ESP_OK;
    }
    if (error != ESP_OK) {
        return error;
    }
    if (encoded_size != CONVERSATION_CONFIG_RECORD_ENCODED_SIZE &&
        encoded_size != CONVERSATION_CONFIG_RECORD_V2_ENCODED_SIZE) {
        return ESP_OK;
    }

    uint8_t encoded[CONVERSATION_CONFIG_RECORD_ENCODED_SIZE];
    error = nvs_get_blob(handle, key, encoded, &encoded_size);
    if (recoverable_record_error(error)) {
        conversation_config_clear_sensitive(encoded, sizeof(encoded));
        return ESP_OK;
    }
    if (error == ESP_OK) {
        *valid = conversation_config_record_decode(
            encoded, encoded_size, record);
    }
    conversation_config_clear_sensitive(encoded, sizeof(encoded));
    return error;
}

static esp_err_t write_record_slot(
    nvs_handle_t handle, conversation_config_record_slot_t slot,
    uint32_t generation, const conversation_config_t *config)
{
    const char *key = record_slot_key(slot);
    uint8_t encoded[CONVERSATION_CONFIG_RECORD_ENCODED_SIZE];
    if (key == NULL || !conversation_config_record_encode(
                           generation, config, encoded,
                           sizeof(encoded))) {
        conversation_config_clear_sensitive(encoded, sizeof(encoded));
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t error = nvs_set_blob(handle, key, encoded, sizeof(encoded));
    conversation_config_clear_sensitive(encoded, sizeof(encoded));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    return error;
}

esp_err_t conversation_config_init(void)
{
    if (s_initialized) {
        return ESP_OK;
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
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    esp_err_t error = app_storage_init();
    nvs_handle_t handle = 0;
    bool handle_open = false;
    if (error == ESP_OK) {
        error = nvs_open(CONVERSATION_CONFIG_NAMESPACE, NVS_READWRITE,
                         &handle);
        handle_open = error == ESP_OK;
    }

    conversation_config_record_t slot_a = {0};
    conversation_config_record_t slot_b = {0};
    bool slot_a_valid = false;
    bool slot_b_valid = false;
    if (error == ESP_OK) {
        error = read_record_slot(handle, CONVERSATION_CONFIG_RECORD_SLOT_A,
                                 &slot_a, &slot_a_valid);
    }
    if (error == ESP_OK) {
        error = read_record_slot(handle, CONVERSATION_CONFIG_RECORD_SLOT_B,
                                 &slot_b, &slot_b_valid);
    }
    if (error == ESP_OK) {
        const conversation_config_record_slot_t latest =
            conversation_config_record_select_latest(
                slot_a_valid ? &slot_a : NULL,
                slot_b_valid ? &slot_b : NULL);
        conversation_config_reset(&s_config);
        s_active_slot = latest;
        s_generation = 0U;
        s_migration = CONVERSATION_CONFIG_MIGRATION_NONE;
        if (latest == CONVERSATION_CONFIG_RECORD_SLOT_A) {
            s_config = slot_a.config;
            s_generation = slot_a.generation;
        } else if (latest == CONVERSATION_CONFIG_RECORD_SLOT_B) {
            s_config = slot_b.config;
            s_generation = slot_b.generation;
        }
        if (s_config.schema_version != CONVERSATION_CONFIG_SCHEMA_VERSION ||
            s_config.model ==
            CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME) {
            /* Keep the newest valid source slot intact until the replacement
             * record has been committed to the other slot. A storage failure
             * must neither erase credentials nor use the retired model nor
             * make an otherwise usable local boot fail. */
            s_config.model = CONVERSATION_DEFAULT_MODEL;
            s_config.schema_version = CONVERSATION_CONFIG_SCHEMA_VERSION;
            s_migration = CONVERSATION_CONFIG_MIGRATION_PENDING;
            const conversation_config_record_slot_t target =
                latest == CONVERSATION_CONFIG_RECORD_SLOT_A
                    ? CONVERSATION_CONFIG_RECORD_SLOT_B
                    : CONVERSATION_CONFIG_RECORD_SLOT_A;
            const uint32_t generation = s_generation + 1U;
            if (write_record_slot(handle, target, generation, &s_config) ==
                ESP_OK) {
                s_active_slot = target;
                s_generation = generation;
                s_migration = CONVERSATION_CONFIG_MIGRATION_COMPLETED;
            }
        }
        s_initialized = true;
    }
    if (handle_open) {
        nvs_close(handle);
    }
    conversation_config_clear_sensitive(&slot_a, sizeof(slot_a));
    conversation_config_clear_sensitive(&slot_b, sizeof(slot_b));
    xSemaphoreGive(s_mutex);
    return error;
}

esp_err_t conversation_config_get_snapshot(
    conversation_config_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    snapshot->config = s_config;
    snapshot->generation = s_generation;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t conversation_config_get_status(
    conversation_config_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    conversation_config_make_status(&s_config, status);
    status->migration = s_migration;
    status->generation = s_generation;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

/* Caller holds s_mutex and has validated the candidate. A failed write never
 * changes the active RAM config or its original committed source slot. */
static esp_err_t commit_candidate_locked(const conversation_config_t *candidate)
{
    const conversation_config_record_slot_t target =
        s_active_slot == CONVERSATION_CONFIG_RECORD_SLOT_A
            ? CONVERSATION_CONFIG_RECORD_SLOT_B
            : CONVERSATION_CONFIG_RECORD_SLOT_A;
    const uint32_t generation = s_generation + 1U;
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(CONVERSATION_CONFIG_NAMESPACE,
                               NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = write_record_slot(handle, target, generation, candidate);
        nvs_close(handle);
    }
    if (error == ESP_OK) {
        conversation_config_clear_sensitive(&s_config, sizeof(s_config));
        s_config = *candidate;
        s_active_slot = target;
        s_generation = generation;
        if (s_migration == CONVERSATION_CONFIG_MIGRATION_PENDING) {
            s_migration = CONVERSATION_CONFIG_MIGRATION_COMPLETED;
        }
    }
    return error;
}

esp_err_t conversation_config_save(
    const conversation_config_update_t *update)
{
    if (update == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    conversation_config_t candidate;
    const conversation_config_result_t validation =
        conversation_config_apply_update(&s_config, update, &candidate);
    const esp_err_t error = validation == CONVERSATION_CONFIG_RESULT_OK
        ? commit_candidate_locked(&candidate) : ESP_ERR_INVALID_ARG;
    conversation_config_clear_sensitive(&candidate, sizeof(candidate));
    xSemaphoreGive(s_mutex);
    return error;
}

esp_err_t conversation_config_save_preferences(
    const conversation_preferences_t *preferences)
{
    if (preferences == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* No caller-provided snapshot can overwrite a newer credential save. */
    conversation_config_t candidate;
    const conversation_config_result_t validation =
        conversation_config_apply_preferences(&s_config, preferences, &candidate);
    const esp_err_t error = validation == CONVERSATION_CONFIG_RESULT_OK
        ? commit_candidate_locked(&candidate)
        : (validation == CONVERSATION_CONFIG_RESULT_API_KEY_REQUIRED
               ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG);
    conversation_config_clear_sensitive(&candidate, sizeof(candidate));
    xSemaphoreGive(s_mutex);
    return error;
}

esp_err_t conversation_config_save_api_key(
    const conversation_api_key_update_t *update)
{
    if (update == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Only the key comes from the caller. Merge all other fields from the
     * latest record under the lock, including after a concurrent portal save. */
    conversation_config_t candidate = s_config;
    if (update->api_key[0] != '\0') {
        memcpy(candidate.api_key, update->api_key, sizeof(candidate.api_key));
    }
    const conversation_config_result_t validation =
        conversation_config_validate(&candidate);
    const esp_err_t error = validation == CONVERSATION_CONFIG_RESULT_OK
        ? commit_candidate_locked(&candidate)
        : (validation == CONVERSATION_CONFIG_RESULT_API_KEY_REQUIRED
               ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_ARG);
    conversation_config_clear_sensitive(&candidate, sizeof(candidate));
    xSemaphoreGive(s_mutex);
    return error;
}

esp_err_t conversation_config_clear(void)
{
    if (!s_initialized || s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open(CONVERSATION_CONFIG_NAMESPACE,
                               NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_erase_all(handle);
        if (error == ESP_OK) {
            error = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (error == ESP_OK) {
        conversation_config_reset(&s_config);
        s_active_slot = CONVERSATION_CONFIG_RECORD_SLOT_NONE;
        /* Preserve a boot-local configuration identity across clear/save.
         * Resetting to zero could associate a prior result with a different
         * API key saved later in this boot. No cleared record is retained. */
        ++s_generation;
        s_migration = CONVERSATION_CONFIG_MIGRATION_NONE;
    }
    xSemaphoreGive(s_mutex);
    return error;
}
