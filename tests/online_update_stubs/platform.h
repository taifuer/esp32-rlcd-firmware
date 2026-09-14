#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Host-only doubles for orchestration tests, not ESP-IDF ABI definitions. */
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
int xTaskCreate(void (*task)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(unsigned);
void test_log(const char *, const char *, ...);
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
esp_err_t network_time_begin_online_session(unsigned);
esp_err_t network_time_end_online_session(void);
esp_err_t audio_music_stop_and_wait(unsigned);
void boot_recovery_note_planned_restart(void);
void esp_restart(void);
esp_err_t esp_crt_bundle_attach(void *);

typedef struct { uint8_t magic; uint8_t padding[23]; } esp_image_header_t;
typedef struct { uint32_t address; uint32_t size; } esp_image_segment_header_t;
typedef struct { char project_name[32]; char version[32]; } esp_app_desc_t;
#define ESP_IMAGE_HEADER_MAGIC 0xe9
typedef struct { size_t size; } esp_partition_t;
typedef unsigned esp_ota_handle_t;
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
esp_err_t esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *);
esp_err_t esp_ota_write(esp_ota_handle_t, const void *, size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
esp_err_t esp_ota_abort(esp_ota_handle_t);

typedef struct fake_http *esp_http_client_handle_t;
typedef struct {
    const char *url;
    esp_err_t (*crt_bundle_attach)(void *);
    unsigned timeout_ms, buffer_size;
    bool keep_alive_enable, disable_auto_redirect;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);

/* Fixed digest double: only tests comparison/control flow, not cryptography. */
typedef struct { unsigned unused; } mbedtls_sha256_context;
void mbedtls_sha256_init(mbedtls_sha256_context *);
void mbedtls_sha256_free(mbedtls_sha256_context *);
int mbedtls_sha256_starts(mbedtls_sha256_context *, int);
int mbedtls_sha256_update(mbedtls_sha256_context *, const unsigned char *, size_t);
int mbedtls_sha256_finish(mbedtls_sha256_context *, unsigned char *);
