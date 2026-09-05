#include "audio_diagnostics.h"
#include "audio_alert.h"
#include "audio_conversation.h"
#include "audio_conversation_control.h"
#include "audio_conversation_flow.h"
#include "audio_response_watchdog.h"
#include "audio_voice.h"
#include "audio_music.h"
#include "music_stream.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_level.h"
#include "audio_mono.h"
#include "voice_command_policy.h"
#include "voice_model_manifest.h"
#include "board_pins.h"
#include "conversation_client.h"
#include "conversation_config.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "es7210_adc.h"
#include "es8311_codec.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_partition.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "model_path.h"
#include "mbedtls/sha256.h"
#include "network_time.h"
#include "spi_flash_mmap.h"

enum {
    AUDIO_I2S_PORT = I2S_NUM_0,
    AUDIO_DMA_DESCRIPTOR_COUNT = 6,
    AUDIO_DMA_FRAME_COUNT = 240,
    AUDIO_CAPTURE_CHANNEL_COUNT = 4,
    AUDIO_RECORDING_CHANNEL_COUNT = 2,
    /* Waveshare's codec_board default maps its two capsules to ES7210
     * MIC1/MIC3, which occupy slots 0/2 in the four-slot TDM stream. */
    AUDIO_CAPTURE_MICROPHONE_1_SLOT = 0,
    AUDIO_CAPTURE_MICROPHONE_2_SLOT = 2,
    AUDIO_CAPTURE_CHUNK_FRAMES = 240,
    AUDIO_CAPTURE_SETTLE_CHUNKS = 10,
    AUDIO_LIVE_LEVEL_CHUNKS = 20,
    AUDIO_MAX_RECORDING_FRAMES =
        AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ *
        AUDIO_DIAGNOSTICS_MAX_RECORDING_MS / 1000U,
    AUDIO_TONE_CHUNK_FRAMES = 240,
    AUDIO_TONE_NOTE_MS = 180,
    AUDIO_TONE_GAP_MS = 60,
    AUDIO_TONE_FADE_MS = 15,
    AUDIO_TONE_AMPLITUDE = 7000,
    /* Give speech a conservative intelligibility boost while the digital
     * limiter still caps its peak. */
    AUDIO_MICROPHONE_GAIN_DB = 30,
    AUDIO_PREPARE_RECORDING_MS = 300,
    AUDIO_CAPTURE_TO_PLAYBACK_MS = 150,
    AUDIO_PLAYBACK_FADE_MS = 10,
    AUDIO_PLAYBACK_TARGET_PEAK = 12000,
    AUDIO_PLAYBACK_GAIN_ONE = 4096,
    AUDIO_PLAYBACK_MAX_GAIN = AUDIO_PLAYBACK_GAIN_ONE * 4,
    /* The same worker owns speech and MP3 decoding. Espressif recommends at
     * least 20 KiB for audio decoders; leave margin for our bounded SD calls. */
    AUDIO_WORKER_STACK_SIZE = 24576,
    AUDIO_WORKER_PRIORITY = 4,
    AUDIO_VOICE_PREPARE_STACK_SIZE = 8192,
    AUDIO_VOICE_PREPARE_PRIORITY = 1,
    AUDIO_ALERT_NOTE_MS = 170,
    AUDIO_ALERT_GAP_MS = 70,
    AUDIO_ALERT_REPEAT_GAP_MS = 900,
    AUDIO_ALERT_SAFETY_TIMEOUT_MS = 65U * 1000U,
    AUDIO_VOICE_MODEL_OFFSET = 0x610000,
    AUDIO_VOICE_MODEL_SIZE = 0x300000,
    AUDIO_VOICE_MODEL_HEADER_BYTES = 36,
    AUDIO_VOICE_MODEL_HASH_CHUNK_BYTES = 4096,
    AUDIO_VOICE_MODEL_MMAP_PAGE_BYTES = 64 * 1024,
    AUDIO_VOICE_MAX_MODEL_COUNT = 8,
    AUDIO_VOICE_CAPTURE_SETTLE_CHUNKS = 4,
    AUDIO_VOICE_DETECTION_THRESHOLD_PERCENT = 65,
    AUDIO_CONVERSATION_CAPTURE_CHUNK_FRAMES = 1600,
    AUDIO_CONVERSATION_PLAYBACK_CHUNK_FRAMES = 2400,
    AUDIO_CONVERSATION_CAPTURE_SETTLE_CHUNKS = 2,
    /* Cloud is selected only from an already-connected station snapshot. Keep
     * this confirmation short so a link-loss race cannot delay an alarm on the
     * shared audio worker for the generic network timeout. */
    AUDIO_CONVERSATION_NETWORK_TIMEOUT_MS = 1000,
    AUDIO_CONVERSATION_PROTOCOL_TIMEOUT_MS = 15000,
    AUDIO_CONVERSATION_RELEASE_TIMEOUT_MS = 5000,
    AUDIO_CONVERSATION_STOP_TIMEOUT_MS = 2000,
    AUDIO_CONVERSATION_POLL_MS = 50,
};

static const char *TAG = "audio_diagnostics";
static uint8_t s_playback_volume =
    AUDIO_DIAGNOSTICS_DEFAULT_PLAYBACK_VOLUME;

typedef enum {
    AUDIO_CONTROL_NONE = 0,
    AUDIO_CONTROL_STOP,
    AUDIO_CONTROL_CANCEL,
} audio_control_t;

typedef enum {
    AUDIO_WORK_NONE = 0,
    AUDIO_WORK_DIAGNOSTIC,
    AUDIO_WORK_VOICE,
    AUDIO_WORK_CONVERSATION,
    AUDIO_WORK_ALERT,
    AUDIO_WORK_MUSIC,
} audio_work_t;

typedef struct {
    i2s_chan_handle_t tx_channel;
    i2s_chan_handle_t rx_channel;
    const audio_codec_data_if_t *data_interface;
    const audio_codec_gpio_if_t *gpio_interface;
    const audio_codec_ctrl_if_t *speaker_control;
    const audio_codec_if_t *speaker_codec;
    esp_codec_dev_handle_t speaker_device;
    const audio_codec_ctrl_if_t *microphone_control;
    const audio_codec_if_t *microphone_codec;
    esp_codec_dev_handle_t microphone_device;
    SemaphoreHandle_t mutex;
    TaskHandle_t worker_task;
    TaskHandle_t voice_prepare_task;
    bool stop_requested;
    bool cancel_requested;
    bool alert_cancelled_session;
    bool diagnostic_requested;
    bool voice_requested;
    bool conversation_requested;
    bool conversation_release_requested;
    bool conversation_accepting_commands;
    audio_conversation_control_t conversation_control;
    bool alert_requested;
    bool alert_running;
    bool alert_stop_requested;
    bool music_requested;
    bool music_running;
    bool music_storage_change;
    uint32_t music_generation;
    audio_music_status_t music_status;
    audio_diagnostics_status_t status;
    audio_voice_status_t voice_status;
    audio_conversation_status_t conversation_status;
    voice_reliability_summary_t voice_reliability;
    esp_pm_lock_handle_t voice_cpu_lock;
} audio_diagnostics_context_t;

static audio_diagnostics_context_t s_audio;
static srmodel_list_t *s_voice_models;
static const esp_mn_iface_t *s_voice_multinet;
static model_iface_data_t *s_voice_model_data;
static const esp_partition_t *s_voice_model_partition;

static void secure_wipe(void *buffer, size_t size);
static void release_audio_workspace(void *workspace,
                                    size_t workspace_bytes);

static uint32_t size_to_u32(size_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static voice_reliability_resources_t capture_voice_resources(void)
{
    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const voice_reliability_resources_t resources = {
        .internal_free_bytes = size_to_u32(
            heap_caps_get_free_size(internal_caps)),
        .internal_largest_block_bytes = size_to_u32(
            heap_caps_get_largest_free_block(internal_caps)),
        .psram_free_bytes = size_to_u32(
            heap_caps_get_free_size(psram_caps)),
        .psram_largest_block_bytes = size_to_u32(
            heap_caps_get_largest_free_block(psram_caps)),
        .worker_stack_hwm_bytes = size_to_u32(
            uxTaskGetStackHighWaterMark2(NULL)),
    };
    return resources;
}

static voice_reliability_outcome_t voice_reliability_outcome(
    audio_voice_result_t result)
{
    switch (result) {
    case AUDIO_VOICE_RESULT_MATCHED:
        return VOICE_RELIABILITY_OUTCOME_MATCHED;
    case AUDIO_VOICE_RESULT_NO_VOICE:
        return VOICE_RELIABILITY_OUTCOME_NO_VOICE;
    case AUDIO_VOICE_RESULT_NOT_UNDERSTOOD:
        return VOICE_RELIABILITY_OUTCOME_NOT_UNDERSTOOD;
    case AUDIO_VOICE_RESULT_CANCELLED:
        return VOICE_RELIABILITY_OUTCOME_CANCELLED;
    case AUDIO_VOICE_RESULT_FAILED:
    case AUDIO_VOICE_RESULT_NONE:
    default:
        return VOICE_RELIABILITY_OUTCOME_FAILED;
    }
}

static uint16_t confidence_permille(float confidence)
{
    if (confidence <= 0.0F) {
        return 0U;
    }
    if (confidence >= 1.0F) {
        return 1000U;
    }
    return (uint16_t)(confidence * 1000.0F + 0.5F);
}

static esp_err_t codec_error(int error)
{
    return error == ESP_CODEC_DEV_OK ? ESP_OK : (esp_err_t)error;
}

static void lock_context(void)
{
    if (s_audio.mutex != NULL) {
        (void)xSemaphoreTake(s_audio.mutex, portMAX_DELAY);
    }
}

static void unlock_context(void)
{
    if (s_audio.mutex != NULL) {
        (void)xSemaphoreGive(s_audio.mutex);
    }
}

static void bump_revision_locked(void)
{
    ++s_audio.status.revision;
    if (s_audio.status.revision == 0U) {
        s_audio.status.revision = 1U;
    }
}

static void bump_voice_revision_locked(void)
{
    ++s_audio.voice_status.revision;
    if (s_audio.voice_status.revision == 0U) {
        s_audio.voice_status.revision = 1U;
    }
}

static void bump_conversation_revision_locked(void)
{
    ++s_audio.conversation_status.revision;
    if (s_audio.conversation_status.revision == 0U) {
        s_audio.conversation_status.revision = 1U;
    }
}

static void set_conversation_state(audio_conversation_state_t state)
{
    lock_context();
    s_audio.conversation_status.state = state;
    s_audio.conversation_status.running =
        state == AUDIO_CONVERSATION_STATE_CONNECTING ||
        state == AUDIO_CONVERSATION_STATE_WAITING_FOR_RELEASE ||
        state == AUDIO_CONVERSATION_STATE_LISTENING ||
        state == AUDIO_CONVERSATION_STATE_THINKING ||
        state == AUDIO_CONVERSATION_STATE_SPEAKING ||
        state == AUDIO_CONVERSATION_STATE_ADVANCING ||
        state == AUDIO_CONVERSATION_STATE_FOLLOW_UP;
    bump_conversation_revision_locked();
    unlock_context();
}

static void update_conversation_elapsed(uint32_t elapsed_ms,
                                        uint32_t session_elapsed_ms)
{
    lock_context();
    s_audio.conversation_status.elapsed_ms = elapsed_ms;
    s_audio.conversation_status.session_elapsed_ms = session_elapsed_ms;
    bump_conversation_revision_locked();
    unlock_context();
}

static void update_conversation_flow_status(
    const audio_conversation_flow_t *flow)
{
    if (flow == NULL) {
        return;
    }
    lock_context();
    s_audio.conversation_status.session_elapsed_ms =
        flow->session_elapsed_ms;
    s_audio.conversation_status.turn_number = flow->turn_number;
    s_audio.conversation_status.max_turns =
        AUDIO_CONVERSATION_MAX_TURNS;
    bump_conversation_revision_locked();
    unlock_context();
}

static void update_conversation_remote_status(
    const conversation_client_status_t *status)
{
    if (status == NULL) {
        return;
    }
    lock_context();
    secure_wipe(s_audio.conversation_status.transcript,
                sizeof(s_audio.conversation_status.transcript));
    snprintf(s_audio.conversation_status.transcript,
             sizeof(s_audio.conversation_status.transcript), "%s",
             status->transcript);
    secure_wipe(s_audio.conversation_status.response,
                sizeof(s_audio.conversation_status.response));
    snprintf(s_audio.conversation_status.response,
             sizeof(s_audio.conversation_status.response), "%s",
             status->response);
    s_audio.conversation_status.service_error_code =
        status->service_error_code;
    secure_wipe(s_audio.conversation_status.service_error_name,
                sizeof(s_audio.conversation_status.service_error_name));
    snprintf(s_audio.conversation_status.service_error_name,
             sizeof(s_audio.conversation_status.service_error_name), "%s",
             status->service_error_name);
    bump_conversation_revision_locked();
    unlock_context();
}

static bool conversation_release_requested(void)
{
    lock_context();
    const bool requested = s_audio.conversation_release_requested;
    unlock_context();
    return requested;
}

static bool take_conversation_continue(void)
{
    lock_context();
    const bool requested = !s_audio.cancel_requested &&
        audio_conversation_control_take_continue(
            &s_audio.conversation_control);
    unlock_context();
    return requested;
}

static void publish_conversation_advancing(void)
{
    lock_context();
    s_audio.conversation_status.state =
        AUDIO_CONVERSATION_STATE_ADVANCING;
    s_audio.conversation_status.running = true;
    bump_conversation_revision_locked();
    unlock_context();
}

static bool take_conversation_end(void)
{
    lock_context();
    const bool requested = !s_audio.cancel_requested &&
        audio_conversation_control_take_end(
            &s_audio.conversation_control);
    unlock_context();
    return requested;
}

static bool take_conversation_cancel(void)
{
    lock_context();
    const bool cancelled = s_audio.cancel_requested;
    if (cancelled) {
        s_audio.conversation_accepting_commands = false;
        s_audio.cancel_requested = false;
        s_audio.stop_requested = false;
        audio_conversation_control_reset(
            &s_audio.conversation_control);
        s_audio.alert_cancelled_session = false;
    }
    unlock_context();
    return cancelled;
}

static void close_conversation_command_gate(void)
{
    lock_context();
    s_audio.conversation_accepting_commands = false;
    audio_conversation_control_reset(&s_audio.conversation_control);
    unlock_context();
}

static void mark_conversation_session_ending(bool *end_session)
{
    if (end_session != NULL) {
        *end_session = true;
    }
    close_conversation_command_gate();
}

static void set_voice_state(audio_voice_state_t state)
{
    lock_context();
    s_audio.voice_status.state = state;
    s_audio.voice_status.running =
        state == AUDIO_VOICE_STATE_PREPARING ||
        state == AUDIO_VOICE_STATE_LISTENING;
    bump_voice_revision_locked();
    unlock_context();
}

static void update_voice_elapsed(uint32_t elapsed_ms)
{
    lock_context();
    s_audio.voice_status.elapsed_ms = elapsed_ms;
    bump_voice_revision_locked();
    unlock_context();
}

static uint32_t read_little_endian_u32(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static bool voice_model_header_is_valid(const esp_partition_t *partition)
{
    if (partition == NULL ||
        partition->size < AUDIO_VOICE_MODEL_HEADER_BYTES) {
        return false;
    }

    uint8_t header[AUDIO_VOICE_MODEL_HEADER_BYTES] = {0};
    const esp_err_t error = esp_partition_read(
        partition, 0U, header, sizeof(header));
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "could not read voice model header: %s",
                 esp_err_to_name(error));
        return false;
    }

    const uint32_t model_count = read_little_endian_u32(header);
    const bool valid =
        model_count > 0U && model_count <= AUDIO_VOICE_MAX_MODEL_COUNT &&
        header[4] == 'm' && header[5] == 'n' &&
        memchr(&header[4], '\0', 32U) != NULL;
    secure_wipe(header, sizeof(header));
    return valid;
}

static int hex_digit_value(char digit)
{
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    return -1;
}

static bool sha256_matches_hex(const uint8_t actual[32],
                               const char *expected_hex)
{
    if (actual == NULL || expected_hex == NULL ||
        strlen(expected_hex) != 64U) {
        return false;
    }
    uint8_t difference = 0U;
    for (size_t index = 0U; index < 32U; ++index) {
        const int high = hex_digit_value(expected_hex[index * 2U]);
        const int low = hex_digit_value(expected_hex[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        difference |= actual[index] ^
                      (uint8_t)((unsigned)high << 4U | (unsigned)low);
    }
    return difference == 0U;
}

static esp_err_t verify_voice_model_image(
    const esp_partition_t *partition)
{
    if (partition == NULL ||
        partition->size < AUDIO_VOICE_MODEL_IMAGE_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *buffer = heap_caps_malloc(
        AUDIO_VOICE_MODEL_HASH_CHUNK_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        buffer = heap_caps_malloc(AUDIO_VOICE_MODEL_HASH_CHUNK_BYTES,
                                  MALLOC_CAP_8BIT);
    }
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uint8_t actual_sha256[32] = {0};
    mbedtls_sha256_context digest;
    mbedtls_sha256_init(&digest);
    esp_err_t error = mbedtls_sha256_starts(&digest, 0) == 0
                          ? ESP_OK
                          : ESP_FAIL;
    size_t offset = 0U;
    while (error == ESP_OK &&
           offset < AUDIO_VOICE_MODEL_IMAGE_SIZE) {
        const size_t remaining =
            AUDIO_VOICE_MODEL_IMAGE_SIZE - offset;
        const size_t length =
            remaining < AUDIO_VOICE_MODEL_HASH_CHUNK_BYTES
                ? remaining
                : AUDIO_VOICE_MODEL_HASH_CHUNK_BYTES;
        error = esp_partition_read(partition, offset, buffer, length);
        if (error == ESP_OK &&
            mbedtls_sha256_update(&digest, buffer, length) != 0) {
            error = ESP_FAIL;
        }
        offset += error == ESP_OK ? length : 0U;
    }
    if (error == ESP_OK &&
        mbedtls_sha256_finish(&digest, actual_sha256) != 0) {
        error = ESP_FAIL;
    }
    mbedtls_sha256_free(&digest);
    if (error == ESP_OK &&
        !sha256_matches_hex(actual_sha256,
                            AUDIO_VOICE_MODEL_SHA256_HEX)) {
        error = ESP_ERR_INVALID_CRC;
    }
    secure_wipe(actual_sha256, sizeof(actual_sha256));
    release_audio_workspace(buffer, AUDIO_VOICE_MODEL_HASH_CHUNK_BYTES);
    return error;
}

static void set_voice_model_integrity(bool ready, esp_err_t error)
{
    lock_context();
    const bool changed = s_audio.voice_status.model_ready != ready ||
                         s_audio.voice_status.last_error != error;
    s_audio.voice_status.model_ready = ready;
    s_audio.voice_status.last_error = error;
    if (changed) {
        bump_voice_revision_locked();
    }
    unlock_context();
}

static const esp_partition_t *find_voice_model_partition(void)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "model");
    if (partition != NULL) {
        return partition;
    }

    const esp_err_t error = esp_partition_register_external(
        NULL, AUDIO_VOICE_MODEL_OFFSET, AUDIO_VOICE_MODEL_SIZE, "model",
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
        &partition);
    if (error != ESP_OK) {
        ESP_LOGW(TAG,
                 "legacy partition table has no usable voice model region: %s",
                 esp_err_to_name(error));
        return NULL;
    }
    ESP_LOGI(TAG,
             "registered legacy voice model region at 0x%x (%u bytes)",
             AUDIO_VOICE_MODEL_OFFSET, AUDIO_VOICE_MODEL_SIZE);
    return partition;
}

typedef struct {
    int command_id;
    const char *phrase;
} voice_phrase_t;

static const voice_phrase_t VOICE_PHRASES[] = {
    /* MultiNet Chinese command grammar uses space-separated pinyin. The
     * display presents the corresponding Han characters to the user. */
    {VOICE_COMMAND_ID_HOME, "hui dao zhu ye"},
    {VOICE_COMMAND_ID_HOME, "cha kan shi jian"},
    {VOICE_COMMAND_ID_CALENDAR, "da kai ri li"},
    {VOICE_COMMAND_ID_CALENDAR, "cha kan ri qi"},
    {VOICE_COMMAND_ID_STATUS, "cha kan zhuang tai"},
    {VOICE_COMMAND_ID_STATUS, "she bei zhuang tai"},
    {VOICE_COMMAND_ID_IMAGE, "da kai tu pian"},
    {VOICE_COMMAND_ID_IMAGE, "cha kan tu pian"},
    {VOICE_COMMAND_ID_SETTINGS, "da kai she zhi"},
    {VOICE_COMMAND_ID_SETTINGS, "cha kan she zhi"},
    {VOICE_COMMAND_ID_CANCEL, "qu xiao"},
};

static void release_voice_engine(bool commands_allocated)
{
    if (commands_allocated) {
        (void)esp_mn_commands_free();
    }
    if (s_voice_model_data != NULL && s_voice_multinet != NULL) {
        s_voice_multinet->destroy(s_voice_model_data);
    }
    s_voice_model_data = NULL;
    s_voice_multinet = NULL;
    if (s_voice_models != NULL) {
        esp_srmodel_deinit(s_voice_models);
    }
    s_voice_models = NULL;
}

static esp_err_t prepare_voice_engine(void)
{
    if (s_voice_model_data != NULL && s_voice_multinet != NULL) {
        s_voice_multinet->clean(s_voice_model_data);
        return ESP_OK;
    }
    if (!voice_model_header_is_valid(s_voice_model_partition)) {
        set_voice_model_integrity(false, ESP_ERR_NOT_FOUND);
        return ESP_ERR_NOT_FOUND;
    }
    const esp_err_t integrity_error =
        verify_voice_model_image(s_voice_model_partition);
    if (integrity_error != ESP_OK) {
        set_voice_model_integrity(false, integrity_error);
        ESP_LOGW(TAG, "offline voice model integrity failed: %s",
                 esp_err_to_name(integrity_error));
        return integrity_error;
    }
    set_voice_model_integrity(true, ESP_OK);

    const size_t required_mmap_pages =
        (s_voice_model_partition->size +
         AUDIO_VOICE_MODEL_MMAP_PAGE_BYTES - 1U) /
        AUDIO_VOICE_MODEL_MMAP_PAGE_BYTES;
    const int free_mmap_pages =
        spi_flash_mmap_get_free_pages(ESP_PARTITION_MMAP_DATA);
    if (free_mmap_pages < 0 ||
        (size_t)free_mmap_pages < required_mmap_pages) {
        ESP_LOGW(TAG,
                 "offline voice model needs %u mmap pages; only %d available",
                 (unsigned)required_mmap_pages, free_mmap_pages);
        return ESP_ERR_NO_MEM;
    }

    s_voice_models = esp_srmodel_init("model");
    if (s_voice_models == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    char *model_name = esp_srmodel_filter(
        s_voice_models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (model_name == NULL) {
        release_voice_engine(false);
        return ESP_ERR_NOT_FOUND;
    }

    s_voice_multinet = esp_mn_handle_from_name(model_name);
    if (s_voice_multinet == NULL) {
        release_voice_engine(false);
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_voice_model_data = s_voice_multinet->create(
        model_name, AUDIO_VOICE_MAX_LISTENING_MS);
    if (s_voice_model_data == NULL) {
        release_voice_engine(false);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t error = esp_mn_commands_alloc(
        s_voice_multinet, s_voice_model_data);
    bool commands_allocated = error == ESP_OK;
    for (size_t index = 0U;
         error == ESP_OK && index < sizeof(VOICE_PHRASES) /
                                         sizeof(VOICE_PHRASES[0]);
         ++index) {
        error = esp_mn_commands_add(VOICE_PHRASES[index].command_id,
                                    VOICE_PHRASES[index].phrase);
    }
    if (error == ESP_OK && esp_mn_commands_update() != NULL) {
        error = ESP_ERR_INVALID_STATE;
    }
    if (error == ESP_OK && s_voice_multinet->set_det_threshold != NULL &&
        s_voice_multinet->set_det_threshold(
            s_voice_model_data,
            AUDIO_VOICE_DETECTION_THRESHOLD_PERCENT / 100.0F) != 0) {
        error = ESP_ERR_INVALID_STATE;
    }
    if (error != ESP_OK) {
        release_voice_engine(commands_allocated);
        return error;
    }

    s_voice_multinet->clean(s_voice_model_data);
    ESP_LOGI(TAG, "offline voice engine ready: model=%s commands=%u",
             model_name,
             (unsigned)(sizeof(VOICE_PHRASES) / sizeof(VOICE_PHRASES[0])));
    return ESP_OK;
}

static void voice_model_prepare_task(void *argument)
{
    (void)argument;
    const esp_err_t error = prepare_voice_engine();

    lock_context();
    s_audio.voice_prepare_task = NULL;
    s_audio.voice_status.engine_preparing = false;
    s_audio.voice_status.engine_ready = error == ESP_OK;
    s_audio.voice_status.last_error = error;
    bump_voice_revision_locked();
    unlock_context();

    if (error == ESP_OK) {
        ESP_LOGI(TAG,
                 "offline voice model prepared without claiming audio hardware");
    } else {
        ESP_LOGW(TAG, "offline voice model preparation failed: %s",
                 esp_err_to_name(error));
    }
    vTaskDelete(NULL);
}

static void remember_initialization_error(esp_err_t error)
{
    if (error != ESP_OK && s_audio.status.last_error == ESP_OK) {
        s_audio.status.last_error = error;
    }
}

static void set_state(audio_session_state_t state)
{
    lock_context();
    s_audio.status.state = state;
    s_audio.status.running = audio_session_state_is_active(state);
    bump_revision_locked();
    unlock_context();
}

static void set_tone_played(bool played)
{
    lock_context();
    s_audio.status.tone_played = played;
    bump_revision_locked();
    unlock_context();
}

static void update_recording_status(uint32_t elapsed_ms,
                                    const audio_level_result_t *levels)
{
    lock_context();
    s_audio.status.recording_elapsed_ms = elapsed_ms;
    if (levels != NULL) {
        s_audio.status.microphone_1_level_percent = levels->percent[0];
        s_audio.status.microphone_2_level_percent = levels->percent[1];
    }
    bump_revision_locked();
    unlock_context();
}

static void store_capture_result(uint32_t duration_ms,
                                 const audio_level_result_t *levels,
                                 uint8_t playback_microphone,
                                 audio_diagnostics_result_t result)
{
    lock_context();
    s_audio.status.microphone_capture_completed = true;
    s_audio.status.recording_elapsed_ms = duration_ms;
    s_audio.status.recording_duration_ms = duration_ms;
    s_audio.status.microphone_1_level_percent = levels->percent[0];
    s_audio.status.microphone_2_level_percent = levels->percent[1];
    s_audio.status.microphone_1_rms = levels->rms[0];
    s_audio.status.microphone_2_rms = levels->rms[1];
    s_audio.status.playback_microphone = playback_microphone;
    s_audio.status.result = result;
    bump_revision_locked();
    unlock_context();
}

static void update_playback_status(uint32_t elapsed_ms)
{
    lock_context();
    s_audio.status.playback_elapsed_ms = elapsed_ms;
    bump_revision_locked();
    unlock_context();
}

static audio_control_t take_control(void)
{
    audio_control_t control = AUDIO_CONTROL_NONE;
    lock_context();
    if (s_audio.cancel_requested) {
        control = AUDIO_CONTROL_CANCEL;
        s_audio.cancel_requested = false;
        s_audio.alert_cancelled_session = false;
        s_audio.stop_requested = false;
    } else if (s_audio.stop_requested) {
        control = AUDIO_CONTROL_STOP;
        s_audio.stop_requested = false;
    }
    unlock_context();
    return control;
}

static bool delay_with_cancel(uint32_t duration_ms)
{
    uint32_t elapsed_ms = 0U;
    while (elapsed_ms < duration_ms) {
        if (take_control() == AUDIO_CONTROL_CANCEL) {
            return false;
        }
        const uint32_t step_ms = duration_ms - elapsed_ms > 10U
                                     ? 10U
                                     : duration_ms - elapsed_ms;
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        elapsed_ms += step_ms;
    }
    return take_control() != AUDIO_CONTROL_CANCEL;
}

static void secure_wipe(void *buffer, size_t size)
{
    volatile uint8_t *bytes = buffer;
    while (size > 0U) {
        *bytes++ = 0U;
        --size;
    }
}

static void release_audio_workspace(void *workspace,
                                    size_t workspace_bytes)
{
    if (workspace != NULL) {
        secure_wipe(workspace, workspace_bytes);
        heap_caps_free(workspace);
    }
}

static esp_err_t initialize_i2s(void)
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = AUDIO_DMA_DESCRIPTOR_COUNT;
    channel_config.dma_frame_num = AUDIO_DMA_FRAME_COUNT;
    channel_config.auto_clear_after_cb = true;

    esp_err_t error = i2s_new_channel(&channel_config, &s_audio.tx_channel,
                                      &s_audio.rx_channel);
    if (error != ESP_OK) {
        return error;
    }

    i2s_std_config_t output_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(
            AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_AUDIO_MCLK_GPIO,
            .bclk = BOARD_AUDIO_BCLK_GPIO,
            .ws = BOARD_AUDIO_WS_GPIO,
            .dout = BOARD_AUDIO_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    output_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    error = i2s_channel_init_std_mode(s_audio.tx_channel, &output_config);
    if (error != ESP_OK) {
        return error;
    }

    const i2s_tdm_slot_mask_t input_slots =
        I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3;
    i2s_tdm_config_t input_config = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(
            AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, input_slots),
        .gpio_cfg = {
            .mclk = BOARD_AUDIO_MCLK_GPIO,
            .bclk = BOARD_AUDIO_BCLK_GPIO,
            .ws = BOARD_AUDIO_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = BOARD_AUDIO_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    input_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    input_config.clk_cfg.bclk_div = 8U;
    input_config.slot_cfg.total_slot = AUDIO_CAPTURE_CHANNEL_COUNT;
    error = i2s_channel_init_tdm_mode(s_audio.rx_channel, &input_config);
    if (error != ESP_OK) {
        return error;
    }

    audio_codec_i2s_cfg_t codec_i2s_config = {
        .port = AUDIO_I2S_PORT,
        .rx_handle = s_audio.rx_channel,
        .tx_handle = s_audio.tx_channel,
    };
    s_audio.data_interface = audio_codec_new_i2s_data(&codec_i2s_config);
    return s_audio.data_interface != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool initialize_speaker(i2c_master_bus_handle_t i2c_bus)
{
    audio_codec_i2c_cfg_t control_config = {
        .port = 0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    s_audio.speaker_control = audio_codec_new_i2c_ctrl(&control_config);
    if (s_audio.speaker_control == NULL) {
        return false;
    }

    if (s_audio.gpio_interface == NULL) {
        s_audio.gpio_interface = audio_codec_new_gpio();
    }
    if (s_audio.gpio_interface == NULL) {
        return false;
    }

    es8311_codec_cfg_t codec_config = {
        .ctrl_if = s_audio.speaker_control,
        .gpio_if = s_audio.gpio_interface,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BOARD_AUDIO_PA_GPIO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {
            .pa_voltage = 5.0F,
            .codec_dac_voltage = 3.3F,
            .pa_gain = 6.0F,
        },
        .no_dac_ref = false,
        .mclk_div = 0,
    };
    s_audio.speaker_codec = es8311_codec_new(&codec_config);
    if (s_audio.speaker_codec == NULL) {
        return false;
    }

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = s_audio.speaker_codec,
        .data_if = s_audio.data_interface,
    };
    s_audio.speaker_device = esp_codec_dev_new(&device_config);
    return s_audio.speaker_device != NULL;
}

static bool initialize_microphones(i2c_master_bus_handle_t i2c_bus)
{
    audio_codec_i2c_cfg_t control_config = {
        .port = 0,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    s_audio.microphone_control = audio_codec_new_i2c_ctrl(&control_config);
    if (s_audio.microphone_control == NULL) {
        return false;
    }

    es7210_codec_cfg_t codec_config = {
        .ctrl_if = s_audio.microphone_control,
        .master_mode = false,
        .mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC2 |
                        ES7120_SEL_MIC3 | ES7120_SEL_MIC4,
        .mclk_src = ES7210_MCLK_FROM_PAD,
        .mclk_div = 0,
    };
    s_audio.microphone_codec = es7210_codec_new(&codec_config);
    if (s_audio.microphone_codec == NULL) {
        return false;
    }

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = s_audio.microphone_codec,
        .data_if = s_audio.data_interface,
    };
    s_audio.microphone_device = esp_codec_dev_new(&device_config);
    return s_audio.microphone_device != NULL;
}

static int16_t triangle_sample(uint32_t *phase, uint32_t frequency_hz,
                               uint32_t frame, uint32_t total_frames)
{
    const uint32_t step =
        (frequency_hz * 65536U) / AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ;
    *phase = (*phase + step) & 0xffffU;
    const int32_t triangle =
        *phase < 32768U ? (int32_t)(*phase * 2U) - 32768
                        : 98302 - (int32_t)(*phase * 2U);
    const uint32_t fade_frames =
        (AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ * AUDIO_TONE_FADE_MS) / 1000U;
    uint32_t envelope = 1024U;
    if (frame < fade_frames) {
        envelope = (frame * 1024U) / fade_frames;
    } else if (total_frames - frame <= fade_frames) {
        envelope = ((total_frames - frame) * 1024U) / fade_frames;
    }
    return (int16_t)(((int64_t)triangle * AUDIO_TONE_AMPLITUDE *
                      (int32_t)envelope) /
                     (32768 * 1024));
}

static esp_err_t write_silence(int16_t *playback_buffer,
                               uint32_t duration_ms, bool *cancelled)
{
    memset(playback_buffer, 0,
           AUDIO_TONE_CHUNK_FRAMES * sizeof(playback_buffer[0]));
    uint32_t frames_remaining =
        (AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ * duration_ms) / 1000U;
    while (frames_remaining > 0U) {
        if (take_control() == AUDIO_CONTROL_CANCEL) {
            *cancelled = true;
            return ESP_OK;
        }
        const uint32_t frames = frames_remaining > AUDIO_TONE_CHUNK_FRAMES
                                    ? AUDIO_TONE_CHUNK_FRAMES
                                    : frames_remaining;
        const esp_err_t error = codec_error(esp_codec_dev_write(
            s_audio.speaker_device, playback_buffer,
            (int)(frames * sizeof(playback_buffer[0]))));
        if (error != ESP_OK) {
            return error;
        }
        frames_remaining -= frames;
    }
    return ESP_OK;
}

static esp_err_t write_note(int16_t *playback_buffer,
                            uint32_t frequency_hz, uint32_t duration_ms,
                            bool *cancelled)
{
    const uint32_t total_frames =
        (AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ * duration_ms) / 1000U;
    uint32_t frames_written = 0U;
    uint32_t phase = 0U;
    while (frames_written < total_frames) {
        if (take_control() == AUDIO_CONTROL_CANCEL) {
            *cancelled = true;
            return ESP_OK;
        }
        const uint32_t frames =
            total_frames - frames_written > AUDIO_TONE_CHUNK_FRAMES
                ? AUDIO_TONE_CHUNK_FRAMES
                : total_frames - frames_written;
        for (uint32_t frame = 0U; frame < frames; ++frame) {
            playback_buffer[frame] = triangle_sample(
                &phase, frequency_hz, frames_written + frame, total_frames);
        }
        const esp_err_t error = codec_error(esp_codec_dev_write(
            s_audio.speaker_device, playback_buffer,
            (int)(frames * sizeof(playback_buffer[0]))));
        if (error != ESP_OK) {
            return error;
        }
        frames_written += frames;
    }
    return ESP_OK;
}

static bool alert_should_stop(TickType_t started)
{
    bool requested = false;
    lock_context();
    requested = s_audio.alert_stop_requested;
    unlock_context();
    const uint32_t elapsed_ms =
        (uint32_t)(xTaskGetTickCount() - started) * portTICK_PERIOD_MS;
    return requested || elapsed_ms >= AUDIO_ALERT_SAFETY_TIMEOUT_MS;
}

static esp_err_t alert_write_silence(int16_t *playback_buffer,
                                     uint32_t duration_ms,
                                     TickType_t started, bool *stopped)
{
    memset(playback_buffer, 0,
           AUDIO_TONE_CHUNK_FRAMES * sizeof(playback_buffer[0]));
    uint32_t frames_remaining =
        (AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ * duration_ms) / 1000U;
    while (frames_remaining > 0U) {
        if (alert_should_stop(started)) {
            *stopped = true;
            return ESP_OK;
        }
        const uint32_t frames = frames_remaining > AUDIO_TONE_CHUNK_FRAMES
                                    ? AUDIO_TONE_CHUNK_FRAMES
                                    : frames_remaining;
        const esp_err_t error = codec_error(esp_codec_dev_write(
            s_audio.speaker_device, playback_buffer,
            (int)(frames * sizeof(playback_buffer[0]))));
        if (error != ESP_OK) {
            return error;
        }
        frames_remaining -= frames;
    }
    return ESP_OK;
}

static esp_err_t alert_write_note(int16_t *playback_buffer,
                                  uint32_t frequency_hz,
                                  uint32_t duration_ms,
                                  TickType_t started, bool *stopped)
{
    const uint32_t total_frames =
        (AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ * duration_ms) / 1000U;
    uint32_t frames_written = 0U;
    uint32_t phase = 0U;
    while (frames_written < total_frames) {
        if (alert_should_stop(started)) {
            *stopped = true;
            return ESP_OK;
        }
        const uint32_t frames =
            total_frames - frames_written > AUDIO_TONE_CHUNK_FRAMES
                ? AUDIO_TONE_CHUNK_FRAMES
                : total_frames - frames_written;
        for (uint32_t frame = 0U; frame < frames; ++frame) {
            playback_buffer[frame] = triangle_sample(
                &phase, frequency_hz, frames_written + frame,
                total_frames);
        }
        const esp_err_t error = codec_error(esp_codec_dev_write(
            s_audio.speaker_device, playback_buffer,
            (int)(frames * sizeof(playback_buffer[0]))));
        if (error != ESP_OK) {
            return error;
        }
        frames_written += frames;
    }
    return ESP_OK;
}

static esp_err_t open_speaker_at_rate(uint32_t sample_rate_hz, int volume)
{
    if (s_audio.speaker_device == NULL || sample_rate_hz == 0U ||
        sample_rate_hz > INT_MAX) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = AUDIO_DIAGNOSTICS_BITS_PER_SAMPLE,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = (int)sample_rate_hz,
        .mclk_multiple = 0,
    };
    esp_err_t error = codec_error(
        esp_codec_dev_open(s_audio.speaker_device, &format));
    if (error == ESP_OK) {
        error = codec_error(esp_codec_dev_set_out_vol(
            s_audio.speaker_device, volume));
    }
    if (error == ESP_OK) {
        error = codec_error(
            esp_codec_dev_set_out_mute(s_audio.speaker_device, false));
    }
    if (error != ESP_OK) {
        (void)esp_codec_dev_set_out_mute(s_audio.speaker_device, true);
        (void)esp_codec_dev_close(s_audio.speaker_device);
    }
    return error;
}

static esp_err_t open_speaker(int volume)
{
    return open_speaker_at_rate(AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ,
                                volume);
}

static esp_err_t close_speaker(void)
{
    (void)esp_codec_dev_set_out_mute(s_audio.speaker_device, true);
    return codec_error(esp_codec_dev_close(s_audio.speaker_device));
}

static esp_err_t play_test_tone(int16_t *playback_buffer, uint8_t volume,
                                bool *played, bool *cancelled)
{
    *played = false;
    *cancelled = false;
    if (volume == 0U) {
        if (take_control() == AUDIO_CONTROL_CANCEL) {
            *cancelled = true;
        }
        ESP_LOGI(TAG,
                 "diagnostic tone skipped because playback volume is 0%%");
        return ESP_OK;
    }

    esp_err_t error = open_speaker(volume);
    if (error != ESP_OK) {
        return error;
    }

    error = write_silence(playback_buffer, 30U, cancelled);
    if (error == ESP_OK && !*cancelled) {
        error = write_note(playback_buffer, 880U, AUDIO_TONE_NOTE_MS,
                           cancelled);
    }
    if (error == ESP_OK && !*cancelled) {
        error = write_silence(playback_buffer, AUDIO_TONE_GAP_MS,
                              cancelled);
    }
    if (error == ESP_OK && !*cancelled) {
        error = write_note(playback_buffer, 1175U, AUDIO_TONE_NOTE_MS,
                           cancelled);
    }
    if (error == ESP_OK && !*cancelled) {
        error = write_silence(playback_buffer, 30U, cancelled);
    }
    const esp_err_t close_error = close_speaker();
    const esp_err_t result = error != ESP_OK ? error : close_error;
    *played = result == ESP_OK && !*cancelled;
    return result;
}

static esp_err_t open_microphones_at_rate(uint32_t sample_rate_hz)
{
    if (s_audio.microphone_device == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = AUDIO_DIAGNOSTICS_BITS_PER_SAMPLE,
        .channel = AUDIO_CAPTURE_CHANNEL_COUNT,
        .channel_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                        I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
        .sample_rate = sample_rate_hz,
        .mclk_multiple = 0,
    };
    esp_err_t error = codec_error(
        esp_codec_dev_open(s_audio.microphone_device, &format));
    if (error == ESP_OK) {
        error = codec_error(esp_codec_dev_set_in_gain(
            s_audio.microphone_device, (float)AUDIO_MICROPHONE_GAIN_DB));
    }
    if (error != ESP_OK) {
        (void)esp_codec_dev_close(s_audio.microphone_device);
    }
    return error;
}

static esp_err_t open_microphones(void)
{
    return open_microphones_at_rate(AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ);
}

static esp_err_t settle_microphones(int16_t *capture_buffer,
                                    bool *cancelled)
{
    for (uint32_t chunk = 0U; chunk < AUDIO_CAPTURE_SETTLE_CHUNKS;
         ++chunk) {
        if (take_control() == AUDIO_CONTROL_CANCEL) {
            *cancelled = true;
            return ESP_OK;
        }
        const esp_err_t error = codec_error(esp_codec_dev_read(
            s_audio.microphone_device, capture_buffer,
            (int)(AUDIO_CAPTURE_CHUNK_FRAMES *
                  AUDIO_CAPTURE_CHANNEL_COUNT *
                  sizeof(capture_buffer[0]))));
        if (error != ESP_OK) {
            return error;
        }
    }
    return ESP_OK;
}

static esp_err_t capture_voice(int16_t *recording,
                               int16_t *capture_buffer,
                               uint32_t *recorded_frames,
                               audio_level_result_t *levels,
                               bool *cancelled)
{
    *recorded_frames = 0U;
    *cancelled = false;
    esp_err_t error = open_microphones();
    if (error != ESP_OK) {
        return error;
    }

    error = settle_microphones(capture_buffer, cancelled);
    if (error == ESP_OK && !*cancelled) {
        set_state(AUDIO_SESSION_STATE_RECORDING);
    }

    audio_level_accumulator_t total_accumulator;
    audio_level_accumulator_t live_accumulator;
    audio_level_init(&total_accumulator);
    audio_level_init(&live_accumulator);
    uint32_t live_chunks = 0U;
    bool stop = false;

    while (error == ESP_OK && !*cancelled && !stop &&
           *recorded_frames < AUDIO_MAX_RECORDING_FRAMES) {
        const audio_control_t control = take_control();
        if (control == AUDIO_CONTROL_CANCEL) {
            *cancelled = true;
            break;
        }
        if (control == AUDIO_CONTROL_STOP) {
            stop = true;
            break;
        }

        const uint32_t frames =
            AUDIO_MAX_RECORDING_FRAMES - *recorded_frames >
                    AUDIO_CAPTURE_CHUNK_FRAMES
                ? AUDIO_CAPTURE_CHUNK_FRAMES
                : AUDIO_MAX_RECORDING_FRAMES - *recorded_frames;
        error = codec_error(esp_codec_dev_read(
            s_audio.microphone_device, capture_buffer,
            (int)(frames * AUDIO_CAPTURE_CHANNEL_COUNT *
                  sizeof(capture_buffer[0]))));
        if (error != ESP_OK) {
            break;
        }
        if (!audio_level_add_tdm16(
                &total_accumulator, capture_buffer, frames,
                AUDIO_CAPTURE_CHANNEL_COUNT,
                AUDIO_CAPTURE_MICROPHONE_1_SLOT,
                AUDIO_CAPTURE_MICROPHONE_2_SLOT) ||
            !audio_level_add_tdm16(
                &live_accumulator, capture_buffer, frames,
                AUDIO_CAPTURE_CHANNEL_COUNT,
                AUDIO_CAPTURE_MICROPHONE_1_SLOT,
                AUDIO_CAPTURE_MICROPHONE_2_SLOT)) {
            error = ESP_ERR_INVALID_STATE;
            break;
        }
        for (uint32_t frame = 0U; frame < frames; ++frame) {
            recording[(*recorded_frames + frame) *
                          AUDIO_RECORDING_CHANNEL_COUNT] =
                capture_buffer[frame * AUDIO_CAPTURE_CHANNEL_COUNT +
                               AUDIO_CAPTURE_MICROPHONE_1_SLOT];
            recording[(*recorded_frames + frame) *
                          AUDIO_RECORDING_CHANNEL_COUNT +
                      1U] =
                capture_buffer[frame * AUDIO_CAPTURE_CHANNEL_COUNT +
                               AUDIO_CAPTURE_MICROPHONE_2_SLOT];
        }
        *recorded_frames += frames;
        ++live_chunks;
        if (live_chunks >= AUDIO_LIVE_LEVEL_CHUNKS) {
            audio_level_result_t live_levels = {0};
            if (!audio_level_finish(&live_accumulator, &live_levels)) {
                error = ESP_ERR_INVALID_STATE;
                break;
            }
            update_recording_status(
                (*recorded_frames * 1000U) /
                    AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ,
                &live_levels);
            audio_level_init(&live_accumulator);
            live_chunks = 0U;
        }
    }

    const esp_err_t close_error =
        codec_error(esp_codec_dev_close(s_audio.microphone_device));
    if (error == ESP_OK && !*cancelled && *recorded_frames > 0U &&
        !audio_level_finish(&total_accumulator, levels)) {
        error = ESP_ERR_INVALID_STATE;
    }
    if (error == ESP_OK && !*cancelled && *recorded_frames == 0U) {
        error = ESP_ERR_INVALID_SIZE;
    }
    return error != ESP_OK ? error : close_error;
}

static int16_t playback_sample(const int16_t *recording,
                               uint32_t frame, uint8_t microphone,
                               int32_t mean, uint32_t gain,
                               uint32_t total_frames)
{
    int64_t sample =
        (int32_t)recording[frame * AUDIO_RECORDING_CHANNEL_COUNT +
                           microphone] -
        mean;
    sample = sample * (int64_t)gain / AUDIO_PLAYBACK_GAIN_ONE;

    const uint32_t fade_frames =
        AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ * AUDIO_PLAYBACK_FADE_MS / 1000U;
    uint32_t envelope = AUDIO_PLAYBACK_GAIN_ONE;
    if (frame < fade_frames) {
        envelope = frame * AUDIO_PLAYBACK_GAIN_ONE / fade_frames;
    }
    const uint32_t remaining_frames = total_frames - frame;
    if (remaining_frames <= fade_frames) {
        const uint32_t tail =
            remaining_frames * AUDIO_PLAYBACK_GAIN_ONE / fade_frames;
        if (tail < envelope) {
            envelope = tail;
        }
    }
    sample = sample * (int64_t)envelope / AUDIO_PLAYBACK_GAIN_ONE;
    if (sample > INT16_MAX) {
        return INT16_MAX;
    }
    if (sample < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)sample;
}

static void playback_parameters(const int16_t *recording,
                                uint32_t recorded_frames,
                                uint8_t microphone, int32_t *mean,
                                uint32_t *gain)
{
    int64_t sum = 0;
    for (uint32_t frame = 0U; frame < recorded_frames; ++frame) {
        sum += recording[frame * AUDIO_RECORDING_CHANNEL_COUNT +
                         microphone];
    }
    *mean = (int32_t)(sum / recorded_frames);

    uint32_t peak = 0U;
    for (uint32_t frame = 0U; frame < recorded_frames; ++frame) {
        int32_t sample =
            (int32_t)recording[frame * AUDIO_RECORDING_CHANNEL_COUNT +
                               microphone] -
            *mean;
        const uint32_t magnitude =
            (uint32_t)(sample < 0 ? -sample : sample);
        if (magnitude > peak) {
            peak = magnitude;
        }
    }
    if (peak == 0U) {
        *gain = AUDIO_PLAYBACK_GAIN_ONE;
        return;
    }
    const uint64_t calculated =
        (uint64_t)AUDIO_PLAYBACK_TARGET_PEAK *
        AUDIO_PLAYBACK_GAIN_ONE / peak;
    *gain = calculated > AUDIO_PLAYBACK_MAX_GAIN
                ? AUDIO_PLAYBACK_MAX_GAIN
                : (uint32_t)calculated;
}

static esp_err_t play_recording(const int16_t *recording,
                                uint32_t recorded_frames,
                                int16_t *playback_buffer,
                                uint8_t microphone, uint8_t volume,
                                bool *stopped, bool *cancelled)
{
    *stopped = false;
    *cancelled = false;
    int32_t mean = 0;
    uint32_t gain = AUDIO_PLAYBACK_GAIN_ONE;
    playback_parameters(recording, recorded_frames, microphone, &mean,
                        &gain);

    esp_err_t error = open_speaker(volume);
    if (error != ESP_OK) {
        return error;
    }
    uint32_t frames_written = 0U;
    uint32_t last_status_ms = 0U;
    while (error == ESP_OK && frames_written < recorded_frames) {
        const audio_control_t control = take_control();
        if (control == AUDIO_CONTROL_CANCEL) {
            *cancelled = true;
            break;
        }
        if (control == AUDIO_CONTROL_STOP) {
            *stopped = true;
            break;
        }
        const uint32_t frames =
            recorded_frames - frames_written > AUDIO_TONE_CHUNK_FRAMES
                ? AUDIO_TONE_CHUNK_FRAMES
                : recorded_frames - frames_written;
        for (uint32_t frame = 0U; frame < frames; ++frame) {
            playback_buffer[frame] = playback_sample(
                recording, frames_written + frame, microphone, mean, gain,
                recorded_frames);
        }
        error = codec_error(esp_codec_dev_write(
            s_audio.speaker_device, playback_buffer,
            (int)(frames * sizeof(playback_buffer[0]))));
        if (error != ESP_OK) {
            break;
        }
        frames_written += frames;
        const uint32_t elapsed_ms =
            frames_written * 1000U / AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ;
        if (elapsed_ms - last_status_ms >= 200U ||
            frames_written == recorded_frames) {
            update_playback_status(elapsed_ms);
            last_status_ms = elapsed_ms;
        }
    }

    if (error == ESP_OK && !*stopped && !*cancelled) {
        error = write_silence(playback_buffer, 30U, cancelled);
    }

    const esp_err_t close_error = close_speaker();
    return error != ESP_OK ? error : close_error;
}

static void finish_cancelled(void)
{
    lock_context();
    s_audio.status.running = false;
    s_audio.status.test_completed = false;
    s_audio.status.voice_played = false;
    s_audio.status.playback_stopped = false;
    s_audio.status.last_error = ESP_OK;
    s_audio.status.state = AUDIO_SESSION_STATE_CANCELLED;
    s_audio.stop_requested = false;
    s_audio.cancel_requested = false;
    s_audio.alert_cancelled_session = false;
    bump_revision_locked();
    unlock_context();
    ESP_LOGI(TAG, "temporary audio session cancelled and cleared");
}

static void finish_failed(esp_err_t error)
{
    lock_context();
    s_audio.status.running = false;
    s_audio.status.test_completed = true;
    s_audio.status.voice_played = false;
    s_audio.status.result = AUDIO_DIAGNOSTICS_RESULT_FAILED;
    s_audio.status.last_error = error != ESP_OK ? error : ESP_FAIL;
    s_audio.status.state = AUDIO_SESSION_STATE_FAILED;
    s_audio.stop_requested = false;
    s_audio.cancel_requested = false;
    s_audio.alert_cancelled_session = false;
    bump_revision_locked();
    unlock_context();
    ESP_LOGW(TAG, "temporary audio session failed: %s",
             esp_err_to_name(error != ESP_OK ? error : ESP_FAIL));
}

static void finish_completed(bool voice_played, bool playback_stopped)
{
    lock_context();
    s_audio.status.running = false;
    s_audio.status.test_completed = true;
    s_audio.status.voice_played = voice_played;
    s_audio.status.playback_stopped = playback_stopped;
    s_audio.status.last_error = ESP_OK;
    s_audio.status.state = AUDIO_SESSION_STATE_COMPLETED;
    s_audio.stop_requested = false;
    s_audio.cancel_requested = false;
    s_audio.alert_cancelled_session = false;
    bump_revision_locked();
    const audio_diagnostics_status_t completed = s_audio.status;
    unlock_context();

    ESP_LOGI(TAG,
             "audio test: result=%s tone=%d mic1=%u%% rms=%u mic2=%u%% rms=%u playback=%s source=MIC%u duration=%ums",
             audio_diagnostics_result_name(completed.result),
             completed.tone_played,
             completed.microphone_1_level_percent,
             completed.microphone_1_rms,
             completed.microphone_2_level_percent,
             completed.microphone_2_rms,
             voice_played ? "played"
                          : (playback_stopped ? "stopped" : "not_played"),
             completed.playback_microphone,
             (unsigned)completed.recording_duration_ms);
}

static void finish_alert(esp_err_t error)
{
    lock_context();
    s_audio.alert_running = false;
    s_audio.alert_stop_requested = false;
    s_audio.alert_cancelled_session = false;
    unlock_context();
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "alert playback stopped");
    } else {
        ESP_LOGW(TAG, "alert playback failed: %s",
                 esp_err_to_name(error));
    }
}

static void run_audio_alert(void)
{
    int16_t playback_buffer[AUDIO_TONE_CHUNK_FRAMES];
    uint8_t volume = 0U;
    lock_context();
    volume = s_playback_volume;
    unlock_context();

    if (volume == 0U) {
        ESP_LOGI(TAG, "alert is silent because playback volume is 0%%");
        finish_alert(ESP_OK);
        return;
    }

    esp_err_t error = open_speaker(volume);
    if (error != ESP_OK) {
        finish_alert(error);
        return;
    }

    const TickType_t started = xTaskGetTickCount();
    bool stopped = false;
    while (error == ESP_OK && !stopped) {
        error = alert_write_note(playback_buffer, 784U,
                                 AUDIO_ALERT_NOTE_MS, started, &stopped);
        if (error == ESP_OK && !stopped) {
            error = alert_write_silence(playback_buffer,
                                        AUDIO_ALERT_GAP_MS, started,
                                        &stopped);
        }
        if (error == ESP_OK && !stopped) {
            error = alert_write_note(playback_buffer, 988U,
                                     AUDIO_ALERT_NOTE_MS, started,
                                     &stopped);
        }
        if (error == ESP_OK && !stopped) {
            error = alert_write_silence(playback_buffer,
                                        AUDIO_ALERT_GAP_MS, started,
                                        &stopped);
        }
        if (error == ESP_OK && !stopped) {
            error = alert_write_note(playback_buffer, 1175U,
                                     AUDIO_ALERT_NOTE_MS, started,
                                     &stopped);
        }
        if (error == ESP_OK && !stopped) {
            error = alert_write_silence(playback_buffer,
                                        AUDIO_ALERT_REPEAT_GAP_MS,
                                        started, &stopped);
        }
    }

    const esp_err_t close_error = close_speaker();
    finish_alert(error != ESP_OK ? error : close_error);
}

static bool commit_voice_session(
    const voice_reliability_sample_t *sample, bool cancelled,
    esp_err_t error, audio_voice_result_t result,
    bool speech_detected, int command_id, float confidence,
    uint32_t elapsed_ms, audio_voice_snapshot_t *snapshot)
{
    lock_context();
    const bool accepted = voice_reliability_summary_record(
        &s_audio.voice_reliability, sample);
    s_audio.voice_status.running = false;
    s_audio.voice_status.elapsed_ms = elapsed_ms;
    if (cancelled) {
        s_audio.voice_status.speech_detected = false;
        s_audio.voice_status.command_id = VOICE_COMMAND_ID_NONE;
        s_audio.voice_status.confidence = 0.0F;
        s_audio.voice_status.state = AUDIO_VOICE_STATE_CANCELLED;
        s_audio.voice_status.result = AUDIO_VOICE_RESULT_CANCELLED;
        s_audio.voice_status.last_error = ESP_OK;
    } else if (error != ESP_OK) {
        s_audio.voice_status.speech_detected = false;
        s_audio.voice_status.command_id = VOICE_COMMAND_ID_NONE;
        s_audio.voice_status.confidence = 0.0F;
        s_audio.voice_status.state = AUDIO_VOICE_STATE_FAILED;
        s_audio.voice_status.result = AUDIO_VOICE_RESULT_FAILED;
        s_audio.voice_status.last_error = error;
    } else {
        s_audio.voice_status.speech_detected = speech_detected;
        s_audio.voice_status.command_id = command_id;
        s_audio.voice_status.confidence = confidence;
        s_audio.voice_status.state = AUDIO_VOICE_STATE_COMPLETED;
        s_audio.voice_status.result = result;
        s_audio.voice_status.last_error = ESP_OK;
    }
    s_audio.stop_requested = false;
    s_audio.cancel_requested = false;
    s_audio.alert_cancelled_session = false;
    bump_voice_revision_locked();
    if (snapshot != NULL) {
        snapshot->status = s_audio.voice_status;
        snapshot->reliability = s_audio.voice_reliability;
    }
    unlock_context();
    return accepted;
}

static void run_voice_session(void)
{
    bool cpu_locked = false;
    bool cpu_lock_released = false;
    bool microphone_open = false;
    bool cancelled = false;
    int16_t *workspace = NULL;
    size_t workspace_bytes = 0U;
    esp_err_t error = ESP_OK;
    audio_voice_result_t result = AUDIO_VOICE_RESULT_NOT_UNDERSTOOD;
    int command_id = VOICE_COMMAND_ID_NONE;
    float confidence = 0.0F;
    uint32_t elapsed_ms = 0U;
    audio_level_result_t levels = {0};
    audio_level_accumulator_t accumulator;
    audio_level_init(&accumulator);
    uint32_t generation = 0U;
    lock_context();
    generation = s_audio.voice_status.generation;
    unlock_context();
    const TickType_t wall_started = xTaskGetTickCount();
    voice_reliability_resources_t observed =
        capture_voice_resources();
    voice_reliability_sample_t reliability;
    voice_reliability_sample_init(
        &reliability, generation, &observed);

    if (s_audio.voice_cpu_lock == NULL) {
        error = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    error = esp_pm_lock_acquire(s_audio.voice_cpu_lock);
    if (error != ESP_OK) {
        goto cleanup;
    }
    cpu_locked = true;
    reliability.cpu_lock_acquired = true;

    const audio_control_t initial_control = take_control();
    if (initial_control == AUDIO_CONTROL_CANCEL) {
        cancelled = true;
        goto cleanup;
    }
    if (initial_control == AUDIO_CONTROL_STOP) {
        result = AUDIO_VOICE_RESULT_NO_VOICE;
        goto cleanup;
    }

    error = prepare_voice_engine();
    if (error != ESP_OK) {
        goto cleanup;
    }
    const audio_control_t prepared_control = take_control();
    if (prepared_control == AUDIO_CONTROL_CANCEL) {
        cancelled = true;
        goto cleanup;
    }
    if (prepared_control == AUDIO_CONTROL_STOP) {
        result = AUDIO_VOICE_RESULT_NO_VOICE;
        goto cleanup;
    }
    const int chunk_frames =
        s_voice_multinet->get_samp_chunksize(s_voice_model_data);
    const int sample_rate =
        s_voice_multinet->get_samp_rate(s_voice_model_data);
    if (chunk_frames <= 0 || chunk_frames > 4096 ||
        sample_rate != AUDIO_VOICE_SAMPLE_RATE_HZ) {
        error = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    const size_t capture_samples =
        (size_t)chunk_frames * AUDIO_CAPTURE_CHANNEL_COUNT;
    const size_t mono_samples = (size_t)chunk_frames;
    workspace_bytes =
        (capture_samples + mono_samples) * sizeof(int16_t);
    workspace = heap_caps_malloc(
        workspace_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (workspace == NULL) {
        workspace = heap_caps_malloc(workspace_bytes, MALLOC_CAP_8BIT);
    }
    if (workspace == NULL) {
        error = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    int16_t *capture_buffer = workspace;
    int16_t *mono_buffer = workspace + capture_samples;
    observed = capture_voice_resources();
    voice_reliability_sample_observe(&reliability, &observed);

    error = open_microphones_at_rate(AUDIO_VOICE_SAMPLE_RATE_HZ);
    if (error != ESP_OK) {
        goto cleanup;
    }
    microphone_open = true;

    for (uint32_t settle = 0U;
         settle < AUDIO_VOICE_CAPTURE_SETTLE_CHUNKS; ++settle) {
        const audio_control_t control = take_control();
        if (control == AUDIO_CONTROL_CANCEL) {
            cancelled = true;
            goto cleanup;
        }
        if (control == AUDIO_CONTROL_STOP) {
            result = AUDIO_VOICE_RESULT_NO_VOICE;
            goto cleanup;
        }
        error = codec_error(esp_codec_dev_read(
            s_audio.microphone_device, capture_buffer,
            (int)(capture_samples * sizeof(capture_buffer[0]))));
        if (error != ESP_OK) {
            goto cleanup;
        }
    }

    set_voice_state(AUDIO_VOICE_STATE_LISTENING);
    uint32_t processed_frames = 0U;
    uint32_t next_status_ms = 500U;
    bool finished = false;
    while (!finished &&
           processed_frames <
               AUDIO_VOICE_SAMPLE_RATE_HZ *
                   AUDIO_VOICE_MAX_LISTENING_MS / 1000U) {
        const audio_control_t control = take_control();
        if (control == AUDIO_CONTROL_CANCEL) {
            cancelled = true;
            break;
        }
        if (control == AUDIO_CONTROL_STOP) {
            break;
        }

        error = codec_error(esp_codec_dev_read(
            s_audio.microphone_device, capture_buffer,
            (int)(capture_samples * sizeof(capture_buffer[0]))));
        if (error != ESP_OK) {
            break;
        }
        if (!audio_level_add_tdm16(
                &accumulator, capture_buffer, (size_t)chunk_frames,
                AUDIO_CAPTURE_CHANNEL_COUNT,
                AUDIO_CAPTURE_MICROPHONE_1_SLOT,
                AUDIO_CAPTURE_MICROPHONE_2_SLOT)) {
            error = ESP_ERR_INVALID_STATE;
            break;
        }
        if (!audio_tdm_extract_mono16(
                capture_buffer, (size_t)chunk_frames,
                AUDIO_CAPTURE_CHANNEL_COUNT,
                AUDIO_CAPTURE_MICROPHONE_1_SLOT, mono_buffer)) {
            error = ESP_ERR_INVALID_STATE;
            break;
        }

        const esp_mn_state_t detection = s_voice_multinet->detect(
            s_voice_model_data, mono_buffer);
        processed_frames += (uint32_t)chunk_frames;
        elapsed_ms = processed_frames * 1000U /
                     AUDIO_VOICE_SAMPLE_RATE_HZ;
        if (elapsed_ms >= next_status_ms) {
            update_voice_elapsed(elapsed_ms);
            observed = capture_voice_resources();
            voice_reliability_sample_observe(
                &reliability, &observed);
            next_status_ms += 500U;
        }
        if (detection == ESP_MN_STATE_DETECTED) {
            const esp_mn_results_t *results =
                s_voice_multinet->get_results(s_voice_model_data);
            if (results != NULL && results->num > 0 &&
                results->command_id[0] >= VOICE_COMMAND_ID_HOME &&
                results->command_id[0] <= VOICE_COMMAND_ID_CANCEL) {
                command_id = results->command_id[0];
                confidence = results->prob[0];
                result = AUDIO_VOICE_RESULT_MATCHED;
            }
            finished = true;
        } else if (detection == ESP_MN_STATE_TIMEOUT) {
            finished = true;
        }
    }

cleanup:
    if (microphone_open) {
        const esp_err_t close_error =
            codec_error(esp_codec_dev_close(s_audio.microphone_device));
        if (error == ESP_OK && close_error != ESP_OK) {
            error = close_error;
        }
    }
    if (s_voice_model_data != NULL && s_voice_multinet != NULL) {
        s_voice_multinet->clean(s_voice_model_data);
    }
    if (accumulator.frame_count > 0U &&
        audio_level_finish(&accumulator, &levels)) {
        const bool active = levels.active[0] || levels.active[1];
        if (result != AUDIO_VOICE_RESULT_MATCHED) {
            result = active ? AUDIO_VOICE_RESULT_NOT_UNDERSTOOD
                            : AUDIO_VOICE_RESULT_NO_VOICE;
        }
    } else if (result != AUDIO_VOICE_RESULT_MATCHED) {
        result = AUDIO_VOICE_RESULT_NO_VOICE;
    }
    if (workspace != NULL) {
        release_audio_workspace(workspace, workspace_bytes);
    }
    if (cpu_locked) {
        const esp_err_t unlock_error =
            esp_pm_lock_release(s_audio.voice_cpu_lock);
        cpu_lock_released = unlock_error == ESP_OK;
        if (error == ESP_OK && unlock_error != ESP_OK) {
            error = unlock_error;
        }
    }

    observed = capture_voice_resources();
    voice_reliability_sample_observe(&reliability, &observed);
    reliability.end = observed;
    reliability.capture_ms = elapsed_ms;
    reliability.wall_ms = (uint32_t)(
        (xTaskGetTickCount() - wall_started) * portTICK_PERIOD_MS);
    reliability.command_id = command_id;
    reliability.confidence_permille = confidence_permille(confidence);
    reliability.cpu_lock_released = cpu_lock_released;
    reliability.final_error = (int32_t)error;

    if (cancelled) {
        reliability.outcome = VOICE_RELIABILITY_OUTCOME_CANCELLED;
        reliability.command_id = VOICE_COMMAND_ID_NONE;
        reliability.confidence_permille = 0U;
    } else if (error != ESP_OK) {
        reliability.outcome = VOICE_RELIABILITY_OUTCOME_FAILED;
        reliability.command_id = VOICE_COMMAND_ID_NONE;
        reliability.confidence_permille = 0U;
    } else {
        reliability.outcome = voice_reliability_outcome(result);
    }

    const bool speech_detected =
        result == AUDIO_VOICE_RESULT_MATCHED ||
        result == AUDIO_VOICE_RESULT_NOT_UNDERSTOOD;
    audio_voice_snapshot_t committed = {0};
    if (!commit_voice_session(
            &reliability, cancelled, error, result, speech_detected,
            command_id, confidence, elapsed_ms, &committed)) {
        ESP_LOGW(TAG, "voice reliability sample was rejected");
    }
    if (cancelled) {
        ESP_LOGI(TAG,
                 "voice session cancelled; temporary audio buffer cleared");
    } else if (error != ESP_OK) {
        ESP_LOGW(TAG, "voice session failed: %s",
                 esp_err_to_name(error));
    } else {
        ESP_LOGI(TAG,
                 "voice session: generation=%u result=%s command=%d confidence=%.3f duration=%ums",
                 (unsigned)committed.status.generation,
                 audio_voice_result_name(result), command_id,
                 (double)confidence, (unsigned)elapsed_ms);
    }
}

static void finish_conversation(bool cancelled, esp_err_t error,
                                const conversation_client_status_t *remote)
{
    uint8_t final_turn_number = 0U;
    uint32_t final_session_elapsed_ms = 0U;
    lock_context();
    s_audio.conversation_status.running = false;
    s_audio.conversation_status.state =
        cancelled ? AUDIO_CONVERSATION_STATE_CANCELLED
                  : (error == ESP_OK
                         ? AUDIO_CONVERSATION_STATE_COMPLETED
                         : AUDIO_CONVERSATION_STATE_FAILED);
    s_audio.conversation_status.last_error =
        cancelled ? ESP_OK : error;
    if (cancelled) {
        s_audio.conversation_status.service_error_code = 0;
        secure_wipe(s_audio.conversation_status.service_error_name,
                    sizeof(s_audio.conversation_status.service_error_name));
        secure_wipe(s_audio.conversation_status.transcript,
                    sizeof(s_audio.conversation_status.transcript));
        secure_wipe(s_audio.conversation_status.response,
                    sizeof(s_audio.conversation_status.response));
    } else if (remote != NULL) {
        s_audio.conversation_status.service_error_code =
            remote->service_error_code;
        secure_wipe(s_audio.conversation_status.service_error_name,
                    sizeof(s_audio.conversation_status.service_error_name));
        snprintf(s_audio.conversation_status.service_error_name,
                 sizeof(s_audio.conversation_status.service_error_name),
                 "%s", remote->service_error_name);
        secure_wipe(s_audio.conversation_status.transcript,
                    sizeof(s_audio.conversation_status.transcript));
        snprintf(s_audio.conversation_status.transcript,
                 sizeof(s_audio.conversation_status.transcript), "%s",
                 remote->transcript);
        secure_wipe(s_audio.conversation_status.response,
                    sizeof(s_audio.conversation_status.response));
        snprintf(s_audio.conversation_status.response,
                 sizeof(s_audio.conversation_status.response), "%s",
                 remote->response);
    }
    s_audio.stop_requested = false;
    s_audio.cancel_requested = false;
    s_audio.conversation_release_requested = false;
    audio_conversation_control_reset(&s_audio.conversation_control);
    s_audio.alert_cancelled_session = false;
    final_turn_number = s_audio.conversation_status.turn_number;
    final_session_elapsed_ms =
        s_audio.conversation_status.session_elapsed_ms;
    bump_conversation_revision_locked();
    unlock_context();

    if (cancelled) {
        ESP_LOGI(TAG,
                 "cloud conversation cancelled; transient audio and text cleared");
    } else if (error != ESP_OK) {
        ESP_LOGW(TAG, "cloud conversation failed: %s",
                 esp_err_to_name(error));
    } else {
        ESP_LOGI(TAG,
                 "AI conversation completed after %u turn(s), %ums",
                 (unsigned)final_turn_number,
                 (unsigned)final_session_elapsed_ms);
    }
}

static uint32_t conversation_elapsed_since(TickType_t started)
{
    const uint64_t elapsed =
        (uint64_t)(xTaskGetTickCount() - started) *
        portTICK_PERIOD_MS;
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

static void note_conversation_session_elapsed(
    audio_conversation_flow_t *flow, TickType_t started)
{
    audio_conversation_flow_note_session_elapsed(
        flow, conversation_elapsed_since(started));
}

static esp_err_t wait_for_conversation_ready(
    conversation_client_t *client, bool *cancelled,
    conversation_client_status_t *remote,
    audio_conversation_flow_t *flow, TickType_t session_started,
    bool *end_session)
{
    const TickType_t started = xTaskGetTickCount();
    bool start_sent = false;
    uint32_t previous_revision = UINT32_MAX;
    while (true) {
        if (take_conversation_cancel()) {
            *cancelled = true;
            return ESP_OK;
        }
        note_conversation_session_elapsed(flow, session_started);
        if (audio_conversation_flow_session_expired(flow)) {
            mark_conversation_session_ending(end_session);
            return ESP_OK;
        }
        conversation_client_get_status(client, remote);
        if (remote->revision != previous_revision) {
            update_conversation_remote_status(remote);
            previous_revision = remote->revision;
        }
        if (remote->phase == CONVERSATION_CLIENT_PHASE_FAILED) {
            return remote->last_error != ESP_OK ? remote->last_error
                                                : ESP_FAIL;
        }
        if (remote->connected && !start_sent) {
            const esp_err_t error =
                conversation_client_send_start(client);
            if (error != ESP_OK) {
                return error;
            }
            start_sent = true;
        }
        if (remote->phase == CONVERSATION_CLIENT_PHASE_LISTENING) {
            return remote->turn_index == flow->turn_number
                       ? ESP_OK
                       : ESP_ERR_INVALID_STATE;
        }
        const uint32_t elapsed_ms = (uint32_t)(
            xTaskGetTickCount() - started) * portTICK_PERIOD_MS;
        if (elapsed_ms >= AUDIO_CONVERSATION_PROTOCOL_TIMEOUT_MS) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(AUDIO_CONVERSATION_POLL_MS));
    }
}

static esp_err_t wait_for_conversation_release(
    conversation_client_t *client, bool *cancelled,
    conversation_client_status_t *remote,
    audio_conversation_flow_t *flow, TickType_t session_started,
    bool *end_session)
{
    set_conversation_state(AUDIO_CONVERSATION_STATE_WAITING_FOR_RELEASE);
    const TickType_t started = xTaskGetTickCount();
    while (true) {
        if (take_conversation_cancel()) {
            *cancelled = true;
            return ESP_OK;
        }
        if (conversation_release_requested()) {
            return ESP_OK;
        }
        note_conversation_session_elapsed(flow, session_started);
        if (audio_conversation_flow_session_expired(flow)) {
            mark_conversation_session_ending(end_session);
            return ESP_OK;
        }
        conversation_client_get_status(client, remote);
        if (remote->phase == CONVERSATION_CLIENT_PHASE_FAILED) {
            update_conversation_remote_status(remote);
            return remote->last_error != ESP_OK ? remote->last_error
                                                : ESP_FAIL;
        }
        const uint32_t elapsed_ms = (uint32_t)(
            xTaskGetTickCount() - started) * portTICK_PERIOD_MS;
        if (elapsed_ms >= AUDIO_CONVERSATION_RELEASE_TIMEOUT_MS) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(AUDIO_CONVERSATION_POLL_MS));
    }
}

static esp_err_t capture_conversation_audio(
    conversation_client_t *client, int16_t *capture_buffer,
    int16_t *mono_buffer, bool *cancelled, uint32_t *elapsed_ms,
    conversation_client_status_t *remote,
    audio_conversation_flow_t *flow, TickType_t session_started,
    bool *end_session)
{
    if (flow == NULL || end_session == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *elapsed_ms = 0U;
    if (take_conversation_cancel()) {
        *cancelled = true;
        return ESP_OK;
    }
    note_conversation_session_elapsed(flow, session_started);
    update_conversation_elapsed(0U, flow->session_elapsed_ms);
    esp_err_t error = open_microphones_at_rate(
        AUDIO_CONVERSATION_CAPTURE_SAMPLE_RATE_HZ);
    if (error != ESP_OK) {
        return error;
    }
    bool microphone_open = true;
    bool speech_command_sent = false;
    for (uint32_t settle = 0U;
         settle < AUDIO_CONVERSATION_CAPTURE_SETTLE_CHUNKS; ++settle) {
        if (take_conversation_cancel()) {
            *cancelled = true;
            goto cleanup;
        }
        note_conversation_session_elapsed(flow, session_started);
        error = codec_error(esp_codec_dev_read(
            s_audio.microphone_device, capture_buffer,
            (int)(AUDIO_CONVERSATION_CAPTURE_CHUNK_FRAMES *
                  AUDIO_CAPTURE_CHANNEL_COUNT * sizeof(capture_buffer[0]))));
        if (error != ESP_OK) {
            goto cleanup;
        }
    }

    error = conversation_client_send_speech(client);
    if (error != ESP_OK) {
        goto cleanup;
    }
    speech_command_sent = true;
    set_conversation_state(AUDIO_CONVERSATION_STATE_LISTENING);
    uint32_t processed_frames = 0U;
    uint32_t next_status_ms = 500U;
    uint32_t previous_remote_revision = UINT32_MAX;
    const TickType_t capture_started = xTaskGetTickCount();
    while (processed_frames <
           AUDIO_CONVERSATION_CAPTURE_SAMPLE_RATE_HZ *
               AUDIO_CONVERSATION_MAX_LISTENING_MS / 1000U &&
           (uint32_t)(xTaskGetTickCount() - capture_started) *
                   portTICK_PERIOD_MS <
               AUDIO_CONVERSATION_MAX_LISTENING_MS) {
        const audio_control_t control = take_control();
        if (control == AUDIO_CONTROL_CANCEL) {
            *cancelled = true;
            break;
        }
        note_conversation_session_elapsed(flow, session_started);
        const bool stop_after_chunk =
            control == AUDIO_CONTROL_STOP && processed_frames == 0U;
        if (control == AUDIO_CONTROL_STOP && !stop_after_chunk) {
            break;
        }
        conversation_client_get_status(client, remote);
        if (remote->revision != previous_remote_revision) {
            update_conversation_remote_status(remote);
            previous_remote_revision = remote->revision;
        }
        if (remote->phase == CONVERSATION_CLIENT_PHASE_FAILED) {
            error = remote->last_error != ESP_OK ? remote->last_error
                                                 : ESP_FAIL;
            break;
        }
        if (remote->speech_ended) {
            break;
        }
        error = codec_error(esp_codec_dev_read(
            s_audio.microphone_device, capture_buffer,
            (int)(AUDIO_CONVERSATION_CAPTURE_CHUNK_FRAMES *
                  AUDIO_CAPTURE_CHANNEL_COUNT * sizeof(capture_buffer[0]))));
        if (error != ESP_OK ||
            !audio_tdm_extract_mono16(
                capture_buffer,
                AUDIO_CONVERSATION_CAPTURE_CHUNK_FRAMES,
                AUDIO_CAPTURE_CHANNEL_COUNT,
                AUDIO_CAPTURE_MICROPHONE_1_SLOT, mono_buffer)) {
            if (error == ESP_OK) {
                error = ESP_ERR_INVALID_STATE;
            }
            break;
        }
        conversation_client_get_status(client, remote);
        if (remote->revision != previous_remote_revision) {
            update_conversation_remote_status(remote);
            previous_remote_revision = remote->revision;
        }
        if (remote->phase == CONVERSATION_CLIENT_PHASE_FAILED) {
            error = remote->last_error != ESP_OK ? remote->last_error
                                                 : ESP_FAIL;
            break;
        }
        if (remote->speech_ended) {
            break;
        }
        error = conversation_client_send_pcm(
            client, mono_buffer,
            AUDIO_CONVERSATION_CAPTURE_CHUNK_FRAMES);
        if (error != ESP_OK) {
            break;
        }
        processed_frames += AUDIO_CONVERSATION_CAPTURE_CHUNK_FRAMES;
        const uint32_t wall_elapsed_ms =
            (uint32_t)(xTaskGetTickCount() - capture_started) *
            portTICK_PERIOD_MS;
        *elapsed_ms = wall_elapsed_ms <
                              AUDIO_CONVERSATION_MAX_LISTENING_MS
                          ? wall_elapsed_ms
                          : AUDIO_CONVERSATION_MAX_LISTENING_MS;
        if (*elapsed_ms >= next_status_ms) {
            update_conversation_elapsed(*elapsed_ms,
                                        flow->session_elapsed_ms);
            next_status_ms += 500U;
        }
        if (stop_after_chunk ||
            wall_elapsed_ms >=
                AUDIO_CONVERSATION_MAX_LISTENING_MS) {
            break;
        }
    }

cleanup:
    if (microphone_open) {
        const esp_err_t close_error =
            codec_error(esp_codec_dev_close(s_audio.microphone_device));
        if (error == ESP_OK && close_error != ESP_OK) {
            error = close_error;
        }
    }
    if (speech_command_sent) {
        if (*cancelled || *end_session || error != ESP_OK) {
            (void)conversation_client_cancel_speech(client);
        } else {
            error = conversation_client_stop_speech(client);
        }
    }
    return error;
}

static esp_err_t play_conversation_response(
    conversation_client_t *client, int16_t *playback_buffer,
    bool *cancelled, conversation_client_status_t *remote,
    audio_conversation_flow_t *flow, TickType_t session_started,
    bool *continue_session, bool *end_session)
{
    if (flow == NULL || continue_session == NULL ||
        end_session == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t volume = 0U;
    lock_context();
    volume = s_playback_volume;
    unlock_context();

    set_conversation_state(AUDIO_CONVERSATION_STATE_THINKING);
    conversation_client_get_status(client, remote);
    uint32_t previous_revision = remote->revision;
    uint32_t previous_received_audio_bytes =
        remote->received_audio_bytes;
    bool previous_response_ended = remote->response_ended;
    audio_response_watchdog_t response_watchdog;
    audio_response_watchdog_init(&response_watchdog);
    TickType_t previous_watchdog_tick = xTaskGetTickCount();
    bool speaker_open = false;
    bool local_started = false;
    bool response_cancel_requested = false;
    esp_err_t error = ESP_OK;
    while (true) {
        const TickType_t now = xTaskGetTickCount();
        const uint32_t watchdog_elapsed_ms =
            (uint32_t)(now - previous_watchdog_tick) *
            portTICK_PERIOD_MS;
        previous_watchdog_tick = now;
        const audio_control_t control = take_control();
        if (control == AUDIO_CONTROL_CANCEL) {
            *cancelled = true;
            break;
        }
        if (take_conversation_continue()) {
            /* Admission is decided at the instant the worker consumes KEY.
             * A late request must leave the current answer untouched; an
             * accepted request owns the next turn even if the clean response
             * boundary crosses the whole-session deadline. */
            note_conversation_session_elapsed(flow, session_started);
            if (!audio_conversation_flow_admit_next(flow)) {
                continue;
            }
            publish_conversation_advancing();
            *continue_session = true;
            const esp_err_t cancel_error =
                conversation_client_cancel_response(client);
            response_cancel_requested = cancel_error == ESP_OK;
            if (cancel_error != ESP_OK) {
                conversation_client_get_status(client, remote);
                if (!remote->response_ended) {
                    error = cancel_error;
                }
            }
            break;
        }
        conversation_client_get_status(client, remote);
        bool response_progress =
            remote->received_audio_bytes !=
                previous_received_audio_bytes ||
            (remote->response_ended && !previous_response_ended);
        previous_received_audio_bytes =
            remote->received_audio_bytes;
        previous_response_ended = remote->response_ended;
        if (remote->revision != previous_revision) {
            update_conversation_remote_status(remote);
            previous_revision = remote->revision;
        }
        if (remote->phase == CONVERSATION_CLIENT_PHASE_FAILED) {
            error = remote->last_error != ESP_OK ? remote->last_error
                                                 : ESP_FAIL;
            break;
        }

        const size_t samples = conversation_client_receive_pcm(
            client, playback_buffer,
            AUDIO_CONVERSATION_PLAYBACK_CHUNK_FRAMES,
            AUDIO_CONVERSATION_POLL_MS);
        if (samples > 0U) {
            if (!local_started) {
                if (volume > 0U) {
                    error = open_speaker_at_rate(
                        AUDIO_CONVERSATION_PLAYBACK_SAMPLE_RATE_HZ,
                        volume);
                    if (error != ESP_OK) {
                        break;
                    }
                    speaker_open = true;
                }
                error = conversation_client_local_response_started(client);
                if (error != ESP_OK) {
                    break;
                }
                local_started = true;
                set_conversation_state(AUDIO_CONVERSATION_STATE_SPEAKING);
            }
            if (speaker_open) {
                error = codec_error(esp_codec_dev_write(
                    s_audio.speaker_device, playback_buffer,
                    (int)(samples * sizeof(playback_buffer[0]))));
                if (error != ESP_OK) {
                    break;
                }
            } else {
                /* Muted playback still advances captions at the duration of
                 * the discarded PCM instead of draining the network queue at
                 * download speed. Chunks are small enough to keep controls
                 * responsive while preserving the text-only timing. */
                const uint32_t muted_playback_ms =
                    (uint32_t)((samples * 1000U +
                                AUDIO_CONVERSATION_PLAYBACK_SAMPLE_RATE_HZ -
                                1U) /
                               AUDIO_CONVERSATION_PLAYBACK_SAMPLE_RATE_HZ);
                vTaskDelay(pdMS_TO_TICKS(muted_playback_ms));
            }
            error = conversation_client_note_played_pcm(
                client, samples);
            if (error != ESP_OK) {
                break;
            }
            response_progress = true;
        }

        /* Only generated/drained audio or response.done counts as model
         * progress. ASR and other status revisions must neither shorten the
         * first-audio window nor keep a stalled response alive forever. A
         * progress sample observed at the timeout boundary wins this poll. */
        if (response_progress) {
            audio_response_watchdog_note_progress(
                &response_watchdog);
            previous_watchdog_tick = xTaskGetTickCount();
        } else if (audio_response_watchdog_tick(
                       &response_watchdog, watchdog_elapsed_ms)) {
            error = ESP_ERR_TIMEOUT;
            break;
        }

        if (remote->response_ended &&
            conversation_client_buffered_pcm_bytes(client) == 0U) {
            break;
        }
    }

    if (speaker_open) {
        const esp_err_t close_error = close_speaker();
        if (error == ESP_OK && close_error != ESP_OK) {
            error = close_error;
        }
    }
    if (local_started) {
        const esp_err_t local_error =
            conversation_client_local_response_ended(client);
        if (error == ESP_OK && local_error != ESP_OK) {
            error = local_error;
        }
    }
    if (*cancelled) {
        /* Capture cancellation clears an uncommitted input buffer; once a
         * response is in progress the matching Realtime operation is
         * response.cancel. Closing the socket remains the bounded fallback. */
        (void)conversation_client_cancel_speech(client);
    }
    if ((*continue_session || *end_session) &&
        !response_cancel_requested && error == ESP_OK) {
        conversation_client_get_status(client, remote);
        if (!remote->response_ended) {
            const esp_err_t cancel_error =
                conversation_client_cancel_response(client);
            if (cancel_error != ESP_OK && *continue_session) {
                error = cancel_error;
            }
        }
    }
    return error;
}

static esp_err_t wait_for_conversation_response_boundary(
    conversation_client_t *client, bool *cancelled,
    conversation_client_status_t *remote)
{
    const TickType_t started = xTaskGetTickCount();
    uint32_t previous_revision = UINT32_MAX;
    while (true) {
        if (take_conversation_cancel()) {
            *cancelled = true;
            return ESP_OK;
        }
        conversation_client_get_status(client, remote);
        if (remote->revision != previous_revision) {
            update_conversation_remote_status(remote);
            previous_revision = remote->revision;
        }
        if (remote->phase == CONVERSATION_CLIENT_PHASE_FAILED) {
            return remote->last_error != ESP_OK ? remote->last_error
                                                : ESP_FAIL;
        }
        if (remote->response_ended) {
            return ESP_OK;
        }
        if (conversation_elapsed_since(started) >=
            AUDIO_CONVERSATION_STOP_TIMEOUT_MS) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(AUDIO_CONVERSATION_POLL_MS));
    }
}

static esp_err_t begin_next_conversation_turn(
    conversation_client_t *client,
    conversation_client_status_t *remote,
    audio_conversation_flow_t *flow)
{
    if (!audio_conversation_flow_can_continue(flow)) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t next_turn = (uint8_t)(flow->turn_number + 1U);
    const esp_err_t error =
        conversation_client_begin_next_turn(client);
    if (error != ESP_OK) {
        return error;
    }
    if (audio_conversation_flow_continue(flow) !=
        AUDIO_CONVERSATION_FLOW_START_NEXT_TURN) {
        return ESP_ERR_INVALID_STATE;
    }
    conversation_client_get_status(client, remote);
    if (flow->turn_number != next_turn ||
        remote->turn_index != flow->turn_number) {
        return ESP_ERR_INVALID_STATE;
    }
    update_conversation_remote_status(remote);
    /* Publish the new input phase and reopen the continue gate atomically so
     * rapid repeated KEY events cannot leak into the following response. */
    lock_context();
    s_audio.conversation_status.session_elapsed_ms =
        flow->session_elapsed_ms;
    s_audio.conversation_status.turn_number = flow->turn_number;
    s_audio.conversation_status.max_turns =
        AUDIO_CONVERSATION_MAX_TURNS;
    s_audio.conversation_status.state =
        AUDIO_CONVERSATION_STATE_LISTENING;
    s_audio.conversation_status.running = true;
    audio_conversation_control_next_turn_started(
        &s_audio.conversation_control);
    bump_conversation_revision_locked();
    unlock_context();
    return ESP_OK;
}

static esp_err_t wait_for_conversation_follow_up(
    conversation_client_t *client, bool *cancelled,
    conversation_client_status_t *remote,
    audio_conversation_flow_t *flow, TickType_t session_started,
    bool *start_next_turn, bool *end_session)
{
    set_conversation_state(AUDIO_CONVERSATION_STATE_FOLLOW_UP);
    update_conversation_flow_status(flow);
    TickType_t previous = xTaskGetTickCount();
    uint32_t previous_remote_revision = UINT32_MAX;
    while (true) {
        if (take_conversation_cancel()) {
            *cancelled = true;
            return ESP_OK;
        }
        if (take_conversation_end()) {
            mark_conversation_session_ending(end_session);
            return ESP_OK;
        }
        const TickType_t now = xTaskGetTickCount();
        const uint32_t elapsed_ms = (uint32_t)(now - previous) *
                                    portTICK_PERIOD_MS;
        previous = now;
        const audio_conversation_flow_action_t action =
            audio_conversation_flow_tick(
                flow, elapsed_ms,
                conversation_elapsed_since(session_started));
        if (action == AUDIO_CONVERSATION_FLOW_END_SESSION) {
            update_conversation_flow_status(flow);
            mark_conversation_session_ending(end_session);
            return ESP_OK;
        }

        conversation_client_get_status(client, remote);
        if (remote->revision != previous_remote_revision) {
            update_conversation_remote_status(remote);
            previous_remote_revision = remote->revision;
        }
        if (remote->phase == CONVERSATION_CLIENT_PHASE_FAILED) {
            return remote->last_error != ESP_OK ? remote->last_error
                                                : ESP_FAIL;
        }
        if (take_conversation_continue()) {
            /* Close the polling TOCTOU window: a request accepted just before
             * the first tick still has to be valid at the actual turn
             * boundary. This enforces both the 30-second follow-up deadline
             * and the five-minute next-turn admission deadline. */
            const TickType_t accepted_at = xTaskGetTickCount();
            const uint32_t accepted_elapsed_ms =
                (uint32_t)(accepted_at - previous) * portTICK_PERIOD_MS;
            previous = accepted_at;
            if (audio_conversation_flow_tick(
                    flow, accepted_elapsed_ms,
                    conversation_elapsed_since(session_started)) ==
                AUDIO_CONVERSATION_FLOW_END_SESSION) {
                update_conversation_flow_status(flow);
                mark_conversation_session_ending(end_session);
                return ESP_OK;
            }
            publish_conversation_advancing();
            const esp_err_t error = begin_next_conversation_turn(
                client, remote, flow);
            if (error != ESP_OK) {
                return error;
            }
            *start_next_turn = true;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(AUDIO_CONVERSATION_POLL_MS));
    }
}

static void run_conversation_session(void)
{
    conversation_config_snapshot_t config = {0};
    conversation_client_t *client = NULL;
    conversation_client_status_t remote = {0};
    audio_conversation_flow_t flow;
    bool cpu_locked = false;
    bool network_acquired = false;
    bool cancelled = false;
    bool end_session = false;
    int16_t *workspace = NULL;
    const size_t capture_samples =
        (size_t)AUDIO_CONVERSATION_CAPTURE_CHUNK_FRAMES *
        AUDIO_CAPTURE_CHANNEL_COUNT;
    const size_t mono_samples =
        AUDIO_CONVERSATION_CAPTURE_CHUNK_FRAMES;
    const size_t playback_samples =
        AUDIO_CONVERSATION_PLAYBACK_CHUNK_FRAMES;
    const size_t workspace_samples =
        capture_samples + mono_samples + playback_samples;
    const size_t workspace_bytes =
        workspace_samples * sizeof(int16_t);
    esp_err_t error = ESP_OK;
    uint32_t elapsed_ms = 0U;
    const TickType_t session_started = xTaskGetTickCount();

    audio_conversation_flow_init(&flow);
    update_conversation_flow_status(&flow);
    set_conversation_state(AUDIO_CONVERSATION_STATE_CONNECTING);
    error = conversation_config_get_snapshot(&config);
    if (error != ESP_OK ||
        !config.config.enabled ||
        conversation_config_validate(&config.config) !=
            CONVERSATION_CONFIG_RESULT_OK) {
        if (error == ESP_OK) {
            error = ESP_ERR_INVALID_STATE;
        }
        goto cleanup;
    }
    if (s_audio.voice_cpu_lock == NULL) {
        error = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    error = esp_pm_lock_acquire(s_audio.voice_cpu_lock);
    if (error != ESP_OK) {
        goto cleanup;
    }
    cpu_locked = true;

    if (take_conversation_cancel()) {
        cancelled = true;
        goto cleanup;
    }
    error = network_time_begin_online_session(
        AUDIO_CONVERSATION_NETWORK_TIMEOUT_MS);
    if (error != ESP_OK) {
        goto cleanup;
    }
    network_acquired = true;
    if (take_conversation_cancel()) {
        cancelled = true;
        goto cleanup;
    }

    error = conversation_client_create(&config, &client);
    conversation_config_clear_sensitive(&config, sizeof(config));
    if (error != ESP_OK) {
        goto cleanup;
    }
    error = conversation_client_start_transport(client);
    if (error == ESP_OK) {
        error = wait_for_conversation_ready(
            client, &cancelled, &remote, &flow, session_started,
            &end_session);
    }
    if (error == ESP_OK && !cancelled && !end_session) {
        error = wait_for_conversation_release(
            client, &cancelled, &remote, &flow, session_started,
            &end_session);
    }
    if (error != ESP_OK || cancelled || end_session) {
        goto cleanup;
    }

    workspace = heap_caps_malloc(
        workspace_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (workspace == NULL) {
        workspace = heap_caps_malloc(workspace_bytes, MALLOC_CAP_8BIT);
    }
    if (workspace == NULL) {
        error = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    int16_t *capture_buffer = workspace;
    int16_t *mono_buffer = workspace + capture_samples;
    int16_t *playback_buffer = mono_buffer + mono_samples;

    while (error == ESP_OK && !cancelled && !end_session) {
        bool continue_session = false;
        error = capture_conversation_audio(
            client, capture_buffer, mono_buffer, &cancelled,
            &elapsed_ms, &remote, &flow, session_started,
            &end_session);
        if (error == ESP_OK && !cancelled && !end_session) {
            error = play_conversation_response(
                client, playback_buffer, &cancelled, &remote, &flow,
                session_started, &continue_session, &end_session);
        }
        note_conversation_session_elapsed(&flow, session_started);
        update_conversation_flow_status(&flow);
        if (error != ESP_OK || cancelled || end_session) {
            break;
        }

        const audio_conversation_flow_action_t turn_action =
            audio_conversation_flow_turn_completed(&flow);
        update_conversation_flow_status(&flow);
        if (turn_action == AUDIO_CONVERSATION_FLOW_END_SESSION) {
            mark_conversation_session_ending(&end_session);
            break;
        }

        if (continue_session) {
            error = wait_for_conversation_response_boundary(
                client, &cancelled, &remote);
            if (error == ESP_OK && !cancelled && !end_session) {
                error = begin_next_conversation_turn(
                    client, &remote, &flow);
            }
            continue;
        }

        bool start_next_turn = false;
        error = wait_for_conversation_follow_up(
            client, &cancelled, &remote, &flow, session_started,
            &start_next_turn, &end_session);
        if (error == ESP_OK && start_next_turn && !cancelled &&
            !end_session) {
            continue;
        }
        break;
    }
    note_conversation_session_elapsed(&flow, session_started);
    update_conversation_flow_status(&flow);
cleanup:
    /* Once a terminal decision has been made, reject button commands before
     * transport teardown. A command accepted during close could otherwise
     * report success even though no worker loop remains to consume it. */
    close_conversation_command_gate();
    if (client != NULL) {
        /* The Realtime protocol explicitly permits direct WebSocket
         * disconnection. Avoid a close-handshake wait on this single audio
         * worker so an alarm queued at the turn boundary can acquire the
         * speaker promptly. */
        (void)conversation_client_abort(client);
        conversation_client_get_status(client, &remote);
        if (!cancelled) {
            update_conversation_remote_status(&remote);
        }
        conversation_client_destroy(client);
    }
    if (workspace != NULL) {
        release_audio_workspace(workspace, workspace_bytes);
    }
    if (network_acquired) {
        const esp_err_t network_error =
            network_time_end_online_session();
        if (error == ESP_OK && !cancelled && network_error != ESP_OK) {
            error = network_error;
        }
    }
    if (cpu_locked) {
        const esp_err_t unlock_error =
            esp_pm_lock_release(s_audio.voice_cpu_lock);
        if (error == ESP_OK && !cancelled && unlock_error != ESP_OK) {
            error = unlock_error;
        }
    }
    conversation_config_clear_sensitive(&config, sizeof(config));
    finish_conversation(cancelled, error, &remote);
    secure_wipe(&remote, sizeof(remote));
}

static void run_audio_session(void)
{
    const size_t recording_samples =
        (size_t)AUDIO_MAX_RECORDING_FRAMES *
        AUDIO_RECORDING_CHANNEL_COUNT;
    const size_t capture_samples =
        AUDIO_CAPTURE_CHUNK_FRAMES * AUDIO_CAPTURE_CHANNEL_COUNT;
    const size_t playback_samples = AUDIO_TONE_CHUNK_FRAMES;
    const size_t workspace_bytes =
        (recording_samples + capture_samples + playback_samples) *
        sizeof(int16_t);
    int16_t *workspace = heap_caps_malloc(
        workspace_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (workspace == NULL) {
        finish_failed(ESP_ERR_NO_MEM);
        return;
    }
    int16_t *recording = workspace;
    int16_t *capture_buffer = workspace + recording_samples;
    int16_t *playback_buffer = capture_buffer + capture_samples;
    uint8_t playback_volume = 0U;
    lock_context();
    playback_volume = s_playback_volume;
    unlock_context();

    bool cancelled = false;
    bool tone_played = false;
    esp_err_t error = play_test_tone(playback_buffer, playback_volume,
                                     &tone_played, &cancelled);
    set_tone_played(tone_played);
    if (cancelled) {
        release_audio_workspace(workspace, workspace_bytes);
        finish_cancelled();
        return;
    }
    if (error != ESP_OK) {
        release_audio_workspace(workspace, workspace_bytes);
        finish_failed(error);
        return;
    }

    set_state(AUDIO_SESSION_STATE_PREPARING_RECORDING);
    if (!delay_with_cancel(AUDIO_PREPARE_RECORDING_MS)) {
        release_audio_workspace(workspace, workspace_bytes);
        finish_cancelled();
        return;
    }

    uint32_t recorded_frames = 0U;
    audio_level_result_t levels = {0};
    error = capture_voice(recording, capture_buffer, &recorded_frames,
                          &levels, &cancelled);
    if (cancelled) {
        release_audio_workspace(workspace, workspace_bytes);
        finish_cancelled();
        return;
    }
    if (error != ESP_OK) {
        release_audio_workspace(workspace, workspace_bytes);
        finish_failed(error);
        return;
    }

    set_state(AUDIO_SESSION_STATE_ANALYZING);
    const uint8_t playback_microphone =
        levels.rms[1] > levels.rms[0] ? 2U : 1U;
    const audio_diagnostics_result_t result =
        levels.active[0] && levels.active[1]
            ? AUDIO_DIAGNOSTICS_RESULT_INPUT_DETECTED
            : AUDIO_DIAGNOSTICS_RESULT_CHECK_INPUT;
    const uint32_t duration_ms =
        recorded_frames * 1000U / AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ;
    store_capture_result(duration_ms, &levels, playback_microphone, result);

    if (!levels.active[0] && !levels.active[1]) {
        release_audio_workspace(workspace, workspace_bytes);
        finish_completed(false, false);
        return;
    }
    if (playback_volume == 0U) {
        ESP_LOGI(TAG,
                 "diagnostic loopback skipped because playback volume is 0%%");
        release_audio_workspace(workspace, workspace_bytes);
        finish_completed(false, false);
        return;
    }
    if (!delay_with_cancel(AUDIO_CAPTURE_TO_PLAYBACK_MS)) {
        release_audio_workspace(workspace, workspace_bytes);
        finish_cancelled();
        return;
    }

    set_state(AUDIO_SESSION_STATE_PLAYBACK);
    bool playback_stopped = false;
    error = play_recording(recording, recorded_frames, playback_buffer,
                           (uint8_t)(playback_microphone - 1U), playback_volume,
                           &playback_stopped, &cancelled);
    release_audio_workspace(workspace, workspace_bytes);
    if (cancelled) {
        finish_cancelled();
    } else if (error != ESP_OK) {
        finish_failed(error);
    } else {
        finish_completed(!playback_stopped, playback_stopped);
    }
}

static void music_stop_locked(void)
{
    if (s_audio.music_requested || s_audio.music_running ||
        s_audio.music_status.state == AUDIO_MUSIC_PLAYING ||
        s_audio.music_status.state == AUDIO_MUSIC_PAUSED) {
        ++s_audio.music_generation;
        s_audio.music_requested = false;
        s_audio.music_status.state = AUDIO_MUSIC_STOPPED;
        s_audio.music_status.elapsed_seconds = 0;
        ++s_audio.music_status.revision;
    }
}

void audio_music_get_status(audio_music_status_t *status)
{
    if (status == NULL) return;
    lock_context();
    *status = s_audio.music_status;
    status->volume = s_playback_volume;
    status->busy = s_audio.music_requested || s_audio.music_running;
    unlock_context();
}

static bool music_can_start_locked(void)
{
    return !s_audio.music_storage_change && s_audio.status.initialized && s_audio.status.speaker_ready &&
           s_audio.worker_task != NULL && !s_audio.diagnostic_requested &&
           !audio_session_state_is_active(s_audio.status.state) &&
           !s_audio.voice_requested && !s_audio.voice_status.running &&
           !s_audio.conversation_requested && !s_audio.conversation_status.running &&
           !s_audio.alert_requested && !s_audio.alert_running;
}

esp_err_t audio_music_toggle(void)
{
    music_library_status_t library;
    music_library_get_status(&library);
    if (library.count == 0U) return ESP_ERR_NOT_FOUND;
    lock_context();
    if (!music_can_start_locked()) { unlock_context(); return ESP_ERR_INVALID_STATE; }
    if (s_audio.music_status.selected_index >= library.count) s_audio.music_status.selected_index = 0;
    if (s_audio.music_status.state == AUDIO_MUSIC_PLAYING) {
        s_audio.music_status.state = AUDIO_MUSIC_PAUSED;
    } else if (s_audio.music_status.state == AUDIO_MUSIC_PAUSED) {
        s_audio.music_status.state = AUDIO_MUSIC_PLAYING;
    } else {
        ++s_audio.music_generation;
        s_audio.music_requested = true;
        s_audio.music_status.state = AUDIO_MUSIC_PLAYING;
        s_audio.music_status.elapsed_seconds = 0;
        s_audio.music_status.error = ESP_OK;
    }
    ++s_audio.music_status.revision;
    unlock_context();
    xTaskNotifyGive(s_audio.worker_task);
    return ESP_OK;
}

esp_err_t audio_music_next(void)
{
    music_library_status_t library;
    music_library_get_status(&library);
    if (library.count == 0U) return ESP_ERR_NOT_FOUND;
    lock_context();
    if (!music_can_start_locked()) { unlock_context(); return ESP_ERR_INVALID_STATE; }
    const bool playing = s_audio.music_status.state == AUDIO_MUSIC_PLAYING;
    ++s_audio.music_generation;
    s_audio.music_requested = playing;
    s_audio.music_status.selected_index = (s_audio.music_status.selected_index + 1U) % library.count;
    s_audio.music_status.elapsed_seconds = 0;
    s_audio.music_status.error = ESP_OK;
    s_audio.music_status.state = playing ? AUDIO_MUSIC_PLAYING : AUDIO_MUSIC_STOPPED;
    ++s_audio.music_status.revision;
    unlock_context();
    xTaskNotifyGive(s_audio.worker_task);
    return ESP_OK;
}

void audio_music_stop(void)
{
    lock_context();
    music_stop_locked();
    unlock_context();
}

esp_err_t audio_music_stop_and_wait(uint32_t timeout_ms)
{
    audio_music_stop();
    const TickType_t start = xTaskGetTickCount();
    do {
        audio_music_status_t status;
        audio_music_get_status(&status);
        if (!status.busy) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (xTaskGetTickCount() - start < pdMS_TO_TICKS(timeout_ms));
    return ESP_ERR_TIMEOUT;
}

esp_err_t audio_music_begin_storage_change(uint32_t timeout_ms)
{
    lock_context();
    if (s_audio.music_storage_change) { unlock_context(); return ESP_ERR_INVALID_STATE; }
    s_audio.music_storage_change = true;
    unlock_context();
    const esp_err_t error = audio_music_stop_and_wait(timeout_ms);
    if (error != ESP_OK) audio_music_end_storage_change();
    return error;
}

void audio_music_end_storage_change(void)
{
    lock_context();
    s_audio.music_storage_change = false;
    unlock_context();
}

esp_err_t audio_music_select(const char *filename, bool play)
{
    size_t index = 0;
    if (!music_library_find(filename, &index) && play) return ESP_ERR_NOT_FOUND;
    lock_context();
    if ((play && !music_can_start_locked()) || (!play && s_audio.music_running)) {
        unlock_context(); return ESP_ERR_INVALID_STATE;
    }
    ++s_audio.music_generation;
    s_audio.music_requested = play;
    s_audio.music_status.selected_index = (uint8_t)index;
    s_audio.music_status.state = play ? AUDIO_MUSIC_PLAYING : AUDIO_MUSIC_STOPPED;
    s_audio.music_status.elapsed_seconds = 0;
    s_audio.music_status.error = ESP_OK;
    ++s_audio.music_status.revision;
    TaskHandle_t worker = s_audio.worker_task;
    unlock_context();
    if (play && worker != NULL) xTaskNotifyGive(worker);
    return ESP_OK;
}

typedef struct {
    uint32_t generation;
    bool opened;
    bool muted;
    bool cpu_locked;
    uint8_t volume;
} music_output_t;

static bool music_output_ready(void *context)
{
    music_output_t *output = context;
    while (true) {
        lock_context();
        const bool current = output->generation == s_audio.music_generation;
        const bool paused = s_audio.music_status.state == AUDIO_MUSIC_PAUSED;
        unlock_context();
        if (!current) return false;
        if (paused && output->cpu_locked) {
            (void)esp_pm_lock_release(s_audio.voice_cpu_lock);
            output->cpu_locked = false;
        } else if (!paused && !output->cpu_locked && s_audio.voice_cpu_lock != NULL) {
            output->cpu_locked = esp_pm_lock_acquire(s_audio.voice_cpu_lock) == ESP_OK;
        }
        if (output->opened && output->muted != paused) {
            (void)esp_codec_dev_set_out_mute(s_audio.speaker_device, paused);
            output->muted = paused;
        }
        if (!paused) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static esp_err_t music_output_write(void *context, uint32_t rate, const int16_t *mono, size_t frames)
{
    music_output_t *output = context;
    lock_context();
    const uint8_t volume = s_playback_volume;
    unlock_context();
    if (!output->opened) {
        const esp_err_t error = open_speaker_at_rate(rate, volume);
        if (error != ESP_OK) return error;
        output->opened = true;
        output->volume = volume;
    } else if (volume != output->volume) {
        const esp_err_t error = codec_error(esp_codec_dev_set_out_vol(s_audio.speaker_device, volume));
        if (error != ESP_OK) return error;
        output->volume = volume;
    }
    return codec_error(esp_codec_dev_write(s_audio.speaker_device, (void *)mono, (int)(frames * sizeof(*mono))));
}

static void music_output_progress(void *context, uint32_t seconds)
{
    const music_output_t *output = context;
    lock_context();
    if (output->generation == s_audio.music_generation && seconds != s_audio.music_status.elapsed_seconds) {
        s_audio.music_status.elapsed_seconds = seconds;
        ++s_audio.music_status.revision;
    }
    unlock_context();
}

static void run_music(uint32_t generation, size_t index)
{
    music_output_t output = {.generation = generation};
    music_track_t track;
    const music_stream_sink_t sink = {.context = &output, .ready = music_output_ready,
        .write = music_output_write, .progress = music_output_progress};
    esp_err_t error = music_library_track(index, &track)
        ? music_stream_run(&track, &sink) : ESP_ERR_NOT_FOUND;
    if (error == ESP_OK && output.opened) {
        /* Let the final DMA descriptors drain. Cancellation still takes
         * precedence; never add this tail delay to alarm/OTA handoffs. */
        for (unsigned tick = 0; tick < 8U; ++tick) {
            lock_context();
            const bool current = output.generation == s_audio.music_generation;
            unlock_context();
            if (!current) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (output.opened) {
        const esp_err_t close_error = close_speaker();
        if (error == ESP_OK) error = close_error;
    }
    if (output.cpu_locked) (void)esp_pm_lock_release(s_audio.voice_cpu_lock);
    music_library_status_t library;
    music_library_get_status(&library);
    lock_context();
    s_audio.music_running = false;
    if (output.generation == s_audio.music_generation) {
        s_audio.music_status.error = error;
        if (error == ESP_OK && s_audio.music_status.state == AUDIO_MUSIC_PLAYING && index + 1U < library.count) {
            ++s_audio.music_status.selected_index;
            s_audio.music_status.elapsed_seconds = 0;
            ++s_audio.music_generation;
            s_audio.music_requested = true;
        } else {
            s_audio.music_status.state = error == ESP_OK ? AUDIO_MUSIC_STOPPED : AUDIO_MUSIC_ERROR;
        }
        ++s_audio.music_status.revision;
    }
    unlock_context();
}

static void audio_worker_task(void *argument)
{
    (void)argument;
    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (true) {
            audio_work_t work = AUDIO_WORK_NONE;
            uint32_t music_generation = 0;
            size_t music_index = 0;
            lock_context();
            /* If an alert arrives with a session request, let the session
             * observe its cancellation first so codec/I2S ownership is
             * released before alert playback starts. */
            if (s_audio.diagnostic_requested) {
                s_audio.diagnostic_requested = false;
                work = AUDIO_WORK_DIAGNOSTIC;
            } else if (s_audio.voice_requested) {
                s_audio.voice_requested = false;
                work = AUDIO_WORK_VOICE;
            } else if (s_audio.conversation_requested) {
                s_audio.conversation_requested = false;
                work = AUDIO_WORK_CONVERSATION;
            } else if (s_audio.alert_requested) {
                s_audio.alert_requested = false;
                s_audio.alert_running = true;
                work = AUDIO_WORK_ALERT;
            } else if (s_audio.music_requested) {
                s_audio.music_requested = false;
                s_audio.music_running = true;
                music_generation = s_audio.music_generation;
                music_index = s_audio.music_status.selected_index;
                work = AUDIO_WORK_MUSIC;
            }
            unlock_context();

            if (work == AUDIO_WORK_DIAGNOSTIC) {
                run_audio_session();
            } else if (work == AUDIO_WORK_VOICE) {
                run_voice_session();
            } else if (work == AUDIO_WORK_CONVERSATION) {
                run_conversation_session();
            } else if (work == AUDIO_WORK_ALERT) {
                run_audio_alert();
            } else if (work == AUDIO_WORK_MUSIC) {
                run_music(music_generation, music_index);
            } else {
                break;
            }
        }
    }
}

esp_err_t audio_diagnostics_init(i2c_master_bus_handle_t i2c_bus)
{
    if (s_audio.status.initialized) {
        return s_audio.status.speaker_ready &&
                       s_audio.status.microphones_ready &&
                       s_audio.worker_task != NULL
                   ? ESP_OK
                   : s_audio.status.last_error;
    }
    if (i2c_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_audio, 0, sizeof(s_audio));
    s_audio.mutex = xSemaphoreCreateMutex();
    if (s_audio.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_audio.status.result = AUDIO_DIAGNOSTICS_RESULT_NOT_RUN;
    s_audio.status.state = AUDIO_SESSION_STATE_IDLE;
    s_audio.status.revision = 1U;
    s_audio.voice_status.initialized = true;
    s_audio.voice_status.state = AUDIO_VOICE_STATE_IDLE;
    s_audio.voice_status.result = AUDIO_VOICE_RESULT_NONE;
    s_audio.voice_status.revision = 1U;
    s_audio.conversation_status.initialized = true;
    s_audio.conversation_status.state = AUDIO_CONVERSATION_STATE_IDLE;
    s_audio.conversation_status.max_turns =
        AUDIO_CONVERSATION_MAX_TURNS;
    s_audio.conversation_status.revision = 1U;
    voice_reliability_summary_init(&s_audio.voice_reliability);
    s_voice_model_partition = find_voice_model_partition();
    s_audio.voice_status.model_ready =
        voice_model_header_is_valid(s_voice_model_partition);
    esp_err_t voice_lock_error = esp_pm_lock_create(
        ESP_PM_CPU_FREQ_MAX, 0, "rlcd_voice", &s_audio.voice_cpu_lock);
    if (voice_lock_error != ESP_OK) {
        s_audio.voice_status.initialized = false;
        s_audio.voice_status.last_error = voice_lock_error;
        s_audio.conversation_status.initialized = false;
        s_audio.conversation_status.last_error = voice_lock_error;
        ESP_LOGW(TAG, "voice CPU lock unavailable: %s",
                 esp_err_to_name(voice_lock_error));
    }
    const esp_err_t speaker_probe = i2c_master_probe(
        i2c_bus, BOARD_AUDIO_ES8311_I2C_ADDRESS, 100);
    const esp_err_t microphone_probe = i2c_master_probe(
        i2c_bus, BOARD_AUDIO_ES7210_I2C_ADDRESS, 100);
    ESP_LOGI(TAG, "codec probes: ES8311=%s ES7210=%s",
             esp_err_to_name(speaker_probe),
             esp_err_to_name(microphone_probe));

    esp_err_t error = initialize_i2s();
    if (error != ESP_OK) {
        s_audio.status.last_error = error;
        return error;
    }
    s_audio.status.initialized = true;

    if (speaker_probe == ESP_OK) {
        s_audio.status.speaker_ready = initialize_speaker(i2c_bus);
        if (!s_audio.status.speaker_ready) {
            remember_initialization_error(ESP_FAIL);
        }
    } else {
        remember_initialization_error(speaker_probe);
    }
    if (microphone_probe == ESP_OK) {
        s_audio.status.microphones_ready = initialize_microphones(i2c_bus);
        if (!s_audio.status.microphones_ready) {
            remember_initialization_error(ESP_FAIL);
        }
    } else {
        remember_initialization_error(microphone_probe);
    }

    if (!s_audio.status.speaker_ready ||
        !s_audio.status.microphones_ready) {
        ESP_LOGW(TAG,
                 "audio partially initialized: speaker=%d microphones=%d",
                 s_audio.status.speaker_ready,
                 s_audio.status.microphones_ready);
    }

    s_audio.voice_status.microphone_ready =
        s_audio.status.microphones_ready;
    s_audio.conversation_status.microphone_ready =
        s_audio.status.microphones_ready;
    s_audio.conversation_status.speaker_ready =
        s_audio.status.speaker_ready;
    if (!s_audio.voice_status.model_ready) {
        ESP_LOGW(TAG,
                 "offline voice model is missing or invalid; clock remains available");
    }

    if (s_audio.status.speaker_ready ||
        s_audio.status.microphones_ready) {
        const BaseType_t task_created = xTaskCreate(
            audio_worker_task, "audio_worker", AUDIO_WORKER_STACK_SIZE,
            NULL, AUDIO_WORKER_PRIORITY, &s_audio.worker_task);
        if (task_created != pdPASS) {
            s_audio.status.last_error = ESP_ERR_NO_MEM;
            s_audio.voice_status.initialized = false;
            s_audio.voice_status.last_error = ESP_ERR_NO_MEM;
            s_audio.conversation_status.initialized = false;
            s_audio.conversation_status.last_error = ESP_ERR_NO_MEM;
            bump_voice_revision_locked();
            bump_conversation_revision_locked();
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_audio.voice_status.initialized &&
        s_audio.voice_status.model_ready &&
        s_audio.voice_status.microphone_ready &&
        s_audio.worker_task != NULL) {
        s_audio.voice_status.engine_preparing = true;
        bump_voice_revision_locked();
        const BaseType_t task_created = xTaskCreate(
            voice_model_prepare_task, "voice_prepare",
            AUDIO_VOICE_PREPARE_STACK_SIZE, NULL,
            AUDIO_VOICE_PREPARE_PRIORITY,
            &s_audio.voice_prepare_task);
        if (task_created != pdPASS) {
            s_audio.voice_status.engine_preparing = false;
            s_audio.voice_status.last_error = ESP_ERR_NO_MEM;
            bump_voice_revision_locked();
            ESP_LOGW(TAG,
                     "offline voice model preparation task unavailable");
        }
    }

    if (s_audio.status.speaker_ready &&
        s_audio.status.microphones_ready &&
        s_audio.worker_task != NULL) {
        ESP_LOGI(TAG,
                 "audio ready: diagnostics=%u Hz voice=%u Hz model=%s",
                 AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ,
                 AUDIO_VOICE_SAMPLE_RATE_HZ,
                 s_audio.voice_status.model_ready ? "ready" : "missing");
        return ESP_OK;
    }
    return s_audio.status.last_error != ESP_OK
               ? s_audio.status.last_error
               : ESP_ERR_NOT_FOUND;
}

void audio_diagnostics_get_status(audio_diagnostics_status_t *status)
{
    if (status == NULL) {
        return;
    }
    lock_context();
    *status = s_audio.status;
    unlock_context();
}

esp_err_t audio_diagnostics_set_playback_volume(uint8_t volume_percent)
{
    if (volume_percent > 100U) {
        return ESP_ERR_INVALID_ARG;
    }

    lock_context();
    if (audio_session_state_is_active(s_audio.status.state) ||
        s_audio.voice_requested || s_audio.voice_status.running ||
        s_audio.conversation_requested ||
        s_audio.conversation_status.running ||
        s_audio.alert_requested || s_audio.alert_running) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    s_playback_volume = volume_percent;
    unlock_context();
    return ESP_OK;
}

esp_err_t audio_diagnostics_start(void)
{
    lock_context();
    if (!s_audio.status.initialized || !s_audio.status.speaker_ready ||
        !s_audio.status.microphones_ready || s_audio.worker_task == NULL ||
        audio_session_state_is_active(s_audio.status.state) ||
        s_audio.voice_requested || s_audio.voice_status.running ||
        s_audio.conversation_requested ||
        s_audio.conversation_status.running ||
        s_audio.alert_requested || s_audio.alert_running) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }

    s_audio.stop_requested = false;
    s_audio.cancel_requested = false;
    s_audio.alert_cancelled_session = false;
    music_stop_locked();
    s_audio.diagnostic_requested = true;
    s_audio.status.running = true;
    s_audio.status.test_completed = false;
    s_audio.status.tone_played = false;
    s_audio.status.microphone_capture_completed = false;
    s_audio.status.voice_played = false;
    s_audio.status.playback_stopped = false;
    s_audio.status.microphone_1_level_percent = 0U;
    s_audio.status.microphone_2_level_percent = 0U;
    s_audio.status.microphone_1_rms = 0U;
    s_audio.status.microphone_2_rms = 0U;
    s_audio.status.playback_microphone = 0U;
    s_audio.status.recording_elapsed_ms = 0U;
    s_audio.status.recording_duration_ms = 0U;
    s_audio.status.playback_elapsed_ms = 0U;
    s_audio.status.state = s_playback_volume == 0U
                               ? AUDIO_SESSION_STATE_PREPARING_RECORDING
                               : AUDIO_SESSION_STATE_PLAYING_TONE;
    s_audio.status.result = AUDIO_DIAGNOSTICS_RESULT_NOT_RUN;
    s_audio.status.last_error = ESP_OK;
    bump_revision_locked();
    unlock_context();

    xTaskNotifyGive(s_audio.worker_task);
    return ESP_OK;
}

esp_err_t audio_alert_start(void)
{
    lock_context();
    if (!s_audio.status.initialized || !s_audio.status.speaker_ready ||
        s_audio.worker_task == NULL || s_audio.alert_requested ||
        s_audio.alert_running) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }

    s_audio.alert_cancelled_session = false;
    if (audio_session_state_is_active(s_audio.status.state) ||
        s_audio.voice_requested || s_audio.voice_status.running ||
        s_audio.conversation_requested ||
        s_audio.conversation_status.running) {
        s_audio.alert_cancelled_session =
            !s_audio.cancel_requested;
        s_audio.cancel_requested = true;
    }
    music_stop_locked();
    s_audio.alert_requested = true;
    s_audio.alert_stop_requested = false;
    unlock_context();

    xTaskNotifyGive(s_audio.worker_task);
    return ESP_OK;
}

esp_err_t audio_alert_stop(void)
{
    lock_context();
    if (s_audio.alert_requested && !s_audio.alert_running) {
        s_audio.alert_requested = false;
        s_audio.alert_stop_requested = false;
        if (s_audio.alert_cancelled_session) {
            s_audio.cancel_requested = false;
            s_audio.alert_cancelled_session = false;
        }
        unlock_context();
        return ESP_OK;
    }
    if (!s_audio.alert_running) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio.alert_stop_requested = true;
    unlock_context();
    return ESP_OK;
}

bool audio_alert_is_active(void)
{
    lock_context();
    const bool active = s_audio.alert_requested || s_audio.alert_running;
    unlock_context();
    return active;
}

esp_err_t audio_diagnostics_request_stop(void)
{
    lock_context();
    if (s_audio.status.state != AUDIO_SESSION_STATE_RECORDING &&
        s_audio.status.state != AUDIO_SESSION_STATE_PLAYBACK) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio.stop_requested = true;
    unlock_context();
    return ESP_OK;
}

esp_err_t audio_diagnostics_cancel(void)
{
    lock_context();
    if (!audio_session_state_is_active(s_audio.status.state)) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio.cancel_requested = true;
    s_audio.alert_cancelled_session = false;
    s_audio.stop_requested = false;
    unlock_context();
    return ESP_OK;
}

void audio_voice_get_status(audio_voice_status_t *status)
{
    if (status == NULL) {
        return;
    }
    lock_context();
    *status = s_audio.voice_status;
    unlock_context();
}

void audio_voice_get_snapshot(audio_voice_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    lock_context();
    snapshot->status = s_audio.voice_status;
    snapshot->reliability = s_audio.voice_reliability;
    unlock_context();
}

esp_err_t audio_voice_reset_reliability(void)
{
    lock_context();
    if (s_audio.voice_requested || s_audio.voice_status.running) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    voice_reliability_summary_init(&s_audio.voice_reliability);
    unlock_context();
    ESP_LOGI(TAG, "volatile voice reliability counters reset");
    return ESP_OK;
}

esp_err_t audio_voice_start(uint32_t generation)
{
    if (generation == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    lock_context();
    if (!s_audio.voice_status.initialized ||
        !s_audio.voice_status.model_ready ||
        !s_audio.voice_status.engine_ready ||
        !s_audio.voice_status.microphone_ready ||
        s_audio.worker_task == NULL || s_audio.voice_requested ||
        s_audio.voice_status.running ||
        audio_session_state_is_active(s_audio.status.state) ||
        s_audio.diagnostic_requested || s_audio.conversation_requested ||
        s_audio.conversation_status.running || s_audio.alert_requested ||
        s_audio.alert_running) {
        const esp_err_t error =
            !s_audio.voice_status.model_ready ? ESP_ERR_NOT_FOUND
                                              : ESP_ERR_INVALID_STATE;
        unlock_context();
        return error;
    }

    s_audio.stop_requested = false;
    s_audio.cancel_requested = false;
    s_audio.alert_cancelled_session = false;
    music_stop_locked();
    s_audio.voice_requested = true;
    s_audio.voice_status.running = true;
    s_audio.voice_status.speech_detected = false;
    s_audio.voice_status.generation = generation;
    s_audio.voice_status.elapsed_ms = 0U;
    s_audio.voice_status.command_id = VOICE_COMMAND_ID_NONE;
    s_audio.voice_status.confidence = 0.0F;
    s_audio.voice_status.state = AUDIO_VOICE_STATE_PREPARING;
    s_audio.voice_status.result = AUDIO_VOICE_RESULT_NONE;
    s_audio.voice_status.last_error = ESP_OK;
    bump_voice_revision_locked();
    unlock_context();

    xTaskNotifyGive(s_audio.worker_task);
    return ESP_OK;
}

esp_err_t audio_voice_request_stop(void)
{
    lock_context();
    if (!s_audio.voice_requested &&
        s_audio.voice_status.state != AUDIO_VOICE_STATE_PREPARING &&
        s_audio.voice_status.state != AUDIO_VOICE_STATE_LISTENING) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio.stop_requested = true;
    unlock_context();
    return ESP_OK;
}

esp_err_t audio_voice_cancel(void)
{
    lock_context();
    if (!s_audio.voice_requested && !s_audio.voice_status.running) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio.cancel_requested = true;
    s_audio.alert_cancelled_session = false;
    s_audio.stop_requested = false;
    unlock_context();
    return ESP_OK;
}

void audio_conversation_get_status(audio_conversation_status_t *status)
{
    if (status == NULL) {
        return;
    }
    lock_context();
    *status = s_audio.conversation_status;
    unlock_context();
}

esp_err_t audio_conversation_start(uint32_t generation)
{
    if (generation == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    lock_context();
    if (!s_audio.conversation_status.initialized ||
        !s_audio.conversation_status.microphone_ready ||
        !s_audio.conversation_status.speaker_ready ||
        s_audio.worker_task == NULL || s_audio.conversation_requested ||
        s_audio.conversation_status.running ||
        audio_session_state_is_active(s_audio.status.state) ||
        s_audio.diagnostic_requested || s_audio.voice_requested ||
        s_audio.voice_status.running || s_audio.alert_requested ||
        s_audio.alert_running) {
        const esp_err_t error =
            !s_audio.conversation_status.microphone_ready ||
                    !s_audio.conversation_status.speaker_ready
                ? ESP_ERR_NOT_FOUND
                : ESP_ERR_INVALID_STATE;
        unlock_context();
        return error;
    }

    s_audio.stop_requested = false;
    s_audio.cancel_requested = false;
    s_audio.alert_cancelled_session = false;
    s_audio.conversation_release_requested = false;
    s_audio.conversation_accepting_commands = true;
    audio_conversation_control_reset(&s_audio.conversation_control);
    music_stop_locked();
    s_audio.conversation_requested = true;
    s_audio.conversation_status.running = true;
    s_audio.conversation_status.generation = generation;
    s_audio.conversation_status.elapsed_ms = 0U;
    s_audio.conversation_status.session_elapsed_ms = 0U;
    s_audio.conversation_status.turn_number = 1U;
    s_audio.conversation_status.max_turns =
        AUDIO_CONVERSATION_MAX_TURNS;
    s_audio.conversation_status.state =
        AUDIO_CONVERSATION_STATE_CONNECTING;
    s_audio.conversation_status.last_error = ESP_OK;
    s_audio.conversation_status.service_error_code = 0;
    secure_wipe(s_audio.conversation_status.service_error_name,
                sizeof(s_audio.conversation_status.service_error_name));
    secure_wipe(s_audio.conversation_status.transcript,
                sizeof(s_audio.conversation_status.transcript));
    secure_wipe(s_audio.conversation_status.response,
                sizeof(s_audio.conversation_status.response));
    bump_conversation_revision_locked();
    unlock_context();

    xTaskNotifyGive(s_audio.worker_task);
    return ESP_OK;
}

esp_err_t audio_conversation_release_key(void)
{
    lock_context();
    if (!s_audio.conversation_accepting_commands ||
        (!s_audio.conversation_requested &&
         !s_audio.conversation_status.running)) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_audio.conversation_status.state !=
            AUDIO_CONVERSATION_STATE_CONNECTING &&
        s_audio.conversation_status.state !=
            AUDIO_CONVERSATION_STATE_WAITING_FOR_RELEASE) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio.conversation_release_requested = true;
    unlock_context();
    return ESP_OK;
}

esp_err_t audio_conversation_request_stop(void)
{
    lock_context();
    if (!s_audio.conversation_accepting_commands ||
        s_audio.conversation_status.state !=
        AUDIO_CONVERSATION_STATE_LISTENING) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio.stop_requested = true;
    unlock_context();
    return ESP_OK;
}

esp_err_t audio_conversation_continue(void)
{
    lock_context();
    const audio_conversation_state_t state =
        s_audio.conversation_status.state;
    const bool active = s_audio.conversation_requested ||
                        s_audio.conversation_status.running;
    if (!s_audio.conversation_accepting_commands ||
        !active ||
        (state != AUDIO_CONVERSATION_STATE_FOLLOW_UP &&
         state != AUDIO_CONVERSATION_STATE_SPEAKING) ||
        s_audio.conversation_status.turn_number == 0U ||
        s_audio.conversation_status.turn_number >=
            s_audio.conversation_status.max_turns ||
        s_audio.cancel_requested ||
        !audio_conversation_control_request_continue(
            &s_audio.conversation_control)) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    unlock_context();
    return ESP_OK;
}

esp_err_t audio_conversation_end(void)
{
    lock_context();
    const bool active = s_audio.conversation_requested ||
                        s_audio.conversation_status.running;
    if (!s_audio.conversation_accepting_commands ||
        !active ||
        s_audio.conversation_status.state !=
            AUDIO_CONVERSATION_STATE_FOLLOW_UP ||
        s_audio.cancel_requested ||
        !audio_conversation_control_request_end(
            &s_audio.conversation_control)) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    unlock_context();
    return ESP_OK;
}

esp_err_t audio_conversation_cancel(void)
{
    lock_context();
    if (!s_audio.conversation_accepting_commands ||
        (!s_audio.conversation_requested &&
         !s_audio.conversation_status.running)) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio.cancel_requested = true;
    s_audio.alert_cancelled_session = false;
    s_audio.stop_requested = false;
    audio_conversation_control_reset(&s_audio.conversation_control);
    unlock_context();
    return ESP_OK;
}

esp_err_t audio_conversation_dismiss(void)
{
    lock_context();
    const audio_conversation_state_t state =
        s_audio.conversation_status.state;
    if (s_audio.conversation_requested ||
        s_audio.conversation_status.running ||
        (state != AUDIO_CONVERSATION_STATE_COMPLETED &&
         state != AUDIO_CONVERSATION_STATE_CANCELLED &&
         state != AUDIO_CONVERSATION_STATE_FAILED)) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio.conversation_status.state = AUDIO_CONVERSATION_STATE_IDLE;
    s_audio.conversation_status.elapsed_ms = 0U;
    s_audio.conversation_status.session_elapsed_ms = 0U;
    s_audio.conversation_status.turn_number = 0U;
    s_audio.conversation_status.max_turns =
        AUDIO_CONVERSATION_MAX_TURNS;
    s_audio.conversation_status.last_error = ESP_OK;
    s_audio.conversation_status.service_error_code = 0;
    secure_wipe(s_audio.conversation_status.service_error_name,
                sizeof(s_audio.conversation_status.service_error_name));
    secure_wipe(s_audio.conversation_status.transcript,
                sizeof(s_audio.conversation_status.transcript));
    secure_wipe(s_audio.conversation_status.response,
                sizeof(s_audio.conversation_status.response));
    bump_conversation_revision_locked();
    unlock_context();
    return ESP_OK;
}

const char *audio_conversation_state_name(
    audio_conversation_state_t state)
{
    switch (state) {
    case AUDIO_CONVERSATION_STATE_CONNECTING:
        return "connecting";
    case AUDIO_CONVERSATION_STATE_WAITING_FOR_RELEASE:
        return "waiting_for_release";
    case AUDIO_CONVERSATION_STATE_LISTENING:
        return "listening";
    case AUDIO_CONVERSATION_STATE_THINKING:
        return "thinking";
    case AUDIO_CONVERSATION_STATE_SPEAKING:
        return "speaking";
    case AUDIO_CONVERSATION_STATE_ADVANCING:
        return "advancing";
    case AUDIO_CONVERSATION_STATE_FOLLOW_UP:
        return "follow_up";
    case AUDIO_CONVERSATION_STATE_COMPLETED:
        return "completed";
    case AUDIO_CONVERSATION_STATE_CANCELLED:
        return "cancelled";
    case AUDIO_CONVERSATION_STATE_FAILED:
        return "failed";
    case AUDIO_CONVERSATION_STATE_IDLE:
    default:
        return "idle";
    }
}

const char *audio_voice_result_name(audio_voice_result_t result)
{
    switch (result) {
    case AUDIO_VOICE_RESULT_MATCHED:
        return "matched";
    case AUDIO_VOICE_RESULT_NO_VOICE:
        return "no_voice";
    case AUDIO_VOICE_RESULT_NOT_UNDERSTOOD:
        return "not_understood";
    case AUDIO_VOICE_RESULT_CANCELLED:
        return "cancelled";
    case AUDIO_VOICE_RESULT_FAILED:
        return "failed";
    case AUDIO_VOICE_RESULT_NONE:
    default:
        return "none";
    }
}

const char *audio_voice_state_name(audio_voice_state_t state)
{
    switch (state) {
    case AUDIO_VOICE_STATE_PREPARING:
        return "preparing";
    case AUDIO_VOICE_STATE_LISTENING:
        return "listening";
    case AUDIO_VOICE_STATE_COMPLETED:
        return "completed";
    case AUDIO_VOICE_STATE_CANCELLED:
        return "cancelled";
    case AUDIO_VOICE_STATE_FAILED:
        return "failed";
    case AUDIO_VOICE_STATE_IDLE:
    default:
        return "idle";
    }
}

const char *audio_diagnostics_result_name(audio_diagnostics_result_t result)
{
    switch (result) {
    case AUDIO_DIAGNOSTICS_RESULT_INPUT_DETECTED:
        return "INPUT DETECTED";
    case AUDIO_DIAGNOSTICS_RESULT_CHECK_INPUT:
        return "CHECK INPUT";
    case AUDIO_DIAGNOSTICS_RESULT_FAILED:
        return "FAILED";
    case AUDIO_DIAGNOSTICS_RESULT_NOT_RUN:
    default:
        return "NOT RUN";
    }
}
