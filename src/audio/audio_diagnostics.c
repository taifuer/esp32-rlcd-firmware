#include "audio_diagnostics.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "audio_level.h"
#include "board_pins.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "es7210_adc.h"
#include "es8311_codec.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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
    AUDIO_TEST_TONE_VOLUME = 58,
    /* The default esp_codec_dev curve is 0.5 dB per step. Keep the verified
     * prompt level, but give speech a conservative 5 dB intelligibility
     * boost while the digital limiter still caps its peak. */
    AUDIO_MICROPHONE_GAIN_DB = 30,
    AUDIO_PREPARE_RECORDING_MS = 300,
    AUDIO_CAPTURE_TO_PLAYBACK_MS = 150,
    AUDIO_PLAYBACK_FADE_MS = 10,
    AUDIO_PLAYBACK_TARGET_PEAK = 12000,
    AUDIO_PLAYBACK_GAIN_ONE = 4096,
    AUDIO_PLAYBACK_MAX_GAIN = AUDIO_PLAYBACK_GAIN_ONE * 4,
    AUDIO_WORKER_STACK_SIZE = 6144,
    AUDIO_WORKER_PRIORITY = 4,
};

static const char *TAG = "audio_diagnostics";
static uint8_t s_playback_volume =
    AUDIO_DIAGNOSTICS_DEFAULT_PLAYBACK_VOLUME;

typedef enum {
    AUDIO_CONTROL_NONE = 0,
    AUDIO_CONTROL_STOP,
    AUDIO_CONTROL_CANCEL,
} audio_control_t;

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
    bool stop_requested;
    bool cancel_requested;
    audio_diagnostics_status_t status;
} audio_diagnostics_context_t;

static audio_diagnostics_context_t s_audio;

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

static void release_audio_workspace(int16_t *workspace,
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

static esp_err_t open_speaker(int volume)
{
    if (s_audio.speaker_device == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = AUDIO_DIAGNOSTICS_BITS_PER_SAMPLE,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ,
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

static esp_err_t close_speaker(void)
{
    (void)esp_codec_dev_set_out_mute(s_audio.speaker_device, true);
    return codec_error(esp_codec_dev_close(s_audio.speaker_device));
}

static esp_err_t play_test_tone(int16_t *playback_buffer, bool *cancelled)
{
    *cancelled = false;
    esp_err_t error = open_speaker(AUDIO_TEST_TONE_VOLUME);
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
    return error != ESP_OK ? error : close_error;
}

static esp_err_t open_microphones(void)
{
    if (s_audio.microphone_device == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = AUDIO_DIAGNOSTICS_BITS_PER_SAMPLE,
        .channel = AUDIO_CAPTURE_CHANNEL_COUNT,
        .channel_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                        I2S_TDM_SLOT2 | I2S_TDM_SLOT3,
        .sample_rate = AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ,
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
                                uint8_t microphone, bool *stopped,
                                bool *cancelled)
{
    *stopped = false;
    *cancelled = false;
    int32_t mean = 0;
    uint32_t gain = AUDIO_PLAYBACK_GAIN_ONE;
    playback_parameters(recording, recorded_frames, microphone, &mean,
                        &gain);

    esp_err_t error = open_speaker(s_playback_volume);
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

    bool cancelled = false;
    esp_err_t error = play_test_tone(playback_buffer, &cancelled);
    set_tone_played(error == ESP_OK && !cancelled);
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
    if (!delay_with_cancel(AUDIO_CAPTURE_TO_PLAYBACK_MS)) {
        release_audio_workspace(workspace, workspace_bytes);
        finish_cancelled();
        return;
    }

    set_state(AUDIO_SESSION_STATE_PLAYBACK);
    bool playback_stopped = false;
    error = play_recording(recording, recorded_frames, playback_buffer,
                           (uint8_t)(playback_microphone - 1U),
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

static void audio_worker_task(void *argument)
{
    (void)argument;
    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run_audio_session();
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
        return s_audio.status.last_error != ESP_OK
                   ? s_audio.status.last_error
                   : ESP_ERR_NOT_FOUND;
    }

    const BaseType_t task_created = xTaskCreate(
        audio_worker_task, "audio_loopback", AUDIO_WORKER_STACK_SIZE, NULL,
        AUDIO_WORKER_PRIORITY, &s_audio.worker_task);
    if (task_created != pdPASS) {
        s_audio.status.last_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "ES8311 speaker and ES7210 dual microphones ready at %u Hz",
             AUDIO_DIAGNOSTICS_SAMPLE_RATE_HZ);
    return ESP_OK;
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
    if (audio_session_state_is_active(s_audio.status.state)) {
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
        audio_session_state_is_active(s_audio.status.state)) {
        unlock_context();
        return ESP_ERR_INVALID_STATE;
    }

    s_audio.stop_requested = false;
    s_audio.cancel_requested = false;
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
    s_audio.status.state = AUDIO_SESSION_STATE_PLAYING_TONE;
    s_audio.status.result = AUDIO_DIAGNOSTICS_RESULT_NOT_RUN;
    s_audio.status.last_error = ESP_OK;
    bump_revision_locked();
    unlock_context();

    xTaskNotifyGive(s_audio.worker_task);
    return ESP_OK;
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
    s_audio.stop_requested = false;
    unlock_context();
    return ESP_OK;
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
