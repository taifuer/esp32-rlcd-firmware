/* Host fault injection for our settings service; the fake storage models
 * commit boundaries, not the ESP-IDF NVS implementation or real power loss. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/settings/app_settings.c"

typedef struct {
    char key[16];
    uint8_t bytes[SETTINGS_RECORD_ENCODED_SIZE];
    size_t size;
} test_blob_t;

static test_blob_t blobs[12];
static test_blob_t pending_blob;
static uint16_t persisted_schema;
static uint16_t pending_schema;
static unsigned commits;
static unsigned fail_commit;
static bool lock_held;

void settings_test_log(const char *tag, const char *format, ...)
{
    (void)tag;
    (void)format;
}
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test error"; }
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

static test_blob_t *find_blob(const char *key, bool create)
{
    for (size_t index = 0U; index < 12U; ++index) {
        if (strcmp(blobs[index].key, key) == 0) { return &blobs[index]; }
    }
    if (create) {
        for (size_t index = 0U; index < 12U; ++index) {
            if (blobs[index].key[0] == '\0') {
                snprintf(blobs[index].key, sizeof(blobs[index].key), "%s", key);
                return &blobs[index];
            }
        }
    }
    return NULL;
}

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(lock_held && strcmp(name, "rlcd_cfg") == 0 && mode == NVS_READWRITE);
    *handle = 1U;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle)
{
    assert(handle == 1U);
    pending_blob = (test_blob_t){0};
    pending_schema = 0U;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(lock_held && handle == 1U);
    if (++commits == fail_commit) { return ESP_FAIL; }
    if (pending_blob.key[0] != '\0') {
        test_blob_t *target = find_blob(pending_blob.key, true);
        assert(target != NULL);
        *target = pending_blob;
        pending_blob = (test_blob_t){0};
    }
    if (pending_schema != 0U) {
        persisted_schema = pending_schema;
        pending_schema = 0U;
    }
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *size)
{
    assert(lock_held && handle == 1U);
    test_blob_t *blob = find_blob(key, false);
    if (blob == NULL) { return ESP_ERR_NVS_NOT_FOUND; }
    if (value != NULL && *size < blob->size) { return ESP_ERR_NVS_INVALID_LENGTH; }
    *size = blob->size;
    if (value != NULL) { memcpy(value, blob->bytes, blob->size); }
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size)
{
    assert(lock_held && handle == 1U && size <= sizeof(pending_blob.bytes));
    snprintf(pending_blob.key, sizeof(pending_blob.key), "%s", key);
    memcpy(pending_blob.bytes, value, size);
    pending_blob.size = size;
    return ESP_OK;
}
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *value)
{
    assert(lock_held && handle == 1U && strcmp(key, "schema") == 0);
    *value = persisted_schema;
    return persisted_schema == 0U ? ESP_ERR_NVS_NOT_FOUND : ESP_OK;
}
esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t value)
{
    assert(lock_held && handle == 1U && strcmp(key, "schema") == 0);
    pending_schema = value;
    return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    (void)handle; (void)key; (void)value;
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_get_i16(nvs_handle_t handle, const char *key, int16_t *value)
{
    (void)handle; (void)key; (void)value;
    return ESP_ERR_NVS_NOT_FOUND;
}

static void simulate_restart(void)
{
    s_initialized = false;
    s_future_schema = false;
    s_mutex = NULL;
    s_settings = (app_settings_t){0};
    s_active_slot = SETTINGS_RECORD_SLOT_NONE;
    s_generation = 0U;
    lock_held = false;
    pending_blob = (test_blob_t){0};
    pending_schema = 0U;
}

static void update_test_checksum(test_blob_t *blob)
{
    uint32_t crc = UINT32_MAX;
    for (size_t byte = 0U; byte < 28U; ++byte) {
        crc ^= blob->bytes[byte];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & (0U - (crc & 1U)));
        }
    }
    crc ^= UINT32_MAX;
    for (unsigned byte = 0U; byte < 4U; ++byte) {
        blob->bytes[28U + byte] = (uint8_t)(crc >> (8U * byte));
    }
}

static void seed_schema6(void)
{
    memset(blobs, 0, sizeof(blobs));
    persisted_schema = 6U;
    commits = 0U;
    fail_commit = 0U;
    simulate_restart();
    for (unsigned index = 0U; index < 2U; ++index) {
        app_settings_t settings;
        app_settings_defaults(&settings);
        settings.manual_saving_requested = true;
        settings.audio_playback_volume = (uint8_t)(39U + index);
        settings.utc_offset_minutes = -300;
        settings.update_channel = APP_UPDATE_CHANNEL_BETA;
        settings.alarm_enabled = true;
        settings.alarm_hour = 23U;
        settings.alarm_minute = 42U;
        settings.alarm_weekdays = APP_SETTINGS_ALARM_WEEKENDS_MASK;
        test_blob_t *blob = find_blob(index == 0U ? "cfg6_a" : "cfg6_b", true);
        blob->size = sizeof(blob->bytes);
        assert(settings_record_encode(19U + index, &settings, blob->bytes, blob->size));
        blob->bytes[4] = 5U;
        blob->bytes[12] = 6U;
        update_test_checksum(blob);
    }
}

static void assert_migrated(void)
{
    app_settings_t settings;
    assert(app_settings_get(&settings) == ESP_OK);
    assert(settings.schema_version == 7U && persisted_schema == 7U);
    assert(settings.audio_playback_volume == 40U);
    assert(settings.manual_saving_requested && settings.utc_offset_minutes == -300);
    assert(settings.update_channel == APP_UPDATE_CHANNEL_BETA);
    assert(settings.alarm_enabled && settings.alarm_hour == 23U);
    assert(settings.alarm_minute == 42U &&
           settings.alarm_weekdays == APP_SETTINGS_ALARM_WEEKENDS_MASK);
    assert(find_blob("cfg6_b", false)->bytes[12] == 6U); /* old slots untouched */
}

static void test_migration_retry(void)
{
    /* Fail either new slot write or the final schema marker commit. */
    for (unsigned failure = 1U; failure <= 3U; ++failure) {
        seed_schema6();
        fail_commit = failure;
        assert(app_settings_init() == ESP_FAIL);
        assert(!s_initialized && persisted_schema == 6U);
        simulate_restart();
        fail_commit = 0U;
        assert(app_settings_init() == ESP_OK);
        assert_migrated();
        simulate_restart();
        assert(app_settings_init() == ESP_OK);
        assert_migrated();
    }
    seed_schema6();
    persisted_schema = 0U; /* lost marker: newest valid legacy pair wins */
    assert(app_settings_init() == ESP_OK);
    assert_migrated();
}

static void test_scoped_save_failure_noop_and_restart(void)
{
    seed_schema6();
    assert(app_settings_init() == ESP_OK);
    app_settings_snapshot_t before, after;
    assert(app_settings_get_snapshot(&before) == ESP_OK);
    unsigned previous_commits = commits;
    assert(app_settings_save_field(APP_SETTING_VOLUME, 40U) == ESP_OK);
    assert(commits == previous_commits);
    fail_commit = commits + 1U;
    assert(app_settings_save_field(APP_SETTING_VOLUME, 0U) == ESP_FAIL);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.generation == before.generation);
    assert(after.settings.audio_playback_volume == 40U);
    fail_commit = 0U;
    assert(app_settings_save_field(APP_SETTING_ALARM_ENABLED, 0U) == ESP_OK);
    assert(app_settings_save_field(APP_SETTING_VOLUME, 70U) == ESP_OK);
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.settings.audio_playback_volume == 70U && !after.settings.alarm_enabled);
    assert(after.settings.alarm_hour == 23U && after.settings.alarm_minute == 42U);
    assert(after.settings.manual_saving_requested && after.settings.utc_offset_minutes == -300);
    previous_commits = commits;
    assert(app_settings_save_field((app_setting_field_t)2, 1U) == ESP_ERR_INVALID_ARG);
    assert(commits == previous_commits);

    persisted_schema = 8U;
    simulate_restart();
    assert(app_settings_init() == ESP_ERR_NOT_SUPPORTED);
    assert(app_settings_save_field(APP_SETTING_VOLUME, 90U) == ESP_ERR_NOT_SUPPORTED);
    assert(commits == previous_commits && persisted_schema == 8U);
}

static void test_dev1_display_is_ignored_without_reset_or_startup_writes(void)
{
    for (uint8_t old_display = 0U; old_display <= 2U; ++old_display) {
        seed_schema6();
        assert(app_settings_init() == ESP_OK);
        app_settings_snapshot_t before, after;
        assert(app_settings_get_snapshot(&before) == ESP_OK);
        test_blob_t *slot_a = find_blob("cfg7_a", false);
        test_blob_t *slot_b = find_blob("cfg7_b", false);
        assert(slot_a != NULL && slot_b != NULL);
        slot_a->bytes[25] = old_display;
        slot_b->bytes[25] = old_display;
        update_test_checksum(slot_a);
        update_test_checksum(slot_b);

        const unsigned previous_commits = commits;
        simulate_restart();
        assert(app_settings_init() == ESP_OK);
        assert(app_settings_get_snapshot(&after) == ESP_OK);
        assert(commits == previous_commits && persisted_schema == 7U);
        assert(after.generation == before.generation);
        assert(settings_equal(&before.settings, &after.settings));
        assert(slot_a->bytes[25] == old_display && slot_b->bytes[25] == old_display);

        assert(app_settings_save_field(APP_SETTING_VOLUME, 50U) == ESP_OK);
        test_blob_t *written = s_active_slot == SETTINGS_RECORD_SLOT_A ? slot_a : slot_b;
        assert(written->bytes[25] == 0U);
        simulate_restart();
        assert(app_settings_init() == ESP_OK);
        assert(app_settings_get_snapshot(&after) == ESP_OK);
        before.settings.audio_playback_volume = 50U;
        assert(settings_equal(&before.settings, &after.settings));
    }
}

int main(void)
{
    test_migration_retry();
    test_scoped_save_failure_noop_and_restart();
    test_dev1_display_is_ignored_without_reset_or_startup_writes();
    puts("settings service migration, fault injection and scoped persistence tests passed");
}
