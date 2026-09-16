#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/market/market_config.c"

static uint8_t fake_record[MARKET_CONFIG_RECORD_SIZE];
static uint8_t fake_pending[MARKET_CONFIG_RECORD_SIZE];
static size_t fake_size;
static bool fake_exists;
static int reads, writes, commits;
static esp_err_t fake_storage_error, fake_write_error, fake_commit_error;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout)
{ (void)semaphore; (void)timeout; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{ (void)semaphore; return pdTRUE; }
esp_err_t app_storage_init(void) { return fake_storage_error; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "rlcd_market") == 0);
    if (!fake_exists && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    *handle = 17;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *size)
{
    assert(handle == 17 && strcmp(key, "config") == 0);
    ++reads;
    if (!fake_exists) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < fake_size) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(value, fake_record, fake_size);
    *size = fake_size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size)
{
    assert(handle == 17 && strcmp(key, "config") == 0);
    assert(size == sizeof(fake_pending));
    ++writes;
    if (fake_write_error != ESP_OK) return fake_write_error;
    memcpy(fake_pending, value, size);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 17);
    ++commits;
    if (fake_commit_error != ESP_OK) return fake_commit_error;
    memcpy(fake_record, fake_pending, sizeof(fake_record));
    fake_size = sizeof(fake_record);
    fake_exists = true;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 17); }

static void reboot(void)
{
    s_mutex = NULL;
    s_initialized = false;
    s_generation = 0;
    memset(&s_config, 0, sizeof(s_config));
}

int main(void)
{
    market_config_t config;
    uint32_t generation = 999;
    assert(market_config_get(NULL) == ESP_ERR_INVALID_ARG);
    assert(market_config_get_snapshot(&config, &generation) == ESP_OK);
    assert(!config.enabled && config.count == 5 && generation == 0);
    assert(reads == 0 && writes == 0);
    assert(market_config_set(&config) == ESP_OK);
    assert(writes == 0 && commits == 0);
    config.enabled = true;
    config.count = 3;
    config.ids[0] = 14; config.ids[1] = 10; config.ids[2] = 0;
    assert(market_config_set(&config) == ESP_OK);
    assert(writes == 1 && commits == 1);
    assert(market_config_get_snapshot(&config, &generation) == ESP_OK);
    assert(generation == 1 && config.enabled && config.ids[0] == 14);
    assert(config.ids[3] == 0 && config.ids[4] == 0);
    config.ids[4] = 255;
    assert(market_config_set(&config) == ESP_OK);
    assert(writes == 1); /* Unused bytes do not cause a flash write. */
    reboot();
    assert(market_config_get(&config) == ESP_OK);
    assert(reads == 1 && config.enabled && config.count == 3 && config.ids[0] == 14);
    config.count = 6;
    assert(market_config_set(&config) == ESP_ERR_INVALID_ARG);
    assert(writes == 1);
    config.count = 3;
    config.ids[1] = 14;
    assert(market_config_set(&config) == ESP_ERR_INVALID_ARG);
    assert(writes == 1);
    config.ids[1] = 7;
    fake_write_error = ESP_ERR_NO_MEM;
    assert(market_config_set(&config) == ESP_ERR_NO_MEM);
    assert(market_config_get_snapshot(&config, &generation) == ESP_OK);
    assert(config.ids[1] == 10 && generation == 0);
    fake_write_error = ESP_OK;
    fake_commit_error = ESP_FAIL;
    config.enabled = false;
    assert(market_config_set(&config) == ESP_FAIL);
    assert(market_config_get(&config) == ESP_OK && config.enabled);
    fake_commit_error = ESP_OK;
    fake_record[6] ^= 1; /* Checksum corruption defaults without erasing NVS. */
    reboot();
    const int previous_writes = writes;
    assert(market_config_get(&config) == ESP_OK);
    assert(!config.enabled && config.count == 5 && config.ids[0] == 0);
    assert(writes == previous_writes);
    fake_size = MARKET_CONFIG_RECORD_SIZE + 1;
    reboot();
    assert(market_config_get(&config) == ESP_OK && !config.enabled);
    fake_storage_error = ESP_FAIL;
    reboot();
    assert(market_config_get(&config) == ESP_FAIL);
    fake_storage_error = ESP_OK;
    assert(market_config_get(&config) == ESP_OK);
    puts("market config tests passed");
    return 0;
}
