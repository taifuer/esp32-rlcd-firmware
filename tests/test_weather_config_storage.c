/* Exercise the real storage service with fake NVS commit outcomes. The stubs
 * model atomic records and error paths, not ESP-IDF internals or power loss. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/weather/weather_config.c"

typedef struct {
    bool present;
    uint8_t bytes[WEATHER_CONFIG_RECORD_ENCODED_SIZE];
} test_blob_t;

static test_blob_t blobs[2], pending_blob;
static bool lock_held, handle_open, pending_write, pending_erase;
static bool fail_open, fail_set, fail_commit, commit_before_failure;
static unsigned pending_slot, writes, commits;

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
    assert(strcmp(name, "rlcd_weather") == 0 && mode == NVS_READWRITE);
    if (fail_open) return ESP_FAIL;
    handle_open = true;
    *handle = 1U;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(lock_held && handle_open && handle == 1U);
    handle_open = pending_write = pending_erase = false;
    weather_config_clear_sensitive(&pending_blob, sizeof(pending_blob));
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *size)
{
    assert(lock_held && handle_open && handle == 1U);
    const test_blob_t *blob = &blobs[slot_index(key)];
    if (!blob->present) return ESP_ERR_NVS_NOT_FOUND;
    if (value != NULL && *size < sizeof(blob->bytes)) return ESP_ERR_NVS_INVALID_LENGTH;
    *size = sizeof(blob->bytes);
    if (value != NULL) memcpy(value, blob->bytes, sizeof(blob->bytes));
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size)
{
    assert(lock_held && handle_open && handle == 1U && !pending_write);
    assert(size == sizeof(pending_blob.bytes));
    ++writes;
    if (fail_set) return ESP_FAIL;
    pending_slot = slot_index(key);
    pending_blob.present = pending_write = true;
    memcpy(pending_blob.bytes, value, size);
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle)
{
    assert(lock_held && handle_open && handle == 1U);
    pending_erase = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(lock_held && handle_open && handle == 1U);
    ++commits;
    if (!fail_commit || commit_before_failure) {
        if (pending_erase) memset(blobs, 0, sizeof(blobs));
        if (pending_write) blobs[pending_slot] = pending_blob;
    }
    return fail_commit ? ESP_FAIL : ESP_OK;
}

static void restart(void)
{
    assert(!lock_held && !handle_open);
    s_initialized = false;
    s_mutex = NULL;
    weather_config_reset(&s_config);
    s_active_slot = WEATHER_CONFIG_RECORD_SLOT_NONE;
    s_generation = 0U;
}

static void reset_fixture(void)
{
    restart();
    memset(blobs, 0, sizeof(blobs));
    memset(&pending_blob, 0, sizeof(pending_blob));
    fail_open = fail_set = fail_commit = commit_before_failure = false;
    pending_write = pending_erase = false;
    writes = commits = 0U;
}

static const weather_preferences_t shanghai = {
    .enabled = true, .api_host = "SH.QWEATHERAPI.COM",
    .province_id = 310000U, .city_id = 310000U,
};
static const weather_preferences_t nanjing = {
    .enabled = false, .api_host = "NJ.QWEATHERAPI.COM",
    .province_id = 320000U, .city_id = 320100U,
};

static void assert_snapshot(const char *key, const char *host, bool enabled,
                            uint32_t province, uint32_t city, uint32_t generation)
{
    weather_config_snapshot_t snapshot;
    assert(weather_config_get_snapshot(&snapshot) == ESP_OK);
    assert(strcmp(snapshot.config.api_key, key) == 0);
    assert(strcmp(snapshot.config.api_host, host) == 0);
    assert(snapshot.config.enabled == enabled);
    assert(snapshot.config.province_id == province && snapshot.config.city_id == city);
    assert(snapshot.generation == generation);
    assert(snapshot.config.district[0] == '\0');
    weather_config_clear_sensitive(&snapshot, sizeof(snapshot));
}

static void test_initial_key_and_interleaved_independent_saves(void)
{
    reset_fixture();
    weather_api_key_update_t key = {.api_key = "key-first"};
    assert(weather_config_save_api_key(&key) == ESP_ERR_INVALID_STATE);
    assert(weather_config_save_preferences(&shanghai) == ESP_ERR_INVALID_STATE);
    assert(weather_config_init() == ESP_OK);
    assert(writes == 0U && commits == 0U);
    const weather_api_key_update_t empty_key = {0};
    assert(weather_config_save_api_key(&empty_key) == ESP_ERR_INVALID_STATE);
    assert(writes == 0U && commits == 0U);
    assert_snapshot("", "", false, 0U, 0U, 0U);
    assert(weather_config_save_preferences(&shanghai) == ESP_ERR_INVALID_STATE);
    assert(writes == 0U && commits == 0U);
    assert(weather_config_save_api_key(&key) == ESP_OK);
    assert_snapshot("key-first", "", false, 0U, 0U, 1U);
    restart();
    assert(weather_config_init() == ESP_OK);
    assert_snapshot("key-first", "", false, 0U, 0U, 1U);
    assert(weather_config_save_preferences(&shanghai) == ESP_OK);
    assert_snapshot("key-first", "sh.qweatherapi.com", true, 310000U, 310000U, 2U);

    /* A key editor opened before the latest preferences cannot undo them. */
    strcpy(key.api_key, "key-rotated");
    assert(weather_config_save_preferences(&nanjing) == ESP_OK);
    assert(weather_config_save_api_key(&key) == ESP_OK);
    assert_snapshot("key-rotated", "nj.qweatherapi.com", false, 320000U, 320100U, 4U);
    /* A preference form opened before key rotation must use the latest key. */
    strcpy(key.api_key, "key-newest");
    assert(weather_config_save_api_key(&key) == ESP_OK);
    assert(weather_config_save_preferences(&shanghai) == ESP_OK);
    assert_snapshot("key-newest", "sh.qweatherapi.com", true, 310000U, 310000U, 6U);
    key.api_key[0] = '\0';
    assert(weather_config_save_api_key(&key) == ESP_OK);
    assert_snapshot("key-newest", "sh.qweatherapi.com", true, 310000U, 310000U, 7U);
    restart();
    assert(weather_config_init() == ESP_OK);
    assert_snapshot("key-newest", "sh.qweatherapi.com", true, 310000U, 310000U, 7U);

    const unsigned writes_before = writes;
    weather_preferences_t invalid = shanghai;
    invalid.city_id = 320100U;
    assert(weather_config_save_preferences(&invalid) == ESP_ERR_INVALID_ARG);
    strcpy(key.api_key, "invalid key");
    assert(weather_config_save_api_key(&key) == ESP_ERR_INVALID_ARG);
    assert(weather_config_save_api_key(NULL) == ESP_ERR_INVALID_ARG);
    assert(weather_config_save_preferences(NULL) == ESP_ERR_INVALID_ARG);
    assert(writes == writes_before);
    assert_snapshot("key-newest", "sh.qweatherapi.com", true, 310000U, 310000U, 7U);
}

static void test_failure_preserves_source_and_recovers(void)
{
    for (unsigned kind = 0U; kind < 2U; ++kind) {
        for (unsigned fault = 0U; fault < 4U; ++fault) {
            reset_fixture();
            assert(weather_config_init() == ESP_OK);
            weather_api_key_update_t key = {.api_key = "key-original"};
            assert(weather_config_save_api_key(&key) == ESP_OK);
            assert(weather_config_save_preferences(&shanghai) == ESP_OK);
            const unsigned source_slot = s_active_slot == WEATHER_CONFIG_RECORD_SLOT_A ? 0U : 1U;
            const test_blob_t source = blobs[source_slot];
            fail_open = fault == 0U;
            fail_set = fault == 1U;
            fail_commit = fault >= 2U;
            commit_before_failure = fault == 3U;
            strcpy(key.api_key, "key-replacement");
            assert((kind == 0U ? weather_config_save_preferences(&nanjing)
                              : weather_config_save_api_key(&key)) == ESP_FAIL);
            assert_snapshot("key-original", "sh.qweatherapi.com", true, 310000U, 310000U, 2U);
            assert(memcmp(&source, &blobs[source_slot], sizeof(source)) == 0);
            fail_open = fail_set = fail_commit = commit_before_failure = false;
            restart();
            assert(weather_config_init() == ESP_OK);
            const bool committed = fault == 3U;
            const bool changed_preferences = committed && kind == 0U;
            assert_snapshot(committed && kind == 1U ? "key-replacement" : "key-original",
                changed_preferences ? "nj.qweatherapi.com" : "sh.qweatherapi.com",
                !changed_preferences, changed_preferences ? 320000U : 310000U,
                changed_preferences ? 320100U : 310000U, committed ? 3U : 2U);
        }
    }
}

static void test_legacy_records_and_full_save_remain_compatible(void)
{
    reset_fixture();
    assert(weather_config_init() == ESP_OK);
    const weather_config_update_t full = {
        .enabled = true, .api_key = "legacy-key", .api_host = "OLD.QWEATHERAPI.COM",
        .province_id = 310000U, .city_id = 310100U, .district = "legacy",
    };
    assert(weather_config_save(&full) == ESP_OK);
    assert_snapshot("legacy-key", "old.qweatherapi.com", true, 310000U, 310100U, 1U);
    /* Schema v1 continues to load an older district, without rewriting. */
    weather_config_t legacy = s_config;
    strcpy(legacy.district, "legacy district");
    assert(weather_config_record_encode(9U, &legacy, blobs[0].bytes, sizeof(blobs[0].bytes)));
    blobs[0].present = true;
    restart();
    const unsigned writes_before = writes;
    assert(weather_config_init() == ESP_OK);
    assert(writes == writes_before && strcmp(s_config.district, "legacy district") == 0);
    const weather_api_key_update_t key = {.api_key = "new-key"};
    assert(weather_config_save_api_key(&key) == ESP_OK);
    assert(strcmp(s_config.district, "legacy district") == 0);
    assert(weather_config_save_preferences(&nanjing) == ESP_OK);
    assert_snapshot("new-key", "nj.qweatherapi.com", false, 320000U, 320100U, 11U);
    assert(weather_config_clear() == ESP_OK);
    restart();
    assert(weather_config_init() == ESP_OK);
    assert_snapshot("", "", false, 0U, 0U, 0U);
}

int main(void)
{
    test_initial_key_and_interleaved_independent_saves();
    test_failure_preserves_source_and_recovers();
    test_legacy_records_and_full_save_remain_compatible();
    puts("weather config storage tests passed");
    return 0;
}
