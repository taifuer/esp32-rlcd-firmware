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
    const size_t checksum_offset = blob->size - 4U;
    uint32_t crc = UINT32_MAX;
    for (size_t byte = 0U; byte < checksum_offset; ++byte) {
        crc ^= blob->bytes[byte];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & (0U - (crc & 1U)));
        }
    }
    crc ^= UINT32_MAX;
    for (unsigned byte = 0U; byte < 4U; ++byte) {
        blob->bytes[checksum_offset + byte] = (uint8_t)(crc >> (8U * byte));
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
        blob->size = SETTINGS_RECORD_SCHEMA6_ENCODED_SIZE;
        blob->bytes[4] = 5U;
        blob->bytes[6] = (uint8_t)blob->size;
        blob->bytes[12] = 6U;
        update_test_checksum(blob);
    }
}

static void assert_migrated(void)
{
    app_settings_t settings;
    assert(app_settings_get(&settings) == ESP_OK);
    assert(settings.schema_version == APP_SETTINGS_SCHEMA_VERSION && persisted_schema == APP_SETTINGS_SCHEMA_VERSION);
    assert(settings.audio_playback_volume == 40U);
    assert(settings.alarm_volume == 40U);
    assert(settings.daily_pages_mask == APP_DAILY_PAGES_ALL_MASK);
    assert(settings.music_mode == APP_MUSIC_MODE_SEQUENTIAL);
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
    assert(after.settings.alarm_volume == 40U);
    assert(after.settings.alarm_hour == 23U && after.settings.alarm_minute == 42U);
    assert(after.settings.manual_saving_requested && after.settings.utc_offset_minutes == -300);
    previous_commits = commits;
    assert(app_settings_save_field((app_setting_field_t)2, 1U) == ESP_ERR_INVALID_ARG);
    assert(commits == previous_commits);

    persisted_schema = 12U;
    simulate_restart();
    assert(app_settings_init() == ESP_ERR_NOT_SUPPORTED);
    assert(app_settings_save_field(APP_SETTING_VOLUME, 90U) == ESP_ERR_NOT_SUPPORTED);
    assert(commits == previous_commits && persisted_schema == 12U);
}

static void test_dev1_display_is_ignored_without_reset_or_startup_writes(void)
{
    for (uint8_t old_display = 0U; old_display <= 2U; ++old_display) {
        seed_schema6();
        assert(app_settings_init() == ESP_OK);
        app_settings_snapshot_t before, after;
        assert(app_settings_get_snapshot(&before) == ESP_OK);
        test_blob_t *slot_a = find_blob("cfg11_a", false);
        test_blob_t *slot_b = find_blob("cfg11_b", false);
        assert(slot_a != NULL && slot_b != NULL);
        slot_a->bytes[25] = old_display;
        slot_b->bytes[25] = old_display;
        update_test_checksum(slot_a);
        update_test_checksum(slot_b);

        const unsigned previous_commits = commits;
        simulate_restart();
        assert(app_settings_init() == ESP_OK);
        assert(app_settings_get_snapshot(&after) == ESP_OK);
        assert(commits == previous_commits && persisted_schema == APP_SETTINGS_SCHEMA_VERSION);
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

static void test_schema7_alarm_migration_and_independent_save(void)
{
    for (unsigned failure = 0U; failure <= 3U; ++failure) {
        seed_schema6();
        /* Construct the actual old format and slots, including muted media. */
        test_blob_t *old = find_blob("cfg7_b", true);
        *old = *find_blob("cfg6_b", false);
        snprintf(old->key, sizeof(old->key), "cfg7_b");
        old->bytes[4] = 6U;
        old->bytes[12] = 7U;
        old->bytes[18] = 0U;
        old->bytes[26] = 0U;
        update_test_checksum(old);
        const test_blob_t before = *old;
        persisted_schema = 7U;
        fail_commit = failure;
        const esp_err_t result = app_settings_init();
        assert(result == (failure == 0U ? ESP_OK : ESP_FAIL));
        if (failure != 0U) {
            assert(persisted_schema == 7U);
            simulate_restart();
            fail_commit = 0U;
            assert(app_settings_init() == ESP_OK);
        }
        app_settings_t current;
        assert(app_settings_get(&current) == ESP_OK);
        assert(current.audio_playback_volume == 0U && current.alarm_volume == 0U);
        assert(current.manual_saving_requested && current.utc_offset_minutes == -300);
        assert(memcmp(old, &before, sizeof(before)) == 0);
        current.alarm_volume = 55U;
        assert(app_settings_save(&current) == ESP_OK);
        assert(app_settings_save_field(APP_SETTING_VOLUME, 20U) == ESP_OK);
        simulate_restart();
        assert(app_settings_init() == ESP_OK);
        assert(app_settings_get(&current) == ESP_OK);
        assert(current.audio_playback_volume == 20U && current.alarm_volume == 55U);
    }
}

static void seed_schema8(void)
{
    seed_schema6();
    persisted_schema = 8U;
    for (unsigned index = 0U; index < 2U; ++index) {
        app_settings_t settings;
        app_settings_defaults(&settings);
        settings.manual_saving_requested = true;
        settings.audio_playback_volume = (uint8_t)(16U + index);
        settings.alarm_volume = (uint8_t)(74U + index);
        settings.utc_offset_minutes = 345;
        settings.temperature_unit = APP_TEMPERATURE_UNIT_FAHRENHEIT;
        settings.update_channel = APP_UPDATE_CHANNEL_BETA;
        settings.alarm_enabled = true;
        settings.alarm_hour = 5U;
        settings.alarm_minute = 12U;
        settings.alarm_weekdays = APP_SETTINGS_ALARM_WEEKENDS_MASK;
        test_blob_t *blob = find_blob(index == 0U ? "cfg8_a" : "cfg8_b", true);
        blob->size = sizeof(blob->bytes);
        assert(settings_record_encode(99U + index, &settings, blob->bytes, blob->size));
        blob->size = SETTINGS_RECORD_SCHEMA8_ENCODED_SIZE;
        blob->bytes[4] = 7U;
        blob->bytes[6] = (uint8_t)blob->size;
        blob->bytes[12] = 8U;
        blob->bytes[27] = 0U;
        update_test_checksum(blob);
    }
}

static void assert_schema8_preferences(uint8_t volume, uint8_t alarm_volume)
{
    app_settings_t current;
    assert(app_settings_get(&current) == ESP_OK);
    assert(current.schema_version == APP_SETTINGS_SCHEMA_VERSION && persisted_schema == APP_SETTINGS_SCHEMA_VERSION);
    assert(current.audio_playback_volume == volume && current.alarm_volume == alarm_volume);
    assert(current.manual_saving_requested && current.utc_offset_minutes == 345);
    assert(current.temperature_unit == APP_TEMPERATURE_UNIT_FAHRENHEIT);
    assert(current.update_channel == APP_UPDATE_CHANNEL_BETA);
    assert(current.alarm_enabled && current.alarm_hour == 5U && current.alarm_minute == 12U);
    assert(current.alarm_weekdays == APP_SETTINGS_ALARM_WEEKENDS_MASK);
    assert(current.daily_pages_mask == APP_DAILY_PAGES_ALL_MASK);
    assert(current.music_mode == APP_MUSIC_MODE_SEQUENTIAL);
}

static void test_schema8_migration_and_commit_failures(void)
{
    for (unsigned failure = 0U; failure <= 3U; ++failure) {
        seed_schema8();
        const test_blob_t before_a = *find_blob("cfg8_a", false);
        const test_blob_t before_b = *find_blob("cfg8_b", false);
        fail_commit = failure;
        assert(app_settings_init() == (failure == 0U ? ESP_OK : ESP_FAIL));
        if (failure != 0U) {
            assert(!s_initialized && persisted_schema == 8U);
            simulate_restart();
            fail_commit = 0U;
            assert(app_settings_init() == ESP_OK);
        }
        assert_schema8_preferences(17U, 75U);
        assert(memcmp(&before_a, find_blob("cfg8_a", false), sizeof(before_a)) == 0);
        assert(memcmp(&before_b, find_blob("cfg8_b", false), sizeof(before_b)) == 0);
        const unsigned before_commits = commits;
        simulate_restart();
        assert(app_settings_init() == ESP_OK);
        assert_schema8_preferences(17U, 75U);
        assert(commits == before_commits);
    }
    seed_schema8();
    persisted_schema = 0U; /* Recover the latest v8 record without a marker. */
    assert(app_settings_init() == ESP_OK);
    assert_schema8_preferences(17U, 75U);

    seed_schema8();
    find_blob("cfg8_b", false)->bytes[26] ^= 1U;
    const test_blob_t corrupt = *find_blob("cfg8_b", false);
    assert(app_settings_init() == ESP_OK);
    assert_schema8_preferences(16U, 74U);
    assert(memcmp(&corrupt, find_blob("cfg8_b", false), sizeof(corrupt)) == 0);

    seed_schema8();
    find_blob("cfg8_a", false)->bytes[18] ^= 1U;
    find_blob("cfg8_b", false)->bytes[18] ^= 1U;
    assert(app_settings_init() == ESP_OK);
    assert_migrated(); /* Both v8 records corrupt: keep valid older v6 settings. */
}

static void seed_schema9(void)
{
    seed_schema8();
    persisted_schema = 9U;
    for (unsigned index = 0U; index < 2U; ++index) {
        test_blob_t *blob = find_blob(index == 0U ? "cfg9_a" : "cfg9_b", true);
        *blob = *find_blob(index == 0U ? "cfg8_a" : "cfg8_b", false);
        snprintf(blob->key, sizeof(blob->key), "%s", index == 0U ? "cfg9_a" : "cfg9_b");
        blob->bytes[4] = 8U;
        blob->bytes[12] = 9U;
        blob->bytes[27] = index == 0U ? APP_DAILY_PAGE_CALENDAR
                                      : APP_DAILY_PAGE_MUSIC | APP_DAILY_PAGE_CHAT;
        update_test_checksum(blob);
    }
}

static void assert_schema9_preferences(uint8_t volume, uint8_t alarm_volume,
                                       uint8_t pages)
{
    app_settings_t current;
    assert(app_settings_get(&current) == ESP_OK);
    assert(current.schema_version == APP_SETTINGS_SCHEMA_VERSION && persisted_schema == APP_SETTINGS_SCHEMA_VERSION);
    assert(current.audio_playback_volume == volume && current.alarm_volume == alarm_volume);
    assert(current.manual_saving_requested && current.utc_offset_minutes == 345);
    assert(current.temperature_unit == APP_TEMPERATURE_UNIT_FAHRENHEIT);
    assert(current.update_channel == APP_UPDATE_CHANNEL_BETA);
    assert(current.alarm_enabled && current.alarm_hour == 5U && current.alarm_minute == 12U);
    assert(current.alarm_weekdays == APP_SETTINGS_ALARM_WEEKENDS_MASK);
    assert(current.daily_pages_mask == pages);
    assert(current.music_mode == APP_MUSIC_MODE_SEQUENTIAL);
}

static void test_schema9_migration_and_commit_failures(void)
{
    for (unsigned failure = 0U; failure <= 3U; ++failure) {
        seed_schema9();
        const test_blob_t before_a = *find_blob("cfg9_a", false);
        const test_blob_t before_b = *find_blob("cfg9_b", false);
        const test_blob_t before8_a = *find_blob("cfg8_a", false);
        const test_blob_t before8_b = *find_blob("cfg8_b", false);
        fail_commit = failure;
        assert(app_settings_init() == (failure == 0U ? ESP_OK : ESP_FAIL));
        if (failure != 0U) {
            assert(!s_initialized && persisted_schema == 9U);
            simulate_restart();
            fail_commit = 0U;
            assert(app_settings_init() == ESP_OK);
        }
        assert_schema9_preferences(17U, 75U, APP_DAILY_PAGE_MUSIC | APP_DAILY_PAGE_CHAT);
        assert(memcmp(&before_a, find_blob("cfg9_a", false), sizeof(before_a)) == 0);
        assert(memcmp(&before_b, find_blob("cfg9_b", false), sizeof(before_b)) == 0);
        assert(memcmp(&before8_a, find_blob("cfg8_a", false), sizeof(before8_a)) == 0);
        assert(memcmp(&before8_b, find_blob("cfg8_b", false), sizeof(before8_b)) == 0);
        const unsigned before_commits = commits;
        simulate_restart();
        assert(app_settings_init() == ESP_OK);
        assert_schema9_preferences(17U, 75U, APP_DAILY_PAGE_MUSIC | APP_DAILY_PAGE_CHAT);
        assert(commits == before_commits);
    }
    seed_schema9();
    persisted_schema = 0U;
    assert(app_settings_init() == ESP_OK);
    assert_schema9_preferences(17U, 75U, APP_DAILY_PAGE_MUSIC | APP_DAILY_PAGE_CHAT);

    seed_schema9();
    find_blob("cfg9_b", false)->bytes[27] ^= APP_DAILY_PAGE_WEATHER;
    const test_blob_t corrupt = *find_blob("cfg9_b", false);
    assert(app_settings_init() == ESP_OK);
    assert_schema9_preferences(16U, 74U, APP_DAILY_PAGE_CALENDAR);
    assert(memcmp(&corrupt, find_blob("cfg9_b", false), sizeof(corrupt)) == 0);

    seed_schema9();
    find_blob("cfg9_a", false)->bytes[18] ^= 1U;
    find_blob("cfg9_b", false)->bytes[18] ^= 1U;
    assert(app_settings_init() == ESP_OK);
    assert_schema8_preferences(17U, 75U); /* Both v9 corrupt: valid v8 wins. */

    seed_schema9();
    find_blob("cfg9_b", false)->bytes[27] = 0U; /* All optional pages hidden is valid. */
    update_test_checksum(find_blob("cfg9_b", false));
    assert(app_settings_init() == ESP_OK);
    assert_schema9_preferences(17U, 75U, 0U);

    seed_schema9();
    assert(app_settings_init() == ESP_OK);
    find_blob("cfg11_a", false)->bytes[18] ^= 1U;
    find_blob("cfg11_b", false)->bytes[18] ^= 1U;
    simulate_restart();
    assert(app_settings_init() == ESP_OK); /* Current marker with both slots corrupt. */
    assert_schema9_preferences(17U, 75U, APP_DAILY_PAGE_MUSIC | APP_DAILY_PAGE_CHAT);

    /* Interrupted migration does not outrank newer edits made by old firmware
     * while its shared marker is still v9. Both partial v11 copies are repaired. */
    seed_schema9();
    fail_commit = 3U;
    assert(app_settings_init() == ESP_FAIL);
    test_blob_t *old = find_blob("cfg9_b", false);
    old->bytes[18] = 36U;
    old->bytes[27] = APP_DAILY_PAGE_WEATHER;
    update_test_checksum(old);
    simulate_restart();
    fail_commit = 0U;
    assert(app_settings_init() == ESP_OK);
    assert_schema9_preferences(36U, 75U, APP_DAILY_PAGE_WEATHER);
    for (unsigned index = 0U; index < 2U; ++index) {
        test_blob_t *blob = find_blob(index == 0U ? "cfg11_a" : "cfg11_b", false);
        settings_record_t decoded;
        assert(blob != NULL && blob->size == SETTINGS_RECORD_ENCODED_SIZE);
        assert(settings_record_decode(blob->bytes, blob->size, &decoded));
        assert(decoded.settings.audio_playback_volume == 36U);
        assert(decoded.settings.daily_pages_mask == APP_DAILY_PAGE_WEATHER);
    }
}

static void seed_schema10(void)
{
    seed_schema9();
    persisted_schema = 10U;
    for (unsigned index = 0U; index < 2U; ++index) {
        test_blob_t *blob = find_blob(index == 0U ? "cfg10_a" : "cfg10_b", true);
        *blob = *find_blob(index == 0U ? "cfg9_a" : "cfg9_b", false);
        snprintf(blob->key, sizeof(blob->key), "%s", index == 0U ? "cfg10_a" : "cfg10_b");
        blob->size = SETTINGS_RECORD_SCHEMA10_ENCODED_SIZE;
        blob->bytes[4] = 9U;
        blob->bytes[6] = (uint8_t)blob->size;
        blob->bytes[12] = 10U;
        blob->bytes[28] = index == 0U ? APP_MUSIC_MODE_REPEAT_ONE : APP_MUSIC_MODE_REPEAT_ALL;
        memset(blob->bytes + 29U, 0, 3U);
        update_test_checksum(blob);
    }
}

static void assert_schema10_preferences(const char *key)
{
    const test_blob_t *old = find_blob(key, false);
    settings_record_t expected;
    assert(settings_record_decode_schema10(old->bytes, old->size, &expected));
    app_settings_t current;
    assert(app_settings_get(&current) == ESP_OK);
    assert(persisted_schema == APP_SETTINGS_SCHEMA_VERSION);
    assert(settings_equal(&current, &expected.settings));
    const uint8_t defaults[] = APP_DAILY_PAGES_DEFAULT_ORDER;
    assert(memcmp(current.daily_pages_order, defaults, sizeof(defaults)) == 0);
}

static void test_schema10_migration_and_commit_failures(void)
{
    for (unsigned failure = 0U; failure <= 3U; ++failure) {
        seed_schema10();
        const test_blob_t old_a = *find_blob("cfg10_a", false);
        const test_blob_t old_b = *find_blob("cfg10_b", false);
        fail_commit = failure;
        assert(app_settings_init() == (failure == 0U ? ESP_OK : ESP_FAIL));
        if (failure != 0U) {
            assert(!s_initialized && persisted_schema == 10U);
            simulate_restart();
            fail_commit = 0U;
            assert(app_settings_init() == ESP_OK);
        }
        assert_schema10_preferences("cfg10_b");
        assert(memcmp(&old_a, find_blob("cfg10_a", false), sizeof(old_a)) == 0);
        assert(memcmp(&old_b, find_blob("cfg10_b", false), sizeof(old_b)) == 0);
        const unsigned previous_commits = commits;
        simulate_restart();
        assert(app_settings_init() == ESP_OK);
        assert_schema10_preferences("cfg10_b");
        assert(commits == previous_commits);
    }
    seed_schema10();
    persisted_schema = 0U;
    assert(app_settings_init() == ESP_OK);
    assert_schema10_preferences("cfg10_b");

    seed_schema10();
    find_blob("cfg10_b", false)->bytes[28] ^= 1U;
    assert(app_settings_init() == ESP_OK);
    assert_schema10_preferences("cfg10_a");

    seed_schema10();
    find_blob("cfg10_a", false)->bytes[28] ^= 1U;
    find_blob("cfg10_b", false)->bytes[28] ^= 1U;
    assert(app_settings_init() == ESP_OK);
    assert_schema9_preferences(17U, 75U, APP_DAILY_PAGE_MUSIC | APP_DAILY_PAGE_CHAT);

    seed_schema10();
    assert(app_settings_init() == ESP_OK);
    find_blob("cfg11_a", false)->bytes[29] ^= 1U;
    find_blob("cfg11_b", false)->bytes[29] ^= 1U;
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert_schema10_preferences("cfg10_b");

    /* A failed marker commit cannot override edits made by old firmware. */
    seed_schema10();
    fail_commit = 3U;
    assert(app_settings_init() == ESP_FAIL);
    test_blob_t *old = find_blob("cfg10_b", false);
    old->bytes[18] = 41U;
    old->bytes[27] = 0U;
    old->bytes[28] = APP_MUSIC_MODE_SEQUENTIAL;
    update_test_checksum(old);
    simulate_restart();
    fail_commit = 0U;
    assert(app_settings_init() == ESP_OK);
    assert_schema10_preferences("cfg10_b");
    for (unsigned index = 0U; index < 2U; ++index) {
        const test_blob_t *current = find_blob(index == 0U ? "cfg11_a" : "cfg11_b", false);
        settings_record_t decoded;
        assert(current != NULL && current->size == SETTINGS_RECORD_ENCODED_SIZE);
        assert(settings_record_decode(current->bytes, current->size, &decoded));
        assert(decoded.settings.audio_playback_volume == 41U);
        assert(decoded.settings.daily_pages_mask == 0U);
        assert(decoded.settings.music_mode == APP_MUSIC_MODE_SEQUENTIAL);
    }
}

static void test_page_order_persistence_noop_failure_and_defaults(void)
{
    seed_schema10();
    assert(app_settings_init() == ESP_OK);
    const test_blob_t old_a = *find_blob("cfg10_a", false);
    const test_blob_t old_b = *find_blob("cfg10_b", false);
    app_settings_snapshot_t before, after;
    assert(app_settings_get_snapshot(&before) == ESP_OK);
    const uint8_t reverse[] = {32U, 16U, 8U, 4U, 2U, 1U};
    app_settings_t candidate = before.settings;
    memcpy(candidate.daily_pages_order, reverse, sizeof(reverse));
    fail_commit = commits + 1U;
    assert(app_settings_save(&candidate) == ESP_FAIL);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.generation == before.generation);
    assert(settings_equal(&before.settings, &after.settings));
    fail_commit = 0U;
    assert(app_settings_save(&candidate) == ESP_OK);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.generation == before.generation + 1U);
    assert(settings_equal(&candidate, &after.settings));
    const unsigned before_noop = commits;
    assert(app_settings_save(&candidate) == ESP_OK);
    assert(commits == before_noop);
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(memcmp(candidate.daily_pages_order, reverse, sizeof(reverse)) == 0);
    assert(candidate.music_mode == APP_MUSIC_MODE_REPEAT_ALL);
    assert(candidate.daily_pages_mask == (APP_DAILY_PAGE_MUSIC | APP_DAILY_PAGE_CHAT));
    assert(app_settings_save_field(APP_SETTING_VOLUME, 51U) == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(memcmp(candidate.daily_pages_order, reverse, sizeof(reverse)) == 0);
    const uint8_t rotated[] = {2U, 4U, 8U, 16U, 32U, 1U};
    memcpy(candidate.daily_pages_order, rotated, sizeof(rotated));
    assert(app_settings_save(&candidate) == ESP_OK);
    test_blob_t *latest = find_blob(record_slot_key(s_active_slot), false);
    latest->bytes[29] ^= 1U;
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(memcmp(candidate.daily_pages_order, reverse, sizeof(reverse)) == 0);
    assert(candidate.audio_playback_volume == 51U);
    memcpy(candidate.daily_pages_order, rotated, sizeof(rotated));
    assert(app_settings_save(&candidate) == ESP_OK);
    persisted_schema = 0U;
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(memcmp(candidate.daily_pages_order, rotated, sizeof(rotated)) == 0);
    const app_settings_t valid = candidate;
    candidate.daily_pages_order[0] = 0U;
    const unsigned before_invalid = commits;
    assert(app_settings_save(&candidate) == ESP_ERR_INVALID_ARG);
    assert(commits == before_invalid);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(settings_equal(&candidate, &valid));
    assert(memcmp(&old_a, find_blob("cfg10_a", false), sizeof(old_a)) == 0);
    assert(memcmp(&old_b, find_blob("cfg10_b", false), sizeof(old_b)) == 0);
    assert(app_settings_restore_defaults() == ESP_OK);
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    app_settings_t defaults;
    app_settings_defaults(&defaults);
    assert(settings_equal(&candidate, &defaults));
}

static void test_music_persistence_noop_failure_and_defaults(void)
{
    seed_schema9();
    assert(app_settings_init() == ESP_OK);
    const test_blob_t old_a = *find_blob("cfg9_a", false);
    const test_blob_t old_b = *find_blob("cfg9_b", false);
    app_settings_snapshot_t before, after;
    assert(app_settings_get_snapshot(&before) == ESP_OK);
    app_settings_t candidate;
    const char *form = "section=music&music_mode=repeat_one";
    assert(app_settings_parse_form(form, strlen(form), &before.settings, &candidate));
    fail_commit = commits + 1U;
    assert(app_settings_save(&candidate) == ESP_FAIL);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.generation == before.generation);
    assert(settings_equal(&before.settings, &after.settings));
    fail_commit = 0U;
    assert(app_settings_save(&candidate) == ESP_OK);
    assert(app_settings_get_snapshot(&before) == ESP_OK);
    const unsigned previous_commits = commits;
    assert(app_settings_save(&candidate) == ESP_OK);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(commits == previous_commits && before.generation == after.generation);
    assert(app_settings_save_field(APP_SETTING_VOLUME, 43U) == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    const char *pages = "section=pages&daily_pages_mask=0&daily_pages_order=1,2,4,8,16,32";
    assert(app_settings_parse_form(pages, strlen(pages), &candidate, &candidate));
    assert(app_settings_save(&candidate) == ESP_OK);
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.settings.music_mode == APP_MUSIC_MODE_REPEAT_ONE);
    assert(after.settings.audio_playback_volume == 43U && after.settings.alarm_volume == 75U);
    assert(after.settings.daily_pages_mask == 0U);

    candidate = after.settings;
    candidate.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
    assert(app_settings_save(&candidate) == ESP_OK);
    test_blob_t *latest = find_blob(record_slot_key(s_active_slot), false);
    latest->bytes[28] ^= 1U;
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(candidate.music_mode == APP_MUSIC_MODE_REPEAT_ONE);
    candidate.music_mode = APP_MUSIC_MODE_REPEAT_ALL;
    assert(app_settings_save(&candidate) == ESP_OK);
    persisted_schema = 0U; /* Current v11 keeps music even with retained v9. */
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(candidate.music_mode == APP_MUSIC_MODE_REPEAT_ALL);
    assert(candidate.daily_pages_mask == 0U && candidate.alarm_volume == 75U);
    const app_settings_t valid = candidate;
    candidate.music_mode = (app_music_mode_t)3;
    const unsigned before_invalid = commits;
    assert(app_settings_save(&candidate) == ESP_ERR_INVALID_ARG);
    assert(commits == before_invalid);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(settings_equal(&candidate, &valid));
    assert(memcmp(&old_a, find_blob("cfg9_a", false), sizeof(old_a)) == 0);
    assert(memcmp(&old_b, find_blob("cfg9_b", false), sizeof(old_b)) == 0);
    assert(app_settings_restore_defaults() == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(candidate.music_mode == APP_MUSIC_MODE_SEQUENTIAL);
    assert(candidate.daily_pages_mask == APP_DAILY_PAGES_ALL_MASK);
    const unsigned before_defaults = commits;
    assert(app_settings_restore_defaults() == ESP_OK);
    assert(commits == before_defaults);
}

static void test_daily_pages_persistence_corruption_and_defaults(void)
{
    seed_schema8();
    assert(app_settings_init() == ESP_OK);
    app_settings_snapshot_t before, after;
    assert(app_settings_get_snapshot(&before) == ESP_OK);
    app_settings_t candidate = before.settings;
    candidate.daily_pages_mask = 0U;
    fail_commit = commits + 1U;
    assert(app_settings_save(&candidate) == ESP_FAIL);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.generation == before.generation);
    assert(settings_equal(&before.settings, &after.settings));
    simulate_restart();
    fail_commit = 0U;
    assert(app_settings_init() == ESP_OK);
    assert_schema8_preferences(17U, 75U);
    assert(app_settings_save(&candidate) == ESP_OK);
    assert(app_settings_save_field(APP_SETTING_VOLUME, 40U) == ESP_OK);
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.settings.daily_pages_mask == 0U);
    assert(after.settings.audio_playback_volume == 40U && after.settings.alarm_volume == 75U);
    candidate = after.settings;
    candidate.daily_pages_mask = APP_DAILY_PAGE_WEATHER | APP_DAILY_PAGE_CALENDAR;
    assert(app_settings_save(&candidate) == ESP_OK);
    test_blob_t *latest = find_blob(record_slot_key(s_active_slot), false);
    latest->bytes[27] ^= APP_DAILY_PAGE_CHAT;
    simulate_restart();
    assert(app_settings_init() == ESP_OK); /* Peer repairs corrupt newest record. */
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.settings.daily_pages_mask == 0U);
    candidate = after.settings;
    candidate.daily_pages_mask = APP_DAILY_PAGE_MUSIC;
    assert(app_settings_save(&candidate) == ESP_OK);
    persisted_schema = 0U;
    simulate_restart();
    assert(app_settings_init() == ESP_OK); /* Valid v11 beats retained v8 slots. */
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.settings.daily_pages_mask == APP_DAILY_PAGE_MUSIC);
    assert(after.settings.audio_playback_volume == 40U);
    assert(app_settings_restore_defaults() == ESP_OK);
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    app_settings_defaults(&candidate);
    assert(settings_equal(&candidate, &after.settings));
    assert(after.settings.daily_pages_mask == APP_DAILY_PAGES_ALL_MASK);
}

static void test_future_schema_preserves_all_storage(void)
{
    seed_schema8();
    assert(app_settings_init() == ESP_OK);
    persisted_schema = 12U;
    const unsigned before_commits = commits;
    test_blob_t saved_blobs[12];
    memcpy(saved_blobs, blobs, sizeof(blobs));
    simulate_restart();
    assert(app_settings_init() == ESP_ERR_NOT_SUPPORTED);
    app_settings_t current;
    assert(app_settings_get(&current) == ESP_OK);
    assert(current.daily_pages_mask == APP_DAILY_PAGES_ALL_MASK);
    assert(current.music_mode == APP_MUSIC_MODE_SEQUENTIAL);
    current.music_mode = APP_MUSIC_MODE_REPEAT_ONE;
    assert(app_settings_save(&current) == ESP_ERR_NOT_SUPPORTED);
    current.daily_pages_mask = 0U;
    assert(app_settings_save(&current) == ESP_ERR_NOT_SUPPORTED);
    bool manual = false;
    assert(app_settings_toggle_manual_saving(&manual) == ESP_ERR_NOT_SUPPORTED);
    assert(app_settings_restore_defaults() == ESP_ERR_NOT_SUPPORTED);
    assert(commits == before_commits && persisted_schema == 12U);
    assert(memcmp(saved_blobs, blobs, sizeof(blobs)) == 0);
}

int main(void)
{
    test_schema10_migration_and_commit_failures();
    test_page_order_persistence_noop_failure_and_defaults();
    test_schema9_migration_and_commit_failures();
    test_music_persistence_noop_failure_and_defaults();
    test_schema8_migration_and_commit_failures();
    test_daily_pages_persistence_corruption_and_defaults();
    test_future_schema_preserves_all_storage();
    test_schema7_alarm_migration_and_independent_save();
    test_migration_retry();
    test_scoped_save_failure_noop_and_restart();
    test_dev1_display_is_ignored_without_reset_or_startup_writes();
    puts("settings service migration, fault injection and scoped persistence tests passed");
}
