/* Host fault injection for our settings service; the fake storage models
 * commit boundaries, not the ESP-IDF NVS implementation or real power loss. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int settings_test_setenv(const char *name, const char *value, int overwrite);
#define setenv settings_test_setenv
#include "../src/settings/app_settings.c"
#undef setenv

typedef struct {
    char key[16];
    uint8_t bytes[SETTINGS_RECORD_ENCODED_SIZE];
    size_t size;
} test_blob_t;

static test_blob_t blobs[24];
static test_blob_t pending_blob;
static uint16_t persisted_schema;
static uint16_t pending_schema;
static unsigned commits;
static unsigned fail_commit;
static bool lock_held;
static unsigned timezone_sets;
static unsigned fail_timezone_set;
static void (*before_next_lock)(void);

static int settings_test_setenv(const char *name, const char *value, int overwrite)
{
    assert(strcmp(name, "TZ") == 0);
    if (++timezone_sets == fail_timezone_set) return -1;
    return setenv(name, value, overwrite);
}

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
    if (before_next_lock != NULL) {
        void (*inject)(void) = before_next_lock;
        before_next_lock = NULL;
        inject();
        assert(!lock_held);
    }
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
    for (size_t index = 0U; index < 24U; ++index) {
        if (strcmp(blobs[index].key, key) == 0) { return &blobs[index]; }
    }
    if (create) {
        for (size_t index = 0U; index < 24U; ++index) {
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
    before_next_lock = NULL;
    fail_timezone_set = 0U;
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

    persisted_schema = APP_SETTINGS_SCHEMA_VERSION + 1U;
    simulate_restart();
    assert(app_settings_init() == ESP_ERR_NOT_SUPPORTED);
    assert(app_settings_save_field(APP_SETTING_VOLUME, 90U) == ESP_ERR_NOT_SUPPORTED);
    assert(commits == previous_commits && persisted_schema == APP_SETTINGS_SCHEMA_VERSION + 1U);
}

static void test_dev1_display_is_ignored_without_reset_or_startup_writes(void)
{
    for (uint8_t old_display = 0U; old_display <= 2U; ++old_display) {
        seed_schema6();
        assert(app_settings_init() == ESP_OK);
        app_settings_snapshot_t before, after;
        assert(app_settings_get_snapshot(&before) == ESP_OK);
        test_blob_t *slot_a = find_blob("cfg14_a", false);
        test_blob_t *slot_b = find_blob("cfg14_b", false);
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
    assert(current.daily_pages_mask == (pages == 0U ? 0U : pages | APP_DAILY_PAGE_TIMER | APP_DAILY_PAGE_ENVIRONMENT));
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
    find_blob("cfg14_a", false)->bytes[18] ^= 1U;
    find_blob("cfg14_b", false)->bytes[18] ^= 1U;
    simulate_restart();
    assert(app_settings_init() == ESP_OK); /* Current marker with both slots corrupt. */
    assert_schema9_preferences(17U, 75U, APP_DAILY_PAGE_MUSIC | APP_DAILY_PAGE_CHAT);

    /* Interrupted migration does not outrank newer edits made by old firmware
     * while its shared marker is still v9. Both partial v12 copies are repaired. */
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
        test_blob_t *blob = find_blob(index == 0U ? "cfg14_a" : "cfg14_b", false);
        settings_record_t decoded;
        assert(blob != NULL && blob->size == SETTINGS_RECORD_ENCODED_SIZE);
        assert(settings_record_decode(blob->bytes, blob->size, &decoded));
        assert(decoded.settings.audio_playback_volume == 36U);
        assert(decoded.settings.daily_pages_mask == (APP_DAILY_PAGE_WEATHER | APP_DAILY_PAGE_TIMER | APP_DAILY_PAGE_ENVIRONMENT));
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
    const uint8_t defaults[] = APP_DAILY_PAGES_LEGACY_ORDER;
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
    find_blob("cfg14_a", false)->bytes[29] ^= 1U;
    find_blob("cfg14_b", false)->bytes[29] ^= 1U;
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
        const test_blob_t *current = find_blob(index == 0U ? "cfg14_a" : "cfg14_b", false);
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
    const uint8_t reverse[] = {32U, 16U, 8U, 64U, 4U, 2U, 1U, 128U};
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
    assert(candidate.daily_pages_mask == (APP_DAILY_PAGE_MUSIC | APP_DAILY_PAGE_CHAT | APP_DAILY_PAGE_TIMER | APP_DAILY_PAGE_ENVIRONMENT));
    assert(app_settings_save_field(APP_SETTING_VOLUME, 51U) == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(memcmp(candidate.daily_pages_order, reverse, sizeof(reverse)) == 0);
    const uint8_t rotated[] = {2U, 4U, 64U, 8U, 16U, 32U, 1U, 128U};
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
    const char *pages = "section=pages&daily_pages_mask=0&daily_pages_order=1,128,2,4,64,8,16,32";
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
    persisted_schema = 0U; /* Current v12 keeps music even with retained v9. */
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
    assert(app_settings_init() == ESP_OK); /* Valid v12 beats retained v8 slots. */
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
    persisted_schema = APP_SETTINGS_SCHEMA_VERSION + 1U;
    const unsigned before_commits = commits;
    test_blob_t saved_blobs[sizeof(blobs) / sizeof(blobs[0])];
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
    assert(commits == before_commits && persisted_schema == APP_SETTINGS_SCHEMA_VERSION + 1U);
    assert(memcmp(saved_blobs, blobs, sizeof(blobs)) == 0);
}

static void seed_schema11(void)
{
    seed_schema10();
    persisted_schema = 11U;
    const uint8_t orders[2][6] = {{2U, 4U, 8U, 16U, 32U, 1U},
                                   {32U, 16U, 8U, 4U, 2U, 1U}};
    for (unsigned index = 0U; index < 2U; ++index) {
        test_blob_t *blob = find_blob(index == 0U ? "cfg11_a" : "cfg11_b", true);
        *blob = *find_blob(index == 0U ? "cfg10_a" : "cfg10_b", false);
        snprintf(blob->key, sizeof(blob->key), "%s", index == 0U ? "cfg11_a" : "cfg11_b");
        blob->size = SETTINGS_RECORD_SCHEMA11_ENCODED_SIZE;
        blob->bytes[4] = 10U;
        blob->bytes[6] = (uint8_t)blob->size;
        blob->bytes[12] = 11U;
        memcpy(blob->bytes + 29U, orders[index], 6U);
        blob->bytes[35] = 0U;
        update_test_checksum(blob);
    }
}

static void assert_schema11_preferences(const char *key)
{
    const test_blob_t *old = find_blob(key, false);
    settings_record_t expected;
    assert(settings_record_decode_schema11(old->bytes, old->size, &expected));
    app_settings_t current;
    assert(app_settings_get(&current) == ESP_OK);
    assert(persisted_schema == APP_SETTINGS_SCHEMA_VERSION);
    assert(settings_equal(&current, &expected.settings));
    assert(current.timer_minutes == 5U);
    assert(memcmp(current.daily_pages_order, old->bytes + 29U, 6U) == 0);
    assert(current.daily_pages_order[6] == APP_DAILY_PAGE_TIMER);
    assert(current.daily_pages_mask == (old->bytes[27] == 0U ? 0U :
                                       old->bytes[27] | APP_DAILY_PAGE_TIMER | APP_DAILY_PAGE_ENVIRONMENT));
}

static void test_schema11_migration_and_commit_failures(void)
{
    for (unsigned failure = 0U; failure <= 3U; ++failure) {
        seed_schema11();
        const test_blob_t old_a = *find_blob("cfg11_a", false);
        const test_blob_t old_b = *find_blob("cfg11_b", false);
        fail_commit = failure;
        assert(app_settings_init() == (failure == 0U ? ESP_OK : ESP_FAIL));
        if (failure != 0U) {
            assert(!s_initialized && persisted_schema == 11U);
            simulate_restart();
            fail_commit = 0U;
            assert(app_settings_init() == ESP_OK);
        }
        assert_schema11_preferences("cfg11_b");
        assert(memcmp(&old_a, find_blob("cfg11_a", false), sizeof(old_a)) == 0);
        assert(memcmp(&old_b, find_blob("cfg11_b", false), sizeof(old_b)) == 0);
        for (unsigned index = 0U; index < 2U; ++index) {
            const test_blob_t *current = find_blob(index == 0U ? "cfg14_a" : "cfg14_b", false);
            assert(current != NULL && current->size == 44U && current->bytes[12] == APP_SETTINGS_SCHEMA_VERSION);
        }
        const unsigned before_restart = commits;
        simulate_restart();
        assert(app_settings_init() == ESP_OK);
        assert_schema11_preferences("cfg11_b");
        assert(commits == before_restart);
    }
    seed_schema11();
    persisted_schema = 0U;
    assert(app_settings_init() == ESP_OK);
    assert_schema11_preferences("cfg11_b");

    seed_schema11();
    find_blob("cfg11_b", false)->bytes[27] = 0U;
    update_test_checksum(find_blob("cfg11_b", false));
    assert(app_settings_init() == ESP_OK);
    assert_schema11_preferences("cfg11_b");

    seed_schema11();
    find_blob("cfg11_b", false)->bytes[29] = 0U;
    update_test_checksum(find_blob("cfg11_b", false));
    assert(app_settings_init() == ESP_OK);
    assert_schema11_preferences("cfg11_a"); /* CRC-valid bad order is rejected. */

    seed_schema11();
    find_blob("cfg11_a", false)->bytes[29] ^= 1U;
    find_blob("cfg11_b", false)->bytes[29] ^= 1U;
    assert(app_settings_init() == ESP_OK);
    assert_schema10_preferences("cfg10_b");

    seed_schema11();
    assert(app_settings_init() == ESP_OK);
    find_blob("cfg14_a", false)->bytes[36] ^= 1U;
    find_blob("cfg14_b", false)->bytes[36] ^= 1U;
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert_schema11_preferences("cfg11_b");

    seed_schema11();
    fail_commit = 3U; /* Both current slots exist but marker remains v11. */
    assert(app_settings_init() == ESP_FAIL);
    test_blob_t *old = find_blob("cfg11_b", false);
    old->bytes[18] = 41U;
    old->bytes[27] = 0U;
    const uint8_t changed[] = {8U, 4U, 2U, 1U, 32U, 16U};
    memcpy(old->bytes + 29U, changed, sizeof(changed));
    update_test_checksum(old);
    simulate_restart();
    fail_commit = 0U;
    assert(app_settings_init() == ESP_OK);
    assert_schema11_preferences("cfg11_b");
    for (unsigned index = 0U; index < 2U; ++index) {
        const test_blob_t *current = find_blob(index == 0U ? "cfg14_a" : "cfg14_b", false);
        settings_record_t decoded;
        assert(settings_record_decode(current->bytes, current->size, &decoded));
        assert(decoded.settings.audio_playback_volume == 41U);
        assert(decoded.settings.daily_pages_mask == 0U);
        assert(memcmp(decoded.settings.daily_pages_order, changed, sizeof(changed)) == 0);
    }
}

static void test_timer_persistence_rotation_and_defaults(void)
{
    seed_schema11();
    assert(app_settings_init() == ESP_OK);
    app_settings_snapshot_t before, after;
    assert(app_settings_get_snapshot(&before) == ESP_OK);
    app_settings_t candidate;
    const char *form = "section=timer&timer_minutes=180";
    assert(app_settings_parse_form(form, strlen(form), &before.settings, &candidate));
    fail_commit = commits + 1U;
    assert(app_settings_save(&candidate) == ESP_FAIL);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.generation == before.generation);
    assert(settings_equal(&after.settings, &before.settings));
    fail_commit = 0U;
    const settings_record_slot_t old_slot = s_active_slot;
    const test_blob_t old_record = *find_blob(record_slot_key(old_slot), false);
    assert(app_settings_save(&candidate) == ESP_OK);
    assert(s_active_slot != old_slot);
    assert(memcmp(&old_record, find_blob(record_slot_key(old_slot), false), sizeof(old_record)) == 0);
    assert(app_settings_get_snapshot(&after) == ESP_OK);
    assert(after.generation == before.generation + 1U);
    assert(after.settings.timer_minutes == 180U);
    after.settings.timer_minutes = before.settings.timer_minutes;
    assert(settings_equal(&after.settings, &before.settings));
    const unsigned before_noop = commits;
    assert(app_settings_save(&candidate) == ESP_OK);
    assert(commits == before_noop);
    assert(app_settings_save_field(APP_SETTING_VOLUME, 51U) == ESP_OK);
    assert(s_active_slot == old_slot);
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(candidate.timer_minutes == 180U && candidate.audio_playback_volume == 51U);
    candidate.timer_minutes = 1U;
    assert(app_settings_save(&candidate) == ESP_OK);
    test_blob_t *latest = find_blob(record_slot_key(s_active_slot), false);
    latest->bytes[37] = 0U;
    update_test_checksum(latest); /* Valid checksum does not rescue invalid timer. */
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    assert(candidate.timer_minutes == 180U && candidate.audio_playback_volume == 51U);
    assert(app_settings_restore_defaults() == ESP_OK);
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&candidate) == ESP_OK);
    app_settings_t defaults;
    app_settings_defaults(&defaults);
    assert(settings_equal(&candidate, &defaults));
}

static void test_fresh_and_v1_default_orders(void)
{
    for (unsigned schema = 0U; schema <= 1U; ++schema) {
        memset(blobs, 0, sizeof(blobs));
        persisted_schema = (uint16_t)schema;
        commits = 0U;
        fail_commit = 0U;
        simulate_restart();
        assert(app_settings_init() == ESP_OK);
        app_settings_t current;
        assert(app_settings_get(&current) == ESP_OK);
        const uint8_t fresh[] = APP_DAILY_PAGES_DEFAULT_ORDER;
        const uint8_t legacy[] = APP_DAILY_PAGES_LEGACY_ORDER;
        assert(memcmp(current.daily_pages_order, schema == 0U ? fresh : legacy,
                      APP_DAILY_PAGE_COUNT) == 0);
        assert(current.timer_minutes == 5U && current.daily_pages_mask == 255U);
    }
}

static void test_existing_custom_duration_is_not_replaced_by_new_default(void)
{
    seed_schema11();
    assert(app_settings_init() == ESP_OK);
    app_settings_t settings;
    assert(app_settings_get(&settings) == ESP_OK);
    settings.timer_minutes = 25U; /* v0.33-dev.1 used this default */
    assert(app_settings_save(&settings) == ESP_OK);
    const unsigned saved_commits = commits;
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&settings) == ESP_OK);
    assert(settings.timer_minutes == 25U && commits == saved_commits);
}

static void seed_schema12(uint8_t mask)
{
    seed_schema11();
    persisted_schema = 12U;
    const uint8_t order[] = {64U, 4U, 32U, 2U, 1U, 16U, 8U};
    for (unsigned index = 0U; index < 2U; ++index) {
        test_blob_t *blob = find_blob(index == 0U ? "cfg12_a" : "cfg12_b", true);
        *blob = *find_blob(index == 0U ? "cfg11_a" : "cfg11_b", false);
        snprintf(blob->key, sizeof(blob->key), "%s", index == 0U ? "cfg12_a" : "cfg12_b");
        blob->size = SETTINGS_RECORD_SCHEMA12_ENCODED_SIZE;
        blob->bytes[4] = 11U;
        blob->bytes[6] = (uint8_t)blob->size;
        blob->bytes[12] = 12U;
        blob->bytes[27] = mask;
        memcpy(blob->bytes + 29U, order, sizeof(order));
        blob->bytes[36] = 25U;
        blob->bytes[37] = blob->bytes[38] = blob->bytes[39] = 0U;
        update_test_checksum(blob);
    }
}

static void test_schema12_migration_and_failures(void)
{
    for (unsigned mask = 0U; mask <= 127U; ++mask) {
        for (unsigned failure = 0U; failure <= 3U; ++failure) {
            seed_schema12((uint8_t)mask);
            const test_blob_t old_a = *find_blob("cfg12_a", false);
            const test_blob_t old_b = *find_blob("cfg12_b", false);
            fail_commit = failure;
            assert(app_settings_init() == (failure == 0U ? ESP_OK : ESP_FAIL));
            if (failure != 0U) {
                assert(!s_initialized && persisted_schema == 12U);
                simulate_restart();
                fail_commit = 0U;
                assert(app_settings_init() == ESP_OK);
            }
            app_settings_t current;
            assert(app_settings_get(&current) == ESP_OK);
            assert(current.daily_pages_mask == (mask == 0U ? 0U : (mask | 128U)));
            assert(memcmp(current.daily_pages_order, old_b.bytes + 29U, 7U) == 0);
            assert(current.daily_pages_order[7] == APP_DAILY_PAGE_ENVIRONMENT);
            assert(current.timer_minutes == 25U && !current.environment_history_save_enabled);
            assert(memcmp(&old_a, find_blob("cfg12_a", false), sizeof(old_a)) == 0);
            assert(memcmp(&old_b, find_blob("cfg12_b", false), sizeof(old_b)) == 0);
            const unsigned before = commits;
            simulate_restart();
            assert(app_settings_init() == ESP_OK && commits == before);
        }
    }
    seed_schema12(127U);
    persisted_schema = 0U;
    assert(app_settings_init() == ESP_OK);
    app_settings_t current;
    assert(app_settings_get(&current) == ESP_OK && current.timer_minutes == 25U);
    seed_schema12(127U);
    find_blob("cfg12_b", false)->bytes[36] = 0U;
    update_test_checksum(find_blob("cfg12_b", false));
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&current) == ESP_OK && current.audio_playback_volume == 16U);
    /* A previous firmware may change its authoritative record while migration
     * failed before the schema marker. Retrying must not resurrect partial v13. */
    seed_schema12(127U);
    fail_commit = 3U;
    assert(app_settings_init() == ESP_FAIL);
    test_blob_t *old = find_blob("cfg12_b", false);
    old->bytes[36] = 30U;
    old->bytes[27] = 0U;
    update_test_checksum(old);
    simulate_restart();
    fail_commit = 0U;
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&current) == ESP_OK);
    assert(current.timer_minutes == 30U && current.daily_pages_mask == 0U);
}

static void seed_schema13(bool history)
{
    seed_schema12(0U);
    persisted_schema = 13U;
    app_settings_t settings;
    app_settings_defaults(&settings);
    settings.environment_history_save_enabled = history;
    settings.daily_pages_mask = 0U;
    settings.timer_minutes = 30U;
    for (unsigned index = 0U; index < 2U; ++index) {
        test_blob_t *blob = find_blob(index == 0U ? "cfg13_a" : "cfg13_b", true);
        blob->size = SETTINGS_RECORD_SCHEMA13_ENCODED_SIZE;
        assert(settings_record_encode(10U + index, &settings, blob->bytes, blob->size));
        blob->bytes[4] = 12U;
        blob->bytes[12] = 13U;
        update_test_checksum(blob);
    }
}

static void test_schema13_migration_preserves_limited_consent(void)
{
    for (unsigned history = 0U; history <= 1U; ++history) {
        for (unsigned failure = 0U; failure <= 3U; ++failure) {
            seed_schema13(history != 0U);
            const test_blob_t old_a = *find_blob("cfg13_a", false);
            const test_blob_t old_b = *find_blob("cfg13_b", false);
            fail_commit = failure;
            assert(app_settings_init() == (failure == 0U ? ESP_OK : ESP_FAIL));
            if (failure != 0U) {
                assert(!s_initialized && persisted_schema == 13U);
                simulate_restart();
                fail_commit = 0U;
                assert(app_settings_init() == ESP_OK);
            }
            app_settings_t current;
            assert(app_settings_get(&current) == ESP_OK);
            assert(current.environment_history_save_enabled == (history != 0U));
            assert(!current.environment_archive_save_enabled);
            assert(current.timer_minutes == 30U && current.daily_pages_mask == 0U);
            assert(memcmp(&old_a, find_blob("cfg13_a", false), sizeof(old_a)) == 0);
            assert(memcmp(&old_b, find_blob("cfg13_b", false), sizeof(old_b)) == 0);
            const unsigned before = commits;
            simulate_restart();
            assert(app_settings_init() == ESP_OK && commits == before);
        }
    }
    seed_schema13(true);
    persisted_schema = 0U;
    assert(app_settings_init() == ESP_OK);
    app_settings_t current;
    assert(app_settings_get(&current) == ESP_OK);
    assert(current.environment_history_save_enabled && !current.environment_archive_save_enabled);
    /* A stale partial migration must not win over a later old-firmware save. */
    seed_schema13(true);
    fail_commit = 3U;
    assert(app_settings_init() == ESP_FAIL);
    test_blob_t *old = find_blob("cfg13_b", false);
    old->bytes[38] = 0U;
    update_test_checksum(old);
    simulate_restart();
    fail_commit = 0U;
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&current) == ESP_OK);
    assert(!current.environment_history_save_enabled && !current.environment_archive_save_enabled);
}

static void test_archive_form_persistence(void)
{
    seed_schema13(true);
    assert(app_settings_init() == ESP_OK);
    const char *enable = "section=environment&environment_history_save_enabled=on&environment_archive_save_enabled=on";
    app_settings_t current;
    assert(app_settings_save_form(enable, strlen(enable), &current) == ESP_OK);
    assert(current.environment_history_save_enabled && current.environment_archive_save_enabled);
    const unsigned saved = commits;
    assert(app_settings_save_form(enable, strlen(enable), NULL) == ESP_OK && commits == saved);
    const char *invalid[] = {
        "section=environment&environment_history_save_enabled=off&environment_archive_save_enabled=on",
        "section=environment&environment_archive_save_enabled=on",
        "section=environment&environment_history_save_enabled=on&environment_archive_save_enabled=true",
        "section=environment&environment_history_save_enabled=on&environment_archive_save_enabled=on&environment_archive_save_enabled=on",
        "section=timer&timer_minutes=10&environment_archive_save_enabled=off",
    };
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(app_settings_save_form(invalid[i], strlen(invalid[i]), NULL) == ESP_ERR_INVALID_ARG);
    }
    assert(commits == saved);
    char backup[APP_SETTINGS_BACKUP_MAX_LENGTH + 1U];
    size_t length = 0U;
    assert(app_settings_export_backup(backup, sizeof(backup), &length) == ESP_OK);
    assert(app_settings_import_backup(backup, length, &current) == ESP_OK);
    assert(current.environment_archive_save_enabled);
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&current) == ESP_OK && current.environment_archive_save_enabled);
    const char *off = "section=environment&environment_history_save_enabled=off";
    fail_commit = commits + 1U;
    assert(app_settings_save_form(off, strlen(off), NULL) == ESP_FAIL);
    assert(app_settings_get(&current) == ESP_OK && current.environment_archive_save_enabled);
    fail_commit = 0U;
    assert(app_settings_save_form(off, strlen(off), &current) == ESP_OK);
    assert(!current.environment_history_save_enabled && !current.environment_archive_save_enabled);
    assert(app_settings_restore_defaults() == ESP_OK);
    assert(app_settings_get(&current) == ESP_OK && !current.environment_archive_save_enabled);
}

static void inject_device_changes(void)
{
    assert(app_settings_save_field(APP_SETTING_VOLUME, 43U) == ESP_OK);
    bool manual = false;
    assert(app_settings_toggle_manual_saving(&manual) == ESP_OK);
}

static void test_atomic_forms_backup_and_timezone(void)
{
    seed_schema12(127U);
    assert(app_settings_init() == ESP_OK);
    app_settings_t before, saved;
    assert(app_settings_get(&before) == ESP_OK);
    assert(app_settings_apply_timezone(&before) == ESP_OK);
    char backup[APP_SETTINGS_BACKUP_MAX_LENGTH + 1U];
    size_t length = 0U;
    assert(app_settings_export_backup(backup, sizeof(backup), &length) == ESP_OK);
    const char *alarm = "section=alarm&alarm=off&alarm_hour=6&alarm_minute=45&alarm_days=62&alarm_volume=7";
    /* Form already received; a queued device change wins the lock before it.
     * Scoped parse must use that new record rather than a caller's snapshot. */
    before_next_lock = inject_device_changes;
    assert(app_settings_save_form(alarm, strlen(alarm), &saved) == ESP_OK);
    assert(saved.audio_playback_volume == 43U);
    assert(saved.manual_saving_requested != before.manual_saving_requested);
    assert(saved.alarm_volume == 7U && !saved.alarm_enabled);
    const unsigned unchanged = commits;
    assert(app_settings_save_form(alarm, strlen(alarm), NULL) == ESP_OK && commits == unchanged);
    const char *environment = "section=environment&environment_history_save_enabled=on";
    assert(app_settings_save_form(environment, strlen(environment), &saved) == ESP_OK);
    assert(saved.environment_history_save_enabled);
    assert(app_settings_import_backup(backup, length, &saved) == ESP_OK);
    assert(saved.environment_history_save_enabled);
    assert(saved.update_channel == APP_UPDATE_CHANNEL_BETA);
    assert(saved.manual_saving_requested != before.manual_saving_requested);
    assert(saved.audio_playback_volume == before.audio_playback_volume);
    const unsigned imported = commits;
    assert(app_settings_import_backup(backup, length, NULL) == ESP_OK && commits == imported);

    before = saved;
    const char *general = "section=general&timezone=480&unit=c&volume=55";
    fail_timezone_set = timezone_sets + 1U;
    assert(app_settings_save_form(general, strlen(general), &saved) == ESP_FAIL);
    assert(commits == imported && strcmp(getenv("TZ"), "UTC-5:45") == 0);
    assert(app_settings_get(&saved) == ESP_OK && settings_equal(&saved, &before));
    fail_timezone_set = 0U;
    fail_commit = commits + 1U;
    assert(app_settings_save_form(general, strlen(general), &saved) == ESP_FAIL);
    assert(strcmp(getenv("TZ"), "UTC-5:45") == 0);
    assert(app_settings_get(&saved) == ESP_OK && settings_equal(&saved, &before));
    fail_commit = 0U;
    assert(app_settings_save_form(general, strlen(general), &saved) == ESP_OK);
    assert(saved.utc_offset_minutes == 480 && strcmp(getenv("TZ"), "UTC-8") == 0);
    assert(saved.environment_history_save_enabled);
    fail_commit = commits + 1U;
    assert(app_settings_import_backup(backup, length, NULL) == ESP_FAIL);
    assert(strcmp(getenv("TZ"), "UTC-8") == 0);
    fail_commit = 0U;
    const unsigned invalid_before = commits;
    const char *invalid_environment = "section=environment&environment_history_save_enabled=true";
    assert(app_settings_save_form(invalid_environment, strlen(invalid_environment), NULL) == ESP_ERR_INVALID_ARG);
    assert(app_settings_import_backup("{}", 2U, NULL) == ESP_ERR_INVALID_ARG);
    assert(commits == invalid_before);
    simulate_restart();
    assert(app_settings_init() == ESP_OK);
    assert(app_settings_get(&saved) == ESP_OK && saved.utc_offset_minutes == 480);
    assert(saved.environment_history_save_enabled);
    assert(app_settings_restore_defaults() == ESP_OK);
    assert(app_settings_get(&saved) == ESP_OK && !saved.environment_history_save_enabled);
}

int main(void)
{
    test_schema13_migration_preserves_limited_consent();
    test_archive_form_persistence();
    test_schema12_migration_and_failures();
    test_atomic_forms_backup_and_timezone();
    test_existing_custom_duration_is_not_replaced_by_new_default();
    test_fresh_and_v1_default_orders();
    test_schema11_migration_and_commit_failures();
    test_timer_persistence_rotation_and_defaults();
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
