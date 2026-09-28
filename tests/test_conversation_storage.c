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
    /* Published format 2/schema 2 uses model byte 15. The retired enum ID is
     * still 0; no newly written config is allowed to encode that ID. */
    assert(blobs[slot].bytes[4] == 2U && blobs[slot].bytes[12] == 2U);
    assert(blobs[slot].bytes[15] == 1U);
    if (legacy) {
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
    puts("conversation storage migration tests passed");
    return 0;
}
