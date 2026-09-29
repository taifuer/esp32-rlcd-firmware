/* Host fault injection for the real conversation configuration service.
 * The fake NVS models commit outcomes, not ESP-IDF internals or real power
 * loss. Including the service permits a cold-boot reset of its static RAM. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/conversation/conversation_config.c"

typedef struct {
    bool present;
    size_t size;
    uint8_t bytes[CONVERSATION_CONFIG_RECORD_ENCODED_SIZE];
} test_blob_t;

static test_blob_t blobs[2];
static test_blob_t pending_blob;
static unsigned pending_slot;
static bool pending_write;
static bool pending_erase;
static bool lock_held;
static bool handle_open;
static unsigned writes;
static unsigned commits;
static unsigned erases;
static bool fail_set;
static bool fail_commit;
static bool commit_before_failure;
static bool fail_open;
static bool fail_erase;
static esp_err_t read_error[2];

esp_err_t app_storage_init(void) { return ESP_OK; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &lock_held; }
int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{
    (void)timeout;
    assert(mutex == &lock_held && !lock_held);
    lock_held = true;
    return pdTRUE;
}
void xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex == &lock_held && lock_held);
    lock_held = false;
}

static unsigned slot_index(const char *key)
{
    assert(strcmp(key, "cfg_a") == 0 || strcmp(key, "cfg_b") == 0);
    return strcmp(key, "cfg_a") == 0 ? 0U : 1U;
}

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(lock_held && !handle_open);
    assert(strcmp(name, "rlcd_cloud") == 0 && mode == NVS_READWRITE);
    if (fail_open) {
        return ESP_FAIL;
    }
    handle_open = true;
    *handle = 1U;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(lock_held && handle_open && handle == 1U);
    handle_open = false;
    pending_write = false;
    pending_erase = false;
    conversation_config_clear_sensitive(&pending_blob, sizeof(pending_blob));
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value,
                       size_t *size)
{
    assert(lock_held && handle_open && handle == 1U);
    const unsigned slot = slot_index(key);
    if (read_error[slot] != ESP_OK) {
        return read_error[slot];
    }
    const test_blob_t *blob = &blobs[slot];
    if (!blob->present) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (value != NULL && *size < blob->size) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    *size = blob->size;
    if (value != NULL) {
        memcpy(value, blob->bytes, blob->size);
    }
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key,
                       const void *value, size_t size)
{
    assert(lock_held && handle_open && handle == 1U && !pending_write);
    assert(size == sizeof(pending_blob.bytes));
    ++writes;
    if (fail_set) {
        return ESP_FAIL;
    }
    pending_slot = slot_index(key);
    pending_blob.present = true;
    pending_blob.size = size;
    memcpy(pending_blob.bytes, value, size);
    pending_write = true;
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle)
{
    assert(lock_held && handle_open && handle == 1U);
    ++erases;
    if (fail_erase) {
        return ESP_FAIL;
    }
    pending_erase = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(lock_held && handle_open && handle == 1U);
    ++commits;
    if (!fail_commit || commit_before_failure) {
        if (pending_erase) {
            memset(blobs, 0, sizeof(blobs));
        }
        if (pending_write) {
            blobs[pending_slot] = pending_blob;
        }
    }
    return fail_commit ? ESP_FAIL : ESP_OK;
}

static void simulate_restart(void)
{
    assert(!lock_held && !handle_open);
    s_initialized = false;
    s_mutex = NULL;
    conversation_config_clear_sensitive(&s_config, sizeof(s_config));
    s_active_slot = CONVERSATION_CONFIG_RECORD_SLOT_NONE;
    s_generation = 0U;
    s_migration = CONVERSATION_CONFIG_MIGRATION_NONE;
    pending_write = false;
    pending_erase = false;
    conversation_config_clear_sensitive(&pending_blob, sizeof(pending_blob));
}

static void reset_fixture(void)
{
    simulate_restart();
    memset(blobs, 0, sizeof(blobs));
    writes = 0U;
    commits = 0U;
    erases = 0U;
    fail_set = false;
    fail_commit = false;
    commit_before_failure = false;
    fail_open = false;
    fail_erase = false;
    read_error[0] = ESP_OK;
    read_error[1] = ESP_OK;
}

static void refresh_checksum(test_blob_t *blob)
{
    const size_t checksum_offset = blob->size - 4U;
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0U; index < checksum_offset; ++index) {
        crc ^= blob->bytes[index];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^
                  (UINT32_C(0xedb88320) & (0U - (crc & 1U)));
        }
    }
    crc ^= UINT32_MAX;
    for (unsigned byte = 0U; byte < 4U; ++byte) {
        blob->bytes[checksum_offset + byte] =
            (uint8_t)(crc >> (8U * byte));
    }
}

static void seed_record(unsigned slot, uint32_t generation,
                        bool legacy, bool enabled, const char *key,
                        const char *host)
{
    conversation_config_t config;
    conversation_config_defaults(&config);
    config.enabled = enabled;
    snprintf(config.api_key, sizeof(config.api_key), "%s", key);
    snprintf(config.api_host, sizeof(config.api_host), "%s", host);
    blobs[slot].present = true;
    blobs[slot].size = sizeof(blobs[slot].bytes);
    assert(conversation_config_record_encode(
        generation, &config, blobs[slot].bytes, blobs[slot].size));
    assert(blobs[slot].bytes[4] == 3U && blobs[slot].bytes[12] == 3U);
    assert(blobs[slot].bytes[15] == 1U);
    if (legacy) {
        /* Reconstruct the released format 2/schema 2 layout. Its first 405
         * bytes are unchanged; the following four bytes hold its checksum. */
        blobs[slot].size = CONVERSATION_CONFIG_RECORD_V2_ENCODED_SIZE;
        blobs[slot].bytes[4] = 2U;
        blobs[slot].bytes[6] = (uint8_t)blobs[slot].size;
        blobs[slot].bytes[7] = (uint8_t)(blobs[slot].size >> 8U);
        blobs[slot].bytes[12] = 2U;
        blobs[slot].bytes[15] = 0U;
        refresh_checksum(&blobs[slot]);
    }
    conversation_config_reset(&config);
}

static void assert_snapshot(const char *key, const char *host, bool enabled,
                            uint32_t generation,
                            conversation_config_migration_t migration)
{
    conversation_config_snapshot_t snapshot;
    assert(conversation_config_get_snapshot(&snapshot) == ESP_OK);
    assert(snapshot.config.model ==
           CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH);
    assert(snapshot.config.enabled == enabled);
    assert(snapshot.config.schema_version == CONVERSATION_CONFIG_SCHEMA_VERSION);
    assert(strcmp(snapshot.config.api_key, key) == 0);
    assert(strcmp(snapshot.config.api_host, host) == 0);
    assert(snapshot.generation == generation);
    conversation_config_status_t status;
    assert(conversation_config_get_status(&status) == ESP_OK);
    assert(status.migration == migration && status.generation == generation);
    assert(status.model == snapshot.config.model && status.enabled == enabled);
    assert(status.configured == (key[0] != '\0'));
    if (key[0] != '\0') {
        const uint8_t *bytes = (const uint8_t *)&status;
        const size_t key_size = strlen(key);
        for (size_t offset = 0U; offset + key_size <= sizeof(status); ++offset) {
            assert(memcmp(bytes + offset, key, key_size) != 0);
        }
    }
    conversation_config_clear_sensitive(&snapshot, sizeof(snapshot));
}

static void test_new_install_and_current_model_are_not_rewritten(void)
{
    reset_fixture();
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("", "", false, 0U, CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(writes == 0U && commits == 0U);

    reset_fixture();
    seed_record(0U, 5U, true, true, "sk-old", "");
    seed_record(1U, 6U, false, false, "sk-current",
                 "dashscope-intl.aliyuncs.com");
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-current", "dashscope-intl.aliyuncs.com", false,
                    6U, CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(writes == 0U && commits == 0U);
}

static void test_migration_preserves_credentials_and_disabled_state(void)
{
    static const char *const hosts[] = {
        "", "dashscope.aliyuncs.com", "dashscope-intl.aliyuncs.com",
        "workspace.cn-beijing.maas.aliyuncs.com",
        "workspace.ap-southeast-1.maas.aliyuncs.com",
    };
    for (unsigned enabled = 0U; enabled <= 1U; ++enabled) {
        for (size_t host = 0U; host < sizeof(hosts) / sizeof(hosts[0]); ++host) {
            reset_fixture();
            seed_record(0U, 8U, false, enabled == 0U, "sk-older", "");
            seed_record(1U, 9U, true, enabled != 0U, "sk-newest", hosts[host]);
            const test_blob_t source = blobs[1];
            assert(conversation_config_init() == ESP_OK);
            assert_snapshot("sk-newest", hosts[host], enabled != 0U, 10U,
                            CONVERSATION_CONFIG_MIGRATION_COMPLETED);
            assert(memcmp(&blobs[1], &source, sizeof(source)) == 0);
            assert(blobs[0].bytes[15] == 1U);
            assert(writes == 1U && commits == 1U && erases == 0U);
            assert(conversation_config_init() == ESP_OK);
            assert(writes == 1U && commits == 1U);
            simulate_restart();
            assert(conversation_config_init() == ESP_OK);
            assert_snapshot("sk-newest", hosts[host], enabled != 0U, 10U,
                            CONVERSATION_CONFIG_MIGRATION_NONE);
            assert(writes == 1U && commits == 1U);
        }
    }
}

static void test_corrupt_unknown_and_recoverable_slots(void)
{
    for (unsigned fault = 0U; fault < 8U; ++fault) {
        reset_fixture();
        seed_record(0U, 100U, true, false, "sk-survives", "");
        seed_record(1U, 101U, true, true, "sk-rejected", "");
        switch (fault) {
        case 0U: blobs[1].bytes[42] ^= 1U; break;
        case 1U: --blobs[1].size; break;
        case 2U:
            /* No published v1 layout is known: reject, never reinterpret its
             * missing model byte as the new default. */
            blobs[1].bytes[4] = 1U;
            blobs[1].bytes[12] = 1U;
            refresh_checksum(&blobs[1]);
            break;
        case 3U:
            blobs[1].bytes[15] = 9U;
            refresh_checksum(&blobs[1]);
            break;
        case 4U:
            blobs[1].bytes[12] = 3U;
            refresh_checksum(&blobs[1]);
            break;
        case 5U: read_error[1] = ESP_ERR_NVS_TYPE_MISMATCH; break;
        case 6U: read_error[1] = ESP_ERR_NVS_INVALID_LENGTH; break;
        default: blobs[1].present = false; break;
        }
        const test_blob_t source = blobs[0];
        assert(conversation_config_init() == ESP_OK);
        assert_snapshot("sk-survives", "", false, 101U,
                        CONVERSATION_CONFIG_MIGRATION_COMPLETED);
        assert(memcmp(&blobs[0], &source, sizeof(source)) == 0);
        assert(writes == 1U && commits == 1U);
    }
}

static void test_generation_wrap_and_equal_generations(void)
{
    reset_fixture();
    seed_record(0U, UINT32_MAX - 1U, false, true, "sk-older", "");
    seed_record(1U, UINT32_MAX, true, false, "sk-wrap", "");
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-wrap", "", false, 0U,
                    CONVERSATION_CONFIG_MIGRATION_COMPLETED);
    simulate_restart();
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-wrap", "", false, 0U, CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(writes == 1U);

    reset_fixture();
    seed_record(0U, 7U, true, true, "sk-slot-a", "");
    seed_record(1U, 7U, false, false, "sk-slot-b", "");
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-slot-a", "", true, 8U,
                    CONVERSATION_CONFIG_MIGRATION_COMPLETED);
}

static void test_legacy_decoder_still_validates_all_other_fields(void)
{
    for (unsigned fault = 0U; fault < 6U; ++fault) {
        reset_fixture();
        seed_record(0U, 2U, true, true, "sk-legacy", "");
        switch (fault) {
        case 0U: blobs[0].bytes[14] = 9U; break; /* unknown service */
        case 1U: blobs[0].bytes[16] = 2U; break; /* invalid enabled flag */
        case 2U: blobs[0].bytes[17] = 1U; break; /* nonzero reserved */
        case 3U: blobs[0].bytes[22] = ' '; break; /* invalid API key */
        case 4U: blobs[0].bytes[22] = 0U; break; /* embedded NUL */
        default:
            blobs[0].bytes[20] = 1U; /* forbidden host */
            blobs[0].bytes[22U + CONVERSATION_API_KEY_MAX_LENGTH] = 'x';
            break;
        }
        refresh_checksum(&blobs[0]);
        const test_blob_t source = blobs[0];
        assert(conversation_config_init() == ESP_OK);
        assert_snapshot("", "", false, 0U, CONVERSATION_CONFIG_MIGRATION_NONE);
        assert(writes == 0U && commits == 0U);
        assert(memcmp(&blobs[0], &source, sizeof(source)) == 0);
    }
}

static void test_migration_write_failure_boots_and_retries(void)
{
    for (unsigned fault = 0U; fault < 3U; ++fault) {
        reset_fixture();
        seed_record(0U, 20U, true, true, "sk-retained",
                     "workspace.cn-beijing.maas.aliyuncs.com");
        const test_blob_t source = blobs[0];
        fail_set = fault == 0U;
        fail_commit = fault != 0U;
        commit_before_failure = fault == 2U;
        assert(conversation_config_init() == ESP_OK);
        assert_snapshot("sk-retained", "workspace.cn-beijing.maas.aliyuncs.com",
                        true, 20U, CONVERSATION_CONFIG_MIGRATION_PENDING);
        assert(memcmp(&blobs[0], &source, sizeof(source)) == 0);
        assert(erases == 0U);
        assert(conversation_config_init() == ESP_OK);
        assert(writes == 1U); /* no repeated flash writes on repeated init */
        fail_set = false;
        fail_commit = false;
        commit_before_failure = false;
        simulate_restart();
        assert(conversation_config_init() == ESP_OK);
        assert_snapshot("sk-retained", "workspace.cn-beijing.maas.aliyuncs.com",
                        true, 21U, fault == 2U
                            ? CONVERSATION_CONFIG_MIGRATION_NONE
                            : CONVERSATION_CONFIG_MIGRATION_COMPLETED);
        assert(writes == (fault == 2U ? 1U : 2U));
    }
}

static conversation_config_update_t preserving_update(bool enabled)
{
    conversation_config_update_t update = {
        .service = CONVERSATION_SERVICE_ALIYUN_REALTIME,
        .model = CONVERSATION_DEFAULT_MODEL,
        .enabled = enabled,
    };
    snprintf(update.api_host, sizeof(update.api_host), "%s",
             "dashscope-intl.aliyuncs.com");
    return update;
}

static void test_pending_migration_save_and_clear(void)
{
    reset_fixture();
    seed_record(1U, 40U, true, false, "sk-pending",
                 "dashscope-intl.aliyuncs.com");
    fail_set = true;
    assert(conversation_config_init() == ESP_OK);
    conversation_config_update_t update = preserving_update(true);
    assert(conversation_config_save(&update) == ESP_FAIL);
    assert_snapshot("sk-pending", "dashscope-intl.aliyuncs.com", false,
                    40U, CONVERSATION_CONFIG_MIGRATION_PENDING);
    fail_set = false;
    update.model = CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME;
    const unsigned attempts = writes;
    assert(conversation_config_save(&update) == ESP_ERR_INVALID_ARG);
    assert(writes == attempts);
    update.model = CONVERSATION_DEFAULT_MODEL;
    assert(conversation_config_save(&update) == ESP_OK);
    assert_snapshot("sk-pending", "dashscope-intl.aliyuncs.com", true,
                    41U, CONVERSATION_CONFIG_MIGRATION_COMPLETED);
    simulate_restart();
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-pending", "dashscope-intl.aliyuncs.com", true,
                    41U, CONVERSATION_CONFIG_MIGRATION_NONE);

    reset_fixture();
    seed_record(0U, 3U, true, true, "sk-to-clear", "");
    fail_commit = true;
    assert(conversation_config_init() == ESP_OK);
    assert(conversation_config_clear() == ESP_FAIL);
    assert_snapshot("sk-to-clear", "", true, 3U,
                    CONVERSATION_CONFIG_MIGRATION_PENDING);
    fail_commit = false;
    assert(conversation_config_clear() == ESP_OK);
    assert_snapshot("", "", false, 4U, CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(!blobs[0].present && !blobs[1].present);
    simulate_restart();
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("", "", false, 0U, CONVERSATION_CONFIG_MIGRATION_NONE);
}

static void test_clear_save_uses_a_new_configuration_identity(void)
{
    reset_fixture();
    assert(conversation_config_init() == ESP_OK);
    conversation_config_update_t update = preserving_update(true);
    snprintf(update.api_key, sizeof(update.api_key), "%s", "sk-first");
    assert(conversation_config_save(&update) == ESP_OK);
    assert_snapshot("sk-first", "dashscope-intl.aliyuncs.com", true,
                    1U, CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(conversation_config_clear() == ESP_OK);
    assert_snapshot("", "", false, 2U, CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(!blobs[0].present && !blobs[1].present);
    snprintf(update.api_key, sizeof(update.api_key), "%s", "sk-second");
    assert(conversation_config_save(&update) == ESP_OK);
    assert_snapshot("sk-second", "dashscope-intl.aliyuncs.com", true,
                    3U, CONVERSATION_CONFIG_MIGRATION_NONE);
    simulate_restart();
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-second", "dashscope-intl.aliyuncs.com", true,
                    3U, CONVERSATION_CONFIG_MIGRATION_NONE);
}

static void test_storage_read_errors_do_not_mutate_records(void)
{
    reset_fixture();
    seed_record(0U, 4U, true, false, "sk-unread", "");
    const test_blob_t source = blobs[0];
    fail_open = true;
    assert(conversation_config_init() == ESP_FAIL);
    fail_open = false;
    read_error[1] = ESP_FAIL;
    assert(conversation_config_init() == ESP_FAIL);
    assert(!s_initialized && writes == 0U && commits == 0U);
    assert(memcmp(&blobs[0], &source, sizeof(source)) == 0);
    read_error[1] = ESP_OK;
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-unread", "", false, 5U,
                    CONVERSATION_CONFIG_MIGRATION_COMPLETED);
}

static void test_schema_two_audio_model_upgrade_and_options_persistence(void)
{
    for (unsigned fault = 0U; fault < 4U; ++fault) {
        reset_fixture();
        seed_record(0U, 80U, true, false, "sk-schema-two", "dashscope-intl.aliyuncs.com");
        blobs[0].bytes[15] = (uint8_t)CONVERSATION_DEFAULT_MODEL;
        refresh_checksum(&blobs[0]);
        const test_blob_t source = blobs[0];
        fail_set = fault == 1U;
        fail_commit = fault >= 2U;
        commit_before_failure = fault == 3U;
        assert(conversation_config_init() == ESP_OK);
        assert_snapshot("sk-schema-two", "dashscope-intl.aliyuncs.com", false,
                        fault == 0U ? 81U : 80U, fault == 0U
                            ? CONVERSATION_CONFIG_MIGRATION_COMPLETED
                            : CONVERSATION_CONFIG_MIGRATION_PENDING);
        assert(s_config.voice == CONVERSATION_DEFAULT_VOICE);
        assert(s_config.reply_style == CONVERSATION_DEFAULT_REPLY_STYLE);
        assert(memcmp(&blobs[0], &source, sizeof(source)) == 0);
        fail_set = fail_commit = commit_before_failure = false;
        simulate_restart();
        assert(conversation_config_init() == ESP_OK);
        assert_snapshot("sk-schema-two", "dashscope-intl.aliyuncs.com", false, 81U,
                        (fault == 0U || fault == 3U)
                            ? CONVERSATION_CONFIG_MIGRATION_NONE
                            : CONVERSATION_CONFIG_MIGRATION_COMPLETED);
        conversation_config_update_t update = preserving_update(false);
        update.has_voice = true;
        update.voice = CONVERSATION_VOICE_LONGANLINGXI;
        update.has_reply_style = true;
        update.reply_style = CONVERSATION_REPLY_STYLE_ENGLISH_PRACTICE;
        assert(conversation_config_save(&update) == ESP_OK);
        update.has_voice = update.has_reply_style = false;
        update.enabled = true;
        assert(conversation_config_save(&update) == ESP_OK);
        simulate_restart();
        assert(conversation_config_init() == ESP_OK);
        assert_snapshot("sk-schema-two", "dashscope-intl.aliyuncs.com", true, 83U,
                        CONVERSATION_CONFIG_MIGRATION_NONE);
        assert(s_config.voice == CONVERSATION_VOICE_LONGANLINGXI);
        assert(s_config.reply_style == CONVERSATION_REPLY_STYLE_ENGLISH_PRACTICE);
        conversation_config_status_t status;
        assert(conversation_config_get_status(&status) == ESP_OK);
        assert(status.voice == s_config.voice && status.reply_style == s_config.reply_style);
        assert(conversation_config_clear() == ESP_OK);
        assert(s_config.voice == CONVERSATION_DEFAULT_VOICE);
        assert(s_config.reply_style == CONVERSATION_DEFAULT_REPLY_STYLE);
    }
}

static void test_invalid_option_record_keeps_older_valid_slot(void)
{
    for (size_t offset = 405U; offset <= 406U; ++offset) {
        reset_fixture();
        seed_record(0U, 1U, false, true, "sk-valid", "");
        seed_record(1U, 2U, false, false, "sk-invalid", "");
        blobs[1].bytes[offset] = 255U;
        refresh_checksum(&blobs[1]);
        assert(conversation_config_init() == ESP_OK);
        assert_snapshot("sk-valid", "", true, 1U, CONVERSATION_CONFIG_MIGRATION_NONE);
        assert(writes == 0U && commits == 0U);
    }
}

static void test_preferences_preserve_latest_credentials(void)
{
    reset_fixture();
    conversation_preferences_t preferences = {
        .enabled = false,
        .voice = CONVERSATION_VOICE_LONGANLUFENG,
        .reply_style = CONVERSATION_REPLY_STYLE_ENGLISH_PRACTICE,
    };
    assert(conversation_config_save_preferences(&preferences) == ESP_ERR_INVALID_STATE);
    assert(conversation_config_init() == ESP_OK);
    assert(conversation_config_save_preferences(&preferences) == ESP_ERR_INVALID_STATE);
    assert(writes == 0U && commits == 0U);

    conversation_config_update_t update = preserving_update(true);
    snprintf(update.api_key, sizeof(update.api_key), "%s", "sk-original");
    assert(conversation_config_save(&update) == ESP_OK);
    /* Simulate a newer credential save after the preference form loaded. Its
     * key, host, service and model must not be replaced by a stale snapshot. */
    snprintf(update.api_key, sizeof(update.api_key), "%s", "sk-newer");
    snprintf(update.api_host, sizeof(update.api_host), "%s",
             "latest.cn-beijing.maas.aliyuncs.com");
    assert(conversation_config_save(&update) == ESP_OK);
    const conversation_config_record_slot_t original_slot = s_active_slot;
    const test_blob_t source = blobs[original_slot == CONVERSATION_CONFIG_RECORD_SLOT_A ? 0 : 1];
    assert(conversation_config_save_preferences(&preferences) == ESP_OK);
    assert_snapshot("sk-newer", "latest.cn-beijing.maas.aliyuncs.com", false, 3U,
                    CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(s_config.service == CONVERSATION_SERVICE_ALIYUN_REALTIME);
    assert(s_config.model == CONVERSATION_DEFAULT_MODEL);
    assert(s_config.voice == preferences.voice && s_config.reply_style == preferences.reply_style);
    assert(memcmp(&source, &blobs[original_slot == CONVERSATION_CONFIG_RECORD_SLOT_A ? 0 : 1],
                  sizeof(source)) == 0);
    simulate_restart();
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-newer", "latest.cn-beijing.maas.aliyuncs.com", false, 3U,
                    CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(s_config.voice == preferences.voice && s_config.reply_style == preferences.reply_style);
    const unsigned previous_writes = writes;
    preferences.voice = CONVERSATION_VOICE_COUNT;
    assert(conversation_config_save_preferences(&preferences) == ESP_ERR_INVALID_ARG);
    assert(conversation_config_save_preferences(NULL) == ESP_ERR_INVALID_ARG);
    assert(writes == previous_writes);
    assert_snapshot("sk-newer", "latest.cn-beijing.maas.aliyuncs.com", false, 3U,
                    CONVERSATION_CONFIG_MIGRATION_NONE);
}

static void test_preferences_failure_preserves_source_and_pending_migration(void)
{
    const conversation_preferences_t preferences = {
        .enabled = true,
        .voice = CONVERSATION_VOICE_LONGANLINGXIN,
        .reply_style = CONVERSATION_REPLY_STYLE_ENGLISH_PRACTICE,
        .model = CONVERSATION_DEFAULT_MODEL,
        .has_model = true,
        .has_api_host = true,
        .api_host = "preferences.cn-beijing.maas.aliyuncs.com",
    };
    for (unsigned fault = 0U; fault < 4U; ++fault) {
        reset_fixture();
        seed_record(0U, 9U, false, false, "sk-before-fault", "");
        assert(conversation_config_init() == ESP_OK);
        const test_blob_t source = blobs[0];
        fail_open = fault == 0U;
        fail_set = fault == 1U;
        fail_commit = fault >= 2U;
        commit_before_failure = fault == 3U;
        assert(conversation_config_save_preferences(&preferences) == ESP_FAIL);
        assert_snapshot("sk-before-fault", "", false, 9U, CONVERSATION_CONFIG_MIGRATION_NONE);
        assert(s_config.voice == CONVERSATION_DEFAULT_VOICE);
        assert(s_config.reply_style == CONVERSATION_DEFAULT_REPLY_STYLE);
        assert(memcmp(&source, &blobs[0], sizeof(source)) == 0);
        fail_open = fail_set = fail_commit = commit_before_failure = false;
        simulate_restart();
        assert(conversation_config_init() == ESP_OK);
        assert_snapshot("sk-before-fault", fault == 3U ? preferences.api_host : "",
                        fault == 3U, fault == 3U ? 10U : 9U,
                        CONVERSATION_CONFIG_MIGRATION_NONE);
        assert(s_config.voice == (fault == 3U ? preferences.voice : CONVERSATION_DEFAULT_VOICE));
    }
    reset_fixture();
    seed_record(0U, 20U, true, false, "sk-pending-preferences", "");
    fail_set = true;
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-pending-preferences", "", false, 20U,
                    CONVERSATION_CONFIG_MIGRATION_PENDING);
    fail_set = false;
    assert(conversation_config_save_preferences(&preferences) == ESP_OK);
    assert_snapshot("sk-pending-preferences", preferences.api_host, true, 21U,
                    CONVERSATION_CONFIG_MIGRATION_COMPLETED);
    assert(s_config.voice == preferences.voice && s_config.reply_style == preferences.reply_style);
}

static void test_credential_save_preserves_latest_preference_save(void)
{
    reset_fixture();
    assert(conversation_config_init() == ESP_OK);
    static const char form[] = "service=aliyun_realtime&"
        "model=qwen-audio-3.0-realtime-flash&api_key=sk-credential-form&api_host=";
    conversation_config_update_t credentials;
    assert(conversation_config_parse_form(form, strlen(form), &credentials) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(conversation_config_save(&credentials) == ESP_OK);
    assert_snapshot("sk-credential-form", "", false, 1U, CONVERSATION_CONFIG_MIGRATION_NONE);
    /* A credential editor holding an older form cannot undo a newer on/off
     * or voice save, regardless of the order the two forms were opened. */
    const conversation_preferences_t preferences = {
        .enabled = true,
        .voice = CONVERSATION_VOICE_LONGANXIAOXIN,
        .reply_style = CONVERSATION_REPLY_STYLE_ENGLISH_PRACTICE,
    };
    assert(conversation_config_save_preferences(&preferences) == ESP_OK);
    snprintf(credentials.api_key, sizeof(credentials.api_key), "%s", "sk-rotated");
    snprintf(credentials.api_host, sizeof(credentials.api_host), "%s",
             "rotated.ap-southeast-1.maas.aliyuncs.com");
    assert(conversation_config_save(&credentials) == ESP_OK);
    assert_snapshot("sk-rotated", "rotated.ap-southeast-1.maas.aliyuncs.com", true, 3U,
                    CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(s_config.voice == preferences.voice && s_config.reply_style == preferences.reply_style);
    simulate_restart();
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-rotated", "rotated.ap-southeast-1.maas.aliyuncs.com", true, 3U,
                    CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(s_config.voice == preferences.voice && s_config.reply_style == preferences.reply_style);
    conversation_config_clear_sensitive(&credentials, sizeof(credentials));
}

static void test_independent_api_key_and_preferences_saves(void)
{
    reset_fixture();
    conversation_api_key_update_t key = {0};
    assert(conversation_config_save_api_key(NULL) == ESP_ERR_INVALID_ARG);
    assert(conversation_config_save_api_key(&key) == ESP_ERR_INVALID_STATE);
    assert(conversation_config_init() == ESP_OK);
    assert(conversation_config_save_api_key(&key) == ESP_ERR_INVALID_STATE);
    assert(writes == 0U && commits == 0U);
    snprintf(key.api_key, sizeof(key.api_key), "%s", "sk-key-only-first");
    assert(conversation_config_save_api_key(&key) == ESP_OK);
    assert_snapshot("sk-key-only-first", "", false, 1U, CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(s_config.voice == CONVERSATION_DEFAULT_VOICE);
    assert(s_config.reply_style == CONVERSATION_DEFAULT_REPLY_STYLE);

    /* A key form drafted before a newer preferences save must merge only the
     * key into that latest state, including its newly changed Host. */
    snprintf(key.api_key, sizeof(key.api_key), "%s", "sk-key-only-newer");
    conversation_preferences_t preferences = {
        .enabled = true,
        .voice = CONVERSATION_VOICE_LONGANLUFENG,
        .reply_style = CONVERSATION_REPLY_STYLE_ENGLISH_PRACTICE,
        .model = CONVERSATION_DEFAULT_MODEL,
        .has_model = true,
        .has_api_host = true,
        .api_host = "latest.ap-southeast-1.maas.aliyuncs.com",
    };
    assert(conversation_config_save_preferences(&preferences) == ESP_OK);
    assert_snapshot("sk-key-only-first", preferences.api_host, true, 2U,
                    CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(conversation_config_save_api_key(&key) == ESP_OK);
    assert_snapshot("sk-key-only-newer", preferences.api_host, true, 3U,
                    CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(s_config.voice == preferences.voice && s_config.reply_style == preferences.reply_style);

    /* A new non-secret form may update the Host but never replace a newer
     * key. A legacy three-field form then preserves that latest Host/model. */
    snprintf(preferences.api_host, sizeof(preferences.api_host), "%s",
             "DASHSCOPE-INTL.ALIYUNCS.COM");
    assert(conversation_config_save_preferences(&preferences) == ESP_OK);
    assert_snapshot("sk-key-only-newer", "dashscope-intl.aliyuncs.com", true, 4U,
                    CONVERSATION_CONFIG_MIGRATION_NONE);
    preferences.has_model = false;
    preferences.has_api_host = false;
    preferences.model = CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME;
    snprintf(preferences.api_host, sizeof(preferences.api_host), "%s", "ignored.example");
    preferences.enabled = false;
    assert(conversation_config_save_preferences(&preferences) == ESP_OK);
    assert_snapshot("sk-key-only-newer", "dashscope-intl.aliyuncs.com", false, 5U,
                    CONVERSATION_CONFIG_MIGRATION_NONE);
    memset(&key, 0, sizeof(key));
    assert(conversation_config_save_api_key(&key) == ESP_OK);
    assert_snapshot("sk-key-only-newer", "dashscope-intl.aliyuncs.com", false, 6U,
                    CONVERSATION_CONFIG_MIGRATION_NONE);
    const unsigned previous_writes = writes;
    const unsigned previous_commits = commits;
    const conversation_config_record_slot_t source_slot = s_active_slot;
    const conversation_config_t current = s_config;
    memset(key.api_key, 'x', sizeof(key.api_key));
    assert(conversation_config_save_api_key(&key) == ESP_ERR_INVALID_ARG);
    snprintf(key.api_key, sizeof(key.api_key), "%s", "sk-invalid key");
    assert(conversation_config_save_api_key(&key) == ESP_ERR_INVALID_ARG);
    preferences.has_api_host = true;
    assert(conversation_config_save_preferences(&preferences) == ESP_ERR_INVALID_ARG);
    preferences.has_model = true;
    preferences.model = CONVERSATION_DEFAULT_MODEL;
    assert(conversation_config_save_preferences(&preferences) == ESP_ERR_INVALID_ARG);
    assert(writes == previous_writes && commits == previous_commits);
    assert(s_active_slot == source_slot && memcmp(&current, &s_config, sizeof(current)) == 0);
    simulate_restart();
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-key-only-newer", "dashscope-intl.aliyuncs.com", false, 6U,
                    CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(s_config.voice == preferences.voice && s_config.reply_style == preferences.reply_style);
    conversation_config_clear_sensitive(&key, sizeof(key));
}

static void test_api_key_failure_preserves_source_and_pending_migration(void)
{
    conversation_api_key_update_t key = { .api_key = "sk-replaced-key-only" };
    for (unsigned fault = 0U; fault < 4U; ++fault) {
        reset_fixture();
        seed_record(0U, 19U, false, true, "sk-before-key-fault",
                    "keep.cn-beijing.maas.aliyuncs.com");
        assert(conversation_config_init() == ESP_OK);
        const test_blob_t source = blobs[0];
        fail_open = fault == 0U;
        fail_set = fault == 1U;
        fail_commit = fault >= 2U;
        commit_before_failure = fault == 3U;
        assert(conversation_config_save_api_key(&key) == ESP_FAIL);
        assert_snapshot("sk-before-key-fault", "keep.cn-beijing.maas.aliyuncs.com", true,
                        19U, CONVERSATION_CONFIG_MIGRATION_NONE);
        assert(s_active_slot == CONVERSATION_CONFIG_RECORD_SLOT_A);
        assert(memcmp(&source, &blobs[0], sizeof(source)) == 0);
        assert(s_config.voice == CONVERSATION_DEFAULT_VOICE);
        assert(s_config.reply_style == CONVERSATION_DEFAULT_REPLY_STYLE);
        fail_open = fail_set = fail_commit = commit_before_failure = false;
        simulate_restart();
        assert(conversation_config_init() == ESP_OK);
        assert_snapshot(fault == 3U ? key.api_key : "sk-before-key-fault",
                        "keep.cn-beijing.maas.aliyuncs.com", true,
                        fault == 3U ? 20U : 19U, CONVERSATION_CONFIG_MIGRATION_NONE);
    }
    reset_fixture();
    seed_record(0U, 8U, true, false, "sk-before-key-migration", "");
    fail_set = true;
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot("sk-before-key-migration", "", false, 8U,
                    CONVERSATION_CONFIG_MIGRATION_PENDING);
    fail_set = false;
    assert(conversation_config_save_api_key(&key) == ESP_OK);
    assert_snapshot(key.api_key, "", false, 9U, CONVERSATION_CONFIG_MIGRATION_COMPLETED);
    assert(s_config.voice == CONVERSATION_DEFAULT_VOICE);
    assert(s_config.reply_style == CONVERSATION_DEFAULT_REPLY_STYLE);
    simulate_restart();
    assert(conversation_config_init() == ESP_OK);
    assert_snapshot(key.api_key, "", false, 9U, CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(conversation_config_clear() == ESP_OK);
    memset(&key, 0, sizeof(key));
    assert(conversation_config_save_api_key(&key) == ESP_ERR_INVALID_STATE);
    const conversation_preferences_t preferences = {
        .enabled = false,
        .voice = CONVERSATION_DEFAULT_VOICE,
        .reply_style = CONVERSATION_DEFAULT_REPLY_STYLE,
        .model = CONVERSATION_DEFAULT_MODEL,
        .has_model = true,
        .has_api_host = true,
    };
    assert(conversation_config_save_preferences(&preferences) == ESP_ERR_INVALID_STATE);
    assert(!blobs[0].present && !blobs[1].present);
    conversation_config_clear_sensitive(&key, sizeof(key));
}

int main(void)
{
    test_new_install_and_current_model_are_not_rewritten();
    test_migration_preserves_credentials_and_disabled_state();
    test_corrupt_unknown_and_recoverable_slots();
    test_generation_wrap_and_equal_generations();
    test_legacy_decoder_still_validates_all_other_fields();
    test_migration_write_failure_boots_and_retries();
    test_pending_migration_save_and_clear();
    test_clear_save_uses_a_new_configuration_identity();
    test_storage_read_errors_do_not_mutate_records();
    test_schema_two_audio_model_upgrade_and_options_persistence();
    test_invalid_option_record_keeps_older_valid_slot();
    test_preferences_preserve_latest_credentials();
    test_preferences_failure_preserves_source_and_pending_migration();
    test_credential_save_preserves_latest_preference_save();
    test_independent_api_key_and_preferences_saves();
    test_api_key_failure_preserves_source_and_pending_migration();
    puts("conversation storage migration tests passed");
    return 0;
}
