#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Execute the production worker and HTTP/image checks with deterministic host
 * I/O. Including the source permits resetting its singleton between cases. */
#include "../src/update/online_firmware_update.c"

static void (*pending_task)(void *);
static bool fail_task, cancel_read, cancel_cleanup, bad_digest;
static bool check_cleanup_guard;
static esp_err_t session_error, music_error, write_error, end_error, boot_error;
static unsigned sessions, releases, begins, writes, ends, boot_selections;
static unsigned aborts, restarts, planned_restarts, http_requests;
static int http_status;
static char response[2048], last_manifest_url[256];
static unsigned char image_bytes[512];
static const esp_partition_t partition = {.size = 0x300000};
struct fake_http {
    const unsigned char *data;
    size_t length, offset;
};

const char *esp_err_to_name(esp_err_t error) { (void)error; return "test"; }
void test_log(const char *tag, const char *format, ...)
{ (void)tag; (void)format; }
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, TaskHandle_t *handle)
{
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    if (fail_task) return 0;
    assert(pending_task == NULL);
    pending_task = task;
    return pdPASS;
}
void vTaskDelete(TaskHandle_t handle) { (void)handle; }
void vTaskDelay(unsigned ticks) { (void)ticks; }
esp_err_t network_time_begin_online_session(unsigned timeout)
{ (void)timeout; ++sessions; return session_error; }
esp_err_t network_time_end_online_session(void)
{
    ++releases;
    if (check_cleanup_guard) {
        assert(online_firmware_update_request_check() == ESP_ERR_INVALID_STATE);
        assert(online_firmware_update_start_install() == ESP_ERR_INVALID_STATE);
        assert(online_firmware_update_set_beta_channel(false) == ESP_ERR_INVALID_STATE);
    }
    if (cancel_cleanup) {
        cancel_cleanup = false;
        assert(online_firmware_update_cancel() == ESP_OK);
    }
    return ESP_OK;
}
esp_err_t audio_music_stop_and_wait(unsigned timeout)
{ (void)timeout; return music_error; }
void boot_recovery_note_planned_restart(void) { ++planned_restarts; }
void esp_restart(void) { ++restarts; }
esp_err_t esp_crt_bundle_attach(void *config) { (void)config; return ESP_OK; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p)
{ (void)p; return &partition; }
esp_err_t esp_ota_begin(const esp_partition_t *p, size_t size, esp_ota_handle_t *handle)
{ assert(p == &partition && size == sizeof(image_bytes)); ++begins; *handle = 1; return ESP_OK; }
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size)
{ assert(handle == 1 && data != NULL && size > 0); ++writes; return write_error; }
esp_err_t esp_ota_end(esp_ota_handle_t handle)
{ assert(handle == 1); ++ends; return end_error; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p)
{ assert(p == &partition); ++boot_selections; return boot_error; }
esp_err_t esp_ota_abort(esp_ota_handle_t handle)
{ assert(handle == 1); ++aborts; return ESP_OK; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    assert(config->crt_bundle_attach != NULL && config->disable_auto_redirect);
    struct fake_http *http = calloc(1, sizeof(*http));
    assert(http != NULL);
    ++http_requests;
    if (strstr(config->url, ".json") != NULL) {
        snprintf(last_manifest_url, sizeof(last_manifest_url), "%s", config->url);
        http->data = (const unsigned char *)response;
        http->length = strlen(response);
    } else {
        http->data = image_bytes;
        http->length = sizeof(image_bytes);
    }
    return http;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t http, int length)
{ (void)http; (void)length; return ESP_OK; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t http)
{ return (int64_t)http->length; }
int esp_http_client_get_status_code(esp_http_client_handle_t http)
{ (void)http; return http_status; }
int esp_http_client_read(esp_http_client_handle_t http, char *data, int capacity)
{
    if (cancel_read) {
        cancel_read = false;
        assert(online_firmware_update_cancel() == ESP_OK);
    }
    size_t length = http->length - http->offset;
    if (length > (size_t)capacity) length = (size_t)capacity;
    memcpy(data, http->data + http->offset, length);
    http->offset += length;
    return (int)length;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t http)
{ return http->offset == http->length; }
esp_err_t esp_http_client_close(esp_http_client_handle_t http)
{ (void)http; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t http)
{ free(http); return ESP_OK; }
void mbedtls_sha256_init(mbedtls_sha256_context *ctx) { ctx->unused = 0; }
void mbedtls_sha256_free(mbedtls_sha256_context *ctx) { (void)ctx; }
int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224)
{ (void)ctx; assert(is224 == 0); return 0; }
int mbedtls_sha256_update(mbedtls_sha256_context *ctx, const unsigned char *data, size_t size)
{ (void)ctx; assert(data != NULL && size > 0); return 0; }
int mbedtls_sha256_finish(mbedtls_sha256_context *ctx, unsigned char *hash)
{ (void)ctx; memset(hash, bad_digest ? 1 : 0, 32); return 0; }

static void serve(const char *version, const char *channel, const char *hardware)
{
    int length = snprintf(response, sizeof(response),
        "{\"schema\":1,\"channel\":\"%s\",\"project\":\"esp32-rlcd-firmware\","
        "\"hardware\":\"%s\",\"version\":\"%s\",\"published_at\":\"2026-09-14\","
        "\"minimum_ota_version\":\"0.7.0\",\"ota\":{"
        "\"filename\":\"esp32-rlcd-firmware-v%s-ota.bin\","
        "\"url\":\"https://mcu.taifua.com/esp32-rlcd/firmware/testing/v%s/app.bin\","
        "\"size\":512,\"sha256\":\"%064d\"}}",
        channel, hardware, version, version, version, 0);
    assert(length > 0 && (size_t)length < sizeof(response));
    memset(image_bytes, 0, sizeof(image_bytes));
    image_bytes[0] = ESP_IMAGE_HEADER_MAGIC;
    esp_app_desc_t description = {0};
    snprintf(description.project_name, sizeof(description.project_name), "rlcd_firmware");
    snprintf(description.version, sizeof(description.version), "%s", version);
    memcpy(image_bytes + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
           &description, sizeof(description));
}

static void serve_beta(const char *version)
{ serve(version, "testing", "waveshare-esp32-s3-rlcd-4.2"); }

static void reset_service(const char *version, bool beta)
{
    assert(pending_task == NULL);
    s_initialized = s_cancel_requested = s_manifest_valid = s_task_active = false;
    s_review_after_check = false;
    s_channel = ONLINE_UPDATE_CHANNEL_STABLE;
    memset(&s_manifest, 0, sizeof(s_manifest));
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = ONLINE_UPDATE_STATE_IDLE;
    fail_task = cancel_read = cancel_cleanup = bad_digest = false;
    check_cleanup_guard = true;
    session_error = music_error = write_error = end_error = boot_error = ESP_OK;
    sessions = releases = begins = writes = ends = boot_selections = 0;
    aborts = restarts = planned_restarts = http_requests = 0;
    http_status = 200;
    last_manifest_url[0] = '\0';
    assert(online_firmware_update_init(version, beta) == ESP_OK);
    serve_beta("0.28.0-dev.2");
}

static void run_worker(void)
{
    assert(pending_task != NULL);
    void (*task)(void *) = pending_task;
    pending_task = NULL;
    task(NULL);
    if (restarts == 0) assert(!s_task_active);
}

static void expect_no_install(void)
{
    assert(begins == 0 && writes == 0 && boot_selections == 0);
    assert(restarts == 0 && planned_restarts == 0);
    assert(s_status.state != ONLINE_UPDATE_STATE_SUCCESS);
}

static void review(void)
{
    assert(online_firmware_update_request_confirmation() == ESP_OK);
    assert(s_status.state == ONLINE_UPDATE_STATE_CHECKING);
    run_worker();
    assert(s_status.state == ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION);
}

static void install(void)
{
    assert(online_firmware_update_start_install() == ESP_OK);
    run_worker();
}

static void test_stale_target(void)
{
    reset_service("0.27.0-dev.2", true);
    serve_beta("0.27.0"); /* Testing may temporarily point at a formal build. */
    assert(online_firmware_update_request_check() == ESP_OK);
    run_worker();
    assert(s_status.state == ONLINE_UPDATE_STATE_AVAILABLE);
    assert(strcmp(s_status.latest_version, "0.27.0") == 0);
    serve_beta("0.28.0-dev.2");
    review();
    assert(strcmp(s_status.latest_version, "0.28.0-dev.2") == 0);
    assert(!s_status.target_changed);
    expect_no_install();
    install();
    assert(s_status.state == ONLINE_UPDATE_STATE_SUCCESS);
    assert(begins == 1 && writes > 0 && ends == 1 && boot_selections == 1);
    assert(restarts == 1 && planned_restarts == 1 && sessions == releases);
}

static void test_retarget_requires_confirmation(void)
{
    reset_service("0.28.0-dev.1", true);
    review();
    serve_beta("0.28.0-dev.3");
    install();
    assert(s_status.state == ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION);
    assert(strcmp(s_status.latest_version, "0.28.0-dev.3") == 0);
    assert(s_status.target_changed);
    expect_no_install();
    install();
    assert(s_status.state == ONLINE_UPDATE_STATE_SUCCESS && restarts == 1);
}

static void test_rejected_targets_never_restart(void)
{
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        reset_service("0.28.0-dev.1", true);
        review();
        switch (scenario) {
        case 0: serve("0.28.0-dev.2", "testing", "wrong-board"); break;
        case 1: serve("0.28.0-dev.2", "stable", "waveshare-esp32-s3-rlcd-4.2"); break;
        case 2: serve_beta("0.28.0-dev.1"); break;
        case 3: serve_beta("0.27.0"); break;
        case 4: snprintf(response, sizeof(response), "{}"); break;
        case 5: http_status = 503; break;
        case 6: {
            char *minimum = strstr(response, "\"0.7.0\"");
            assert(minimum != NULL);
            memcpy(minimum, "\"9.7.0\"", 7);
            break;
        }
        default: {
            char *digest = strstr(response, "\"sha256\":\"");
            assert(digest != NULL);
            digest[10] = 'z';
            break;
        }
        }
        install();
        assert(s_status.state == ((scenario == 2 || scenario == 3)
                   ? ONLINE_UPDATE_STATE_UP_TO_DATE : ONLINE_UPDATE_STATE_FAILED));
        expect_no_install();
    }
}

static void test_image_failure_never_restarts(void)
{
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        reset_service("0.28.0-dev.1", true);
        review();
        switch (scenario) {
        case 0: image_bytes[0] = 0; break;
        case 1: bad_digest = true; break;
        case 2: write_error = ESP_FAIL; break;
        case 3: end_error = ESP_FAIL; break;
        default: boot_error = ESP_FAIL; break;
        }
        install();
        assert(s_status.state == ONLINE_UPDATE_STATE_FAILED);
        assert(restarts == 0 && planned_restarts == 0);
        if (scenario < 4) assert(boot_selections == 0);
        if (scenario == 1 || scenario == 2) assert(aborts == 1);
    }
}

static void test_channels_and_cancellation(void)
{
    reset_service("0.27.0-dev.2", false);
    serve("0.27.0", "stable", "waveshare-esp32-s3-rlcd-4.2");
    review();
    assert(strcmp(last_manifest_url, STABLE_MANIFEST_URL) == 0);
    assert(!s_status.beta_channel);
    assert(online_firmware_update_cancel() == ESP_OK);
    assert(online_firmware_update_set_beta_channel(true) == ESP_OK);
    assert(s_status.state == ONLINE_UPDATE_STATE_IDLE && !s_manifest_valid);
    serve_beta("0.28.0-dev.2");
    review();
    assert(strcmp(last_manifest_url, TESTING_MANIFEST_URL) == 0);
    assert(s_status.beta_channel);
    cancel_read = true;
    install();
    assert(s_status.state == ONLINE_UPDATE_STATE_AVAILABLE);
    expect_no_install();
    cancel_cleanup = true;
    assert(online_firmware_update_request_confirmation() == ESP_OK);
    run_worker();
    assert(s_status.state == ONLINE_UPDATE_STATE_AVAILABLE);
    expect_no_install();

    reset_service("0.28.0-dev.1", true);
    cancel_read = true;
    assert(online_firmware_update_request_check() == ESP_OK);
    run_worker();
    assert(s_status.state == ONLINE_UPDATE_STATE_IDLE);
    expect_no_install();
}

static void test_failures_release_only_owned_resources(void)
{
    reset_service("0.28.0-dev.1", true);
    fail_task = true;
    assert(online_firmware_update_request_confirmation() == ESP_ERR_NO_MEM);
    assert(!s_task_active && s_status.state == ONLINE_UPDATE_STATE_FAILED);
    fail_task = false;
    session_error = ESP_ERR_TIMEOUT;
    assert(online_firmware_update_request_confirmation() == ESP_OK);
    run_worker();
    assert(releases == 0 && http_requests == 0);
    expect_no_install();
    session_error = ESP_OK;
    review();
    fail_task = true;
    assert(online_firmware_update_start_install() == ESP_ERR_NO_MEM);
    assert(!s_task_active && s_status.state == ONLINE_UPDATE_STATE_FAILED);
    fail_task = false;
    review();
    session_error = ESP_ERR_TIMEOUT;
    unsigned previous_releases = releases;
    install();
    assert(releases == previous_releases);
    expect_no_install();
}

int main(void)
{
    test_stale_target();
    test_retarget_requires_confirmation();
    test_rejected_targets_never_restart();
    test_image_failure_never_restarts();
    test_channels_and_cancellation();
    test_failures_release_only_owned_resources();
    puts("online update service tests passed");
    return 0;
}
