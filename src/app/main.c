#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "app_storage.h"
#include "app_settings.h"
#include "alarm_history.h"
#include "alarm_input_gate.h"
#include "alarm_scheduler.h"
#include "audio_alert.h"
#include "audio_conversation.h"
#include "audio_diagnostics.h"
#include "audio_voice.h"
#include "battery.h"
#include "board_buttons.h"
#include "board_i2c.h"
#include "board_pins.h"
#include "button_state.h"
#include "chinese_lunar.h"
#include "conversation_config.h"
#include "display.h"
#include "driver/usb_serial_jtag.h"
#include "environment_comfort.h"
#include "environment_observation.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "firmware_update.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gallery_download.h"
#include "hold_interaction.h"
#include "image_delete_ui.h"
#include "network_time.h"
#include "network_screen_policy.h"
#include "online_firmware_update.h"
#include "page_state.h"
#include "pcf85063.h"
#include "power_manager.h"
#include "rtc_backup.h"
#include "sd_image.h"
#include "shtc3.h"
#include "settings_power_policy.h"
#include "usb_commands.h"
#include "voice_command_policy.h"
#include "voice_backend_policy.h"
#include "voice_session_state.h"

static const char *TAG = "rlcd_firmware";
typedef enum {
    APP_DISPLAY_NONE = 0,
    APP_DISPLAY_DASHBOARD,
    APP_DISPLAY_NETWORK_SETUP,
    APP_DISPLAY_CALENDAR,
    APP_DISPLAY_MONOCHROME_IMAGE,
    APP_DISPLAY_HOLD_PROMPT,
    APP_DISPLAY_IMAGE_DELETE_CONFIRMATION,
    APP_DISPLAY_IMAGE_DELETE_STATUS,
    APP_DISPLAY_STATUS,
    APP_DISPLAY_VOICE,
    APP_DISPLAY_ALARM,
    APP_DISPLAY_SETTINGS,
    APP_DISPLAY_ONLINE_UPDATE,
    APP_DISPLAY_MANUAL_SYNC,
    APP_DISPLAY_MANUAL_SYNC_RESULT,
    APP_DISPLAY_SETTINGS_POWER_RESULT,
    APP_DISPLAY_FIRMWARE_UPDATE_STARTING,
    APP_DISPLAY_FIRMWARE_UPDATE_READY,
    APP_DISPLAY_FIRMWARE_UPDATE_RECEIVING,
    APP_DISPLAY_FIRMWARE_UPDATE_VERIFYING,
    APP_DISPLAY_FIRMWARE_UPDATE_RESULT,
    APP_DISPLAY_GALLERY_DOWNLOAD,
} app_display_mode_t;

typedef enum {
    MANUAL_SYNC_UI_NONE = 0,
    MANUAL_SYNC_UI_ACTIVE,
    MANUAL_SYNC_UI_SUCCESS,
    MANUAL_SYNC_UI_FAILED,
    MANUAL_SYNC_UI_UNAVAILABLE,
} manual_sync_ui_t;

typedef enum {
    POWER_SETTING_UI_NONE = 0,
    POWER_SETTING_UI_APPLYING,
    POWER_SETTING_UI_SAVED,
    POWER_SETTING_UI_PENDING,
    POWER_SETTING_UI_SUPERSEDED,
    POWER_SETTING_UI_FAILED,
} power_setting_ui_t;

enum {
    APP_LOOP_INTERVAL_MS = 50,
    APP_PERIODIC_UPDATE_MS = 1000,
    APP_MANUAL_SYNC_RESULT_MS = 2000,
    APP_POWER_SETTING_RESULT_MS = 2000,
    APP_FIRMWARE_UPDATE_RESULT_MS = 2500,
    APP_GALLERY_RESULT_MS = 3000,
    APP_OTA_CONFIRM_DELAY_MS = 5000,
    APP_CLOUD_VOICE_UI_UPDATE_MS = 300,
};

static esp_err_t set_cpu_saving_policy(bool saving)
{
    const esp_err_t error = power_manager_set_saving(saving);
    /* The logical display/network policy remains useful on hardware where
     * ESP-IDF PM initialization was unavailable. */
    if (error == ESP_ERR_INVALID_STATE && !power_manager_is_ready()) {
        ESP_LOGW(TAG,
                 "CPU frequency policy unavailable; continuing with logical saving policy");
        return ESP_OK;
    }
    if (error != ESP_OK) {
        ESP_LOGW(TAG, "CPU frequency policy change failed: %s",
                 esp_err_to_name(error));
    }
    return error;
}

static esp_err_t apply_runtime_power_transition(
    const app_power_runtime_t *current_runtime,
    const app_power_policy_t *current_policy,
    const app_power_runtime_t *next_runtime,
    const app_power_policy_t *next_policy, bool network_ready)
{
    if (current_runtime == NULL || current_policy == NULL ||
        next_runtime == NULL || next_policy == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const bool current_saving =
        current_runtime->effective_state == APP_POWER_STATE_SAVING;
    const bool next_saving =
        next_runtime->effective_state == APP_POWER_STATE_SAVING;
    const bool network_changes =
        network_ready &&
        current_policy->automatic_network !=
            next_policy->automatic_network;

    /* Restore performance before enabling automatic network work. When
     * entering SAVING, stop scheduling new automatic work before releasing
     * the max-frequency lock. Existing user-started sessions keep running. */
    if (!next_saving) {
        esp_err_t error = set_cpu_saving_policy(false);
        if (error != ESP_OK) {
            return error;
        }
        if (network_changes) {
            error = network_time_set_automatic_sync_enabled(
                next_policy->automatic_network);
            if (error != ESP_OK) {
                (void)set_cpu_saving_policy(current_saving);
                return error;
            }
        }
        return ESP_OK;
    }

    if (network_changes) {
        const esp_err_t error =
            network_time_set_automatic_sync_enabled(
                next_policy->automatic_network);
        if (error != ESP_OK) {
            return error;
        }
    }
    const esp_err_t error = set_cpu_saving_policy(true);
    if (error != ESP_OK && network_changes) {
        (void)network_time_set_automatic_sync_enabled(
            current_policy->automatic_network);
    }
    return error;
}

static const char *page_name(app_page_t page)
{
    switch (page) {
    case APP_PAGE_CALENDAR:
        return "calendar";
    case APP_PAGE_IMAGE:
        return "image";
    case APP_PAGE_STATUS:
        return "status";
    case APP_PAGE_VOICE:
        return "voice";
    case APP_PAGE_SETTINGS:
        return "settings";
    case APP_PAGE_ONLINE_UPDATE:
        return "online update";
    case APP_PAGE_HOME:
    default:
        return "home";
    }
}

static const char *manual_sync_detail(network_time_state_t state)
{
    switch (state) {
    case NETWORK_TIME_STATE_STARTING:
        return "Starting Wi-Fi";
    case NETWORK_TIME_STATE_CONNECTING:
        return "Connecting...";
    case NETWORK_TIME_STATE_SYNCHRONIZING:
        return "Waiting for NTP";
    default:
        return "Request accepted";
    }
}

static const char *firmware_update_error_detail(esp_err_t error)
{
    switch (error) {
    case ESP_ERR_INVALID_STATE:
        return "Wait for network task";
    case ESP_ERR_NOT_SUPPORTED:
        return "OTA partitions unavailable";
    case ESP_ERR_INVALID_SIZE:
        return "Wrong firmware size";
    case ESP_ERR_OTA_VALIDATE_FAILED:
        return "Invalid OTA firmware";
    case ESP_ERR_TIMEOUT:
        return "Upload connection timed out";
    default:
        return esp_err_to_name(error);
    }
}

static const char *image_delete_error_detail(esp_err_t error)
{
    switch (error) {
    case ESP_ERR_INVALID_STATE:
        return "Storage busy; try again";
    case ESP_ERR_NOT_FOUND:
        return "Image changed; try again";
    case ESP_ERR_NO_MEM:
        return "Not enough memory";
    default:
        return "Image kept; check microSD";
    }
}

static bool image_delete_target_matches_status(
    const app_image_delete_target_t *target,
    const sd_image_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    const app_image_delete_target_t current = {
        .ready = status->state == SD_IMAGE_STATE_READY,
        .revision = status->revision,
        .image_count = status->image_count,
        .selected_index = status->selected_index,
        .filename = status->filename,
    };
    return app_image_delete_target_matches(target, &current);
}

static uint32_t alarm_schedule_revision(const app_settings_t *settings)
{
    if (settings == NULL) {
        return 0U;
    }
    return UINT32_C(0x01000000) |
           (settings->alarm_enabled ? UINT32_C(1) : UINT32_C(0)) |
           ((uint32_t)settings->alarm_hour << 1U) |
           ((uint32_t)settings->alarm_minute << 6U) |
           ((uint32_t)settings->alarm_weekdays << 12U);
}

static alarm_schedule_t alarm_schedule_from_settings(
    const app_settings_t *settings)
{
    return (alarm_schedule_t){
        .enabled = settings != NULL && settings->alarm_enabled,
        .hour = settings != NULL ? settings->alarm_hour : 0U,
        .minute = settings != NULL ? settings->alarm_minute : 0U,
        .repeat_weekdays =
            settings != NULL ? settings->alarm_weekdays : 0U,
        .revision = alarm_schedule_revision(settings),
    };
}

static alarm_clock_observation_t alarm_clock_from_dashboard(
    const display_dashboard_t *dashboard)
{
    alarm_clock_observation_t observation = {0};
    if (dashboard == NULL || !dashboard->time_valid) {
        return observation;
    }

    uint8_t calculated_weekday = 0U;
    if (pcf85063_calculate_weekday(
            dashboard->year, dashboard->month, dashboard->day,
            &calculated_weekday) != ESP_OK) {
        return observation;
    }

    observation.valid = true;
    observation.date_key = (uint32_t)dashboard->year * 10000U +
                           (uint32_t)dashboard->month * 100U +
                           dashboard->day;
    observation.weekday = calculated_weekday;
    observation.hour = dashboard->hour;
    observation.minute = dashboard->minute;
    return observation;
}

static alarm_clock_observation_t alarm_clock_from_rtc(
    const pcf85063_datetime_t *datetime)
{
    alarm_clock_observation_t observation = {0};
    if (datetime == NULL || !datetime->clock_integrity) {
        return observation;
    }

    uint8_t calculated_weekday = 0U;
    if (pcf85063_calculate_weekday(
            datetime->year, datetime->month, datetime->day,
            &calculated_weekday) != ESP_OK) {
        return observation;
    }

    observation.valid = true;
    observation.date_key = (uint32_t)datetime->year * 10000U +
                           (uint32_t)datetime->month * 100U +
                           datetime->day;
    observation.weekday = calculated_weekday;
    observation.hour = datetime->hour;
    observation.minute = datetime->minute;
    return observation;
}

static bool voice_engine_available(
    const audio_voice_status_t *status);

static display_voice_state_t display_voice_state(
    const voice_session_state_t *session,
    const audio_voice_status_t *engine, bool cloud_session_selected,
    bool cloud_mode, bool backend_available,
    const audio_conversation_status_t *conversation)
{
    if (cloud_session_selected && conversation != NULL) {
        switch (conversation->state) {
        case AUDIO_CONVERSATION_STATE_CONNECTING:
            return DISPLAY_VOICE_STATE_CLOUD_CONNECTING;
        case AUDIO_CONVERSATION_STATE_WAITING_FOR_RELEASE:
            return DISPLAY_VOICE_STATE_WAITING_FOR_RELEASE;
        case AUDIO_CONVERSATION_STATE_LISTENING:
            return DISPLAY_VOICE_STATE_LISTENING;
        case AUDIO_CONVERSATION_STATE_THINKING:
            return DISPLAY_VOICE_STATE_CLOUD_THINKING;
        case AUDIO_CONVERSATION_STATE_SPEAKING:
            return DISPLAY_VOICE_STATE_CLOUD_SPEAKING;
        case AUDIO_CONVERSATION_STATE_ADVANCING:
            return DISPLAY_VOICE_STATE_CLOUD_ADVANCING;
        case AUDIO_CONVERSATION_STATE_FOLLOW_UP:
            return DISPLAY_VOICE_STATE_CLOUD_FOLLOW_UP;
        case AUDIO_CONVERSATION_STATE_COMPLETED:
            return DISPLAY_VOICE_STATE_CLOUD_COMPLETED;
        case AUDIO_CONVERSATION_STATE_CANCELLED:
            return DISPLAY_VOICE_STATE_CANCELLED;
        case AUDIO_CONVERSATION_STATE_FAILED:
            return DISPLAY_VOICE_STATE_FAILED;
        case AUDIO_CONVERSATION_STATE_IDLE:
        default:
            break;
        }
    }

    /* An idle cloud page is independent of the offline MultiNet model. The
     * microphone/speaker readiness is reported separately by
     * engine_available, so READY here means that cloud is the selected mode
     * and no cloud turn is currently active. */
    if (cloud_mode &&
        voice_session_state_phase(session) == VOICE_SESSION_PHASE_READY) {
        return backend_available ? DISPLAY_VOICE_STATE_READY
                                 : DISPLAY_VOICE_STATE_UNAVAILABLE;
    }

    const voice_session_phase_t phase =
        voice_session_state_phase(session);
    switch (phase) {
    case VOICE_SESSION_PHASE_WAITING_FOR_RELEASE:
        return DISPLAY_VOICE_STATE_WAITING_FOR_RELEASE;
    case VOICE_SESSION_PHASE_LISTENING:
        return engine != NULL &&
                       engine->state == AUDIO_VOICE_STATE_PREPARING
                   ? DISPLAY_VOICE_STATE_PREPARING
                   : DISPLAY_VOICE_STATE_LISTENING;
    case VOICE_SESSION_PHASE_RECOGNIZING:
        return DISPLAY_VOICE_STATE_RECOGNIZING;
    case VOICE_SESSION_PHASE_SUCCEEDED:
        return DISPLAY_VOICE_STATE_SUCCEEDED;
    case VOICE_SESSION_PHASE_NO_VOICE:
        return DISPLAY_VOICE_STATE_NO_VOICE;
    case VOICE_SESSION_PHASE_NOT_UNDERSTOOD:
        return DISPLAY_VOICE_STATE_NOT_UNDERSTOOD;
    case VOICE_SESSION_PHASE_TARGET_UNAVAILABLE:
        return DISPLAY_VOICE_STATE_TARGET_UNAVAILABLE;
    case VOICE_SESSION_PHASE_ENGINE_UNAVAILABLE:
        return DISPLAY_VOICE_STATE_UNAVAILABLE;
    case VOICE_SESSION_PHASE_CANCELLED:
        return DISPLAY_VOICE_STATE_CANCELLED;
    case VOICE_SESSION_PHASE_FAILED:
        return DISPLAY_VOICE_STATE_FAILED;
    case VOICE_SESSION_PHASE_READY:
    default:
        if (!backend_available && conversation != NULL &&
            conversation->running) {
            return DISPLAY_VOICE_STATE_UNAVAILABLE;
        }
        if (engine != NULL && engine->engine_preparing) {
            return DISPLAY_VOICE_STATE_PREPARING;
        }
        return backend_available ? DISPLAY_VOICE_STATE_READY
                                 : DISPLAY_VOICE_STATE_UNAVAILABLE;
    }
}

static bool cloud_audio_ready(
    const audio_conversation_status_t *status)
{
    return status != NULL && status->initialized &&
           status->microphone_ready && status->speaker_ready;
}

static app_voice_backend_t select_voice_backend(
    const conversation_config_status_t *config,
    const audio_conversation_status_t *conversation,
    const audio_voice_status_t *offline,
    const network_time_status_t *network,
    bool normal_power)
{
    return app_voice_backend_choose(
        config != NULL && config->configured && config->enabled,
        normal_power,
        network != NULL && network->station_connected,
        cloud_audio_ready(conversation),
        conversation != NULL && conversation->running,
        voice_engine_available(offline));
}

static bool conversation_config_status_equal(
    const conversation_config_status_t *left,
    const conversation_config_status_t *right)
{
    return left != NULL && right != NULL &&
           left->service == right->service &&
           left->model == right->model &&
           left->enabled == right->enabled &&
           left->configured == right->configured &&
           left->shared_endpoint == right->shared_endpoint &&
           strcmp(left->api_host, right->api_host) == 0;
}

static bool conversation_state_is_terminal(
    audio_conversation_state_t state)
{
    return state == AUDIO_CONVERSATION_STATE_COMPLETED ||
           state == AUDIO_CONVERSATION_STATE_CANCELLED ||
           state == AUDIO_CONVERSATION_STATE_FAILED;
}

static esp_err_t cancel_voice_backend(bool cloud_session_selected)
{
    return cloud_session_selected ? audio_conversation_cancel()
                                  : audio_voice_cancel();
}

static esp_err_t stop_voice_backend(bool cloud_session_selected)
{
    return cloud_session_selected
               ? audio_conversation_request_stop()
               : audio_voice_request_stop();
}

static bool accept_cloud_conversation_status(
    voice_session_state_t *session,
    const audio_conversation_status_t *status,
    esp_err_t *session_error)
{
    if (session == NULL || status == NULL || session_error == NULL ||
        status->generation == 0U ||
        status->generation != voice_session_state_generation(session)) {
        return false;
    }

    const voice_session_phase_t phase =
        voice_session_state_phase(session);
    if ((status->state == AUDIO_CONVERSATION_STATE_THINKING ||
         status->state == AUDIO_CONVERSATION_STATE_SPEAKING ||
         status->state == AUDIO_CONVERSATION_STATE_ADVANCING ||
         status->state == AUDIO_CONVERSATION_STATE_FOLLOW_UP ||
         status->state == AUDIO_CONVERSATION_STATE_COMPLETED) &&
        phase == VOICE_SESSION_PHASE_LISTENING) {
        voice_session_state_note_speech(session);
        (void)voice_session_state_finish_listening(session);
    }

    if (status->state == AUDIO_CONVERSATION_STATE_CANCELLED) {
        *session_error = ESP_OK;
        return voice_session_state_cancel(session) !=
               VOICE_SESSION_ACTION_NONE;
    }
    if (status->state == AUDIO_CONVERSATION_STATE_FAILED) {
        *session_error = status->last_error != ESP_OK
                             ? status->last_error
                             : ESP_FAIL;
        return voice_session_state_report_failure(
            session, status->generation);
    }
    if (status->state == AUDIO_CONVERSATION_STATE_COMPLETED) {
        *session_error = ESP_OK;
        return voice_session_state_report_result(
            session, status->generation,
            VOICE_SESSION_RESULT_MATCHED);
    }
    return false;
}

static const char *cloud_voice_error_detail(
    const audio_conversation_status_t *status)
{
    if (status != NULL && status->service_error_name[0] != '\0') {
        return status->service_error_name;
    }
    if (status != NULL && status->last_error != ESP_OK) {
        return esp_err_to_name(status->last_error);
    }
    return "Cloud conversation stopped";
}

static bool voice_engine_available(
    const audio_voice_status_t *status)
{
    return status != NULL && status->initialized &&
           status->model_ready && status->engine_ready &&
           status->microphone_ready;
}

static const char *voice_unavailable_detail(
    const audio_voice_status_t *status)
{
    if (status == NULL || !status->initialized) {
        return "Audio not ready";
    }
    if (status->engine_preparing) {
        return "Starting offline commands";
    }
    if (!status->model_ready) {
        return "Offline command model missing";
    }
    if (!status->microphone_ready) {
        return "Microphones not ready";
    }
    if (!status->engine_ready) {
        return status->last_error == ESP_OK
                   ? "Offline commands not ready"
                   : esp_err_to_name(status->last_error);
    }
    return "Offline commands busy";
}

static const char *voice_command_detail(
    voice_command_action_t action)
{
    switch (action) {
    case VOICE_COMMAND_ACTION_OPEN_HOME:
        return "Opening home";
    case VOICE_COMMAND_ACTION_OPEN_CALENDAR:
        return "Opening calendar";
    case VOICE_COMMAND_ACTION_OPEN_STATUS:
        return "Opening status";
    case VOICE_COMMAND_ACTION_OPEN_IMAGE:
        return "Opening image";
    case VOICE_COMMAND_ACTION_OPEN_SETTINGS:
        return "Opening settings";
    case VOICE_COMMAND_ACTION_NONE:
    default:
        return "Command accepted";
    }
}

static app_page_t voice_command_page(
    voice_command_action_t action)
{
    switch (action) {
    case VOICE_COMMAND_ACTION_OPEN_CALENDAR:
        return APP_PAGE_CALENDAR;
    case VOICE_COMMAND_ACTION_OPEN_STATUS:
        return APP_PAGE_STATUS;
    case VOICE_COMMAND_ACTION_OPEN_IMAGE:
        return APP_PAGE_IMAGE;
    case VOICE_COMMAND_ACTION_OPEN_SETTINGS:
        return APP_PAGE_SETTINGS;
    case VOICE_COMMAND_ACTION_OPEN_HOME:
    case VOICE_COMMAND_ACTION_NONE:
    default:
        return APP_PAGE_HOME;
    }
}

static bool accept_voice_engine_result(
    voice_session_state_t *session,
    const audio_voice_status_t *status, bool image_available,
    voice_command_action_t *pending_action,
    esp_err_t *session_error)
{
    if (session == NULL || status == NULL ||
        pending_action == NULL || session_error == NULL ||
        status->generation == 0U ||
        status->generation != voice_session_state_generation(session) ||
        (status->state != AUDIO_VOICE_STATE_COMPLETED &&
         status->state != AUDIO_VOICE_STATE_CANCELLED &&
         status->state != AUDIO_VOICE_STATE_FAILED)) {
        return false;
    }

    *pending_action = VOICE_COMMAND_ACTION_NONE;
    *session_error = status->last_error;
    if (status->state == AUDIO_VOICE_STATE_CANCELLED ||
        status->result == AUDIO_VOICE_RESULT_CANCELLED) {
        (void)voice_session_state_cancel(session);
        return true;
    }

    if (status->result == AUDIO_VOICE_RESULT_NO_VOICE) {
        (void)voice_session_state_finish_listening(session);
        return true;
    }

    /* A failed capture and a recognized/unknown utterance both pass through
     * RECOGNIZING so the generation gate remains the sole result handoff. */
    voice_session_state_note_speech(session);
    (void)voice_session_state_finish_listening(session);
    if (status->state == AUDIO_VOICE_STATE_FAILED ||
        status->result == AUDIO_VOICE_RESULT_FAILED) {
        return voice_session_state_report_result(
            session, status->generation,
            VOICE_SESSION_RESULT_ERROR);
    }
    if (status->result == AUDIO_VOICE_RESULT_NOT_UNDERSTOOD) {
        return voice_session_state_report_result(
            session, status->generation,
            VOICE_SESSION_RESULT_NOT_UNDERSTOOD);
    }
    if (status->result != AUDIO_VOICE_RESULT_MATCHED) {
        return voice_session_state_report_result(
            session, status->generation,
            VOICE_SESSION_RESULT_ERROR);
    }

    const voice_command_decision_t decision =
        voice_command_policy_decide(status->command_id,
                                    image_available);
    switch (decision.kind) {
    case VOICE_COMMAND_DECISION_EXECUTE:
        *pending_action = decision.action;
        return voice_session_state_report_result(
            session, status->generation,
            VOICE_SESSION_RESULT_MATCHED);
    case VOICE_COMMAND_DECISION_UNAVAILABLE:
        return voice_session_state_report_result(
            session, status->generation,
            VOICE_SESSION_RESULT_TARGET_UNAVAILABLE);
    case VOICE_COMMAND_DECISION_CANCEL:
        (void)voice_session_state_cancel(session);
        return true;
    case VOICE_COMMAND_DECISION_REJECTED:
    default:
        return voice_session_state_report_result(
            session, status->generation,
            VOICE_SESSION_RESULT_NOT_UNDERSTOOD);
    }
}

static display_network_state_t dashboard_network_state(
    const network_time_status_t *status)
{
    return status != NULL && status->station_connected
               ? DISPLAY_NETWORK_CONNECTED
               : DISPLAY_NETWORK_HIDDEN;
}

static display_environment_comfort_t dashboard_environment_comfort(
    environment_comfort_level_t comfort)
{
    switch (comfort) {
    case ENVIRONMENT_COMFORT_COMFORTABLE:
        return DISPLAY_ENVIRONMENT_COMFORT_COMFORTABLE;
    case ENVIRONMENT_COMFORT_FAIR:
        return DISPLAY_ENVIRONMENT_COMFORT_FAIR;
    case ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT:
        return DISPLAY_ENVIRONMENT_COMFORT_NEEDS_ADJUSTMENT;
    case ENVIRONMENT_COMFORT_UNKNOWN:
    default:
        return DISPLAY_ENVIRONMENT_COMFORT_UNKNOWN;
    }
}

static bool environment_display_values_differ(
    float previous_temperature_c, float previous_humidity_percent,
    float next_temperature_c, float next_humidity_percent,
    bool fahrenheit)
{
    char previous[48];
    char next[48];
    const double previous_temperature =
        fahrenheit ? (double)previous_temperature_c * 9.0 / 5.0 + 32.0
                   : (double)previous_temperature_c;
    const double next_temperature =
        fahrenheit ? (double)next_temperature_c * 9.0 / 5.0 + 32.0
                   : (double)next_temperature_c;
    (void)snprintf(previous, sizeof(previous), "%.1f|%.0f",
                   previous_temperature,
                   (double)previous_humidity_percent);
    (void)snprintf(next, sizeof(next), "%.1f|%.0f", next_temperature,
                   (double)next_humidity_percent);
    return strcmp(previous, next) != 0;
}

static const char *device_wifi_state_name(
    bool service_ready, const network_time_status_t *status)
{
    const bool connecting =
        status != NULL &&
        (status->state == NETWORK_TIME_STATE_STARTING ||
         status->state == NETWORK_TIME_STATE_CONNECTING);
    const bool retrying =
        status != NULL &&
        status->state == NETWORK_TIME_STATE_RETRY_WAIT &&
        status->automatic_sync_enabled;
    const app_wifi_display_t display = app_network_wifi_display(
        service_ready, status != NULL && status->configured,
        status != NULL && status->station_connected,
        status != NULL && status->automatic_sync_enabled,
        connecting, retrying);
    switch (display) {
    case APP_WIFI_DISPLAY_NOT_READY:
        return "NOT READY";
    case APP_WIFI_DISPLAY_NOT_CONFIGURED:
        return "NOT CONFIGURED";
    case APP_WIFI_DISPLAY_CONNECTED:
        return "CONNECTED";
    case APP_WIFI_DISPLAY_CONNECTING:
        return "CONNECTING";
    case APP_WIFI_DISPLAY_OFFLINE_RETRY:
        return "OFFLINE | RETRY";
    case APP_WIFI_DISPLAY_OFF_SAVING:
        return "OFF | SAVING";
    case APP_WIFI_DISPLAY_OFFLINE:
    default:
        return "OFFLINE";
    }
}

static const char *device_time_sync_state_name(
    bool service_ready, bool last_sync_valid,
    const network_time_status_t *status)
{
    const app_time_sync_display_t display =
        app_network_time_sync_display(
            service_ready, last_sync_valid,
            status != NULL &&
                status->state == NETWORK_TIME_STATE_SYNCHRONIZING,
            status != NULL &&
                status->last_failure == NETWORK_TIME_FAILURE_NTP,
            status != NULL &&
                status->last_failure == NETWORK_TIME_FAILURE_SERVICE);
    switch (display) {
    case APP_TIME_SYNC_DISPLAY_NOT_READY:
        return "NOT READY";
    case APP_TIME_SYNC_DISPLAY_SYNCING:
        return "SYNCING";
    case APP_TIME_SYNC_DISPLAY_OK:
        return "OK";
    case APP_TIME_SYNC_DISPLAY_NTP_ERROR:
        return "NTP ERROR";
    case APP_TIME_SYNC_DISPLAY_SERVICE_ERROR:
        return "SERVICE ERROR";
    case APP_TIME_SYNC_DISPLAY_NOT_SYNCED:
    default:
        return "NOT SYNCED";
    }
}

static void configure_key_timing(
    button_state_t *state,
    app_page_t page,
    online_update_state_t online_update_state)
{
    uint32_t action_threshold_ms = app_page_key_hold_threshold_ms(page);
    if (page == APP_PAGE_ONLINE_UPDATE) {
        action_threshold_ms =
            app_page_online_update_hold_threshold_ms(
                online_update_state ==
                ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION);
    }
    (void)button_state_set_action_timing(state, action_threshold_ms);
}

static void configure_boot_timing(button_state_t *state, app_page_t page)
{
    const uint32_t action_threshold_ms =
        app_page_boot_hold_threshold_ms(page);
    (void)button_state_set_action_timing(state, action_threshold_ms);
}

static esp_err_t seed_system_time_from_rtc(
    const pcf85063_datetime_t *datetime)
{
    if (datetime == NULL || !datetime->clock_integrity) {
        return ESP_ERR_INVALID_ARG;
    }
    struct tm local = {
        .tm_sec = datetime->second,
        .tm_min = datetime->minute,
        .tm_hour = datetime->hour,
        .tm_mday = datetime->day,
        .tm_mon = (int)datetime->month - 1,
        .tm_year = (int)datetime->year - 1900,
        .tm_isdst = -1,
    };
    const time_t seconds = mktime(&local);
    if (seconds == (time_t)-1) {
        return ESP_ERR_INVALID_ARG;
    }
    const struct timeval value = {
        .tv_sec = seconds,
        .tv_usec = 0,
    };
    return settimeofday(&value, NULL) == 0 ? ESP_OK : ESP_FAIL;
}

static display_online_update_state_t display_online_update_state(
    online_update_state_t state)
{
    switch (state) {
    case ONLINE_UPDATE_STATE_CHECKING:
        return DISPLAY_ONLINE_UPDATE_STATE_CHECKING;
    case ONLINE_UPDATE_STATE_UP_TO_DATE:
        return DISPLAY_ONLINE_UPDATE_STATE_UP_TO_DATE;
    case ONLINE_UPDATE_STATE_AVAILABLE:
        return DISPLAY_ONLINE_UPDATE_STATE_UPDATE_AVAILABLE;
    case ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION:
        return DISPLAY_ONLINE_UPDATE_STATE_CONFIRM_INSTALL;
    case ONLINE_UPDATE_STATE_CONNECTING:
        return DISPLAY_ONLINE_UPDATE_STATE_CONNECTING;
    case ONLINE_UPDATE_STATE_DOWNLOADING:
        return DISPLAY_ONLINE_UPDATE_STATE_DOWNLOADING;
    case ONLINE_UPDATE_STATE_VERIFYING:
        return DISPLAY_ONLINE_UPDATE_STATE_VERIFYING;
    case ONLINE_UPDATE_STATE_SUCCESS:
        return DISPLAY_ONLINE_UPDATE_STATE_SUCCESS;
    case ONLINE_UPDATE_STATE_FAILED:
        return DISPLAY_ONLINE_UPDATE_STATE_FAILED;
    case ONLINE_UPDATE_STATE_IDLE:
    default:
        return DISPLAY_ONLINE_UPDATE_STATE_NOT_CHECKED;
    }
}

static esp_err_t write_network_time_to_rtc(const network_time_datetime_t *network_time)
{
    pcf85063_datetime_t requested = {
        .year = network_time->year,
        .month = network_time->month,
        .day = network_time->day,
        .hour = network_time->hour,
        .minute = network_time->minute,
        .second = network_time->second,
        .clock_integrity = true,
    };
    esp_err_t error = pcf85063_calculate_weekday(
        requested.year, requested.month, requested.day, &requested.weekday);
    if (error != ESP_OK) {
        return error;
    }
    error = pcf85063_write(&requested);
    if (error != ESP_OK) {
        return error;
    }

    pcf85063_datetime_t verified = {0};
    error = pcf85063_read(&verified);
    if (error != ESP_OK || !verified.clock_integrity) {
        return error == ESP_OK ? ESP_ERR_INVALID_RESPONSE : error;
    }
    ESP_LOGI(TAG, "NTP_RTC_SET_OK %04u-%02u-%02u %02u:%02u:%02u weekday=%u",
             verified.year, verified.month, verified.day, verified.hour,
             verified.minute, verified.second, verified.weekday);
    return ESP_OK;
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const size_t psram_bytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "%s firmware v%s (ESP-IDF %s)", BOARD_NAME, app->version, app->idf_ver);
    ESP_LOGI(TAG, "PSRAM available: %u KiB", (unsigned)(psram_bytes / 1024U));
    ESP_LOGI(TAG, "dashboard with SoftAP provisioning, NVS Wi-Fi settings, and SNTP RTC sync");

    uint8_t *const image_bitmap_snapshot = heap_caps_malloc(
        MONO_IMAGE_BITMAP_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (image_bitmap_snapshot == NULL) {
        ESP_LOGW(TAG, "microSD image display snapshot unavailable");
    }

    const bool display_ready = display_init() == ESP_OK;
    if (display_ready) {
        display_show_status("RLCD FIRMWARE", "Starting dashboard");
    } else {
        ESP_LOGE(TAG, "ST7305 display initialization failed");
    }

    const esp_err_t storage_error = app_storage_init();
    if (storage_error != ESP_OK) {
        ESP_LOGE(TAG, "persistent storage initialization failed: %s",
                 esp_err_to_name(storage_error));
    }

    const esp_err_t conversation_config_error =
        storage_error == ESP_OK ? conversation_config_init()
                                : storage_error;
    if (conversation_config_error != ESP_OK) {
        ESP_LOGW(TAG, "cloud voice configuration unavailable: %s",
                 esp_err_to_name(conversation_config_error));
    }

    app_settings_t settings;
    app_settings_defaults(&settings);
    uint32_t settings_generation = 0U;
    esp_err_t settings_error = storage_error == ESP_OK
                                   ? app_settings_init()
                                   : storage_error;
    if (settings_error == ESP_OK ||
        settings_error == ESP_ERR_NOT_SUPPORTED) {
        app_settings_snapshot_t snapshot = {0};
        const esp_err_t snapshot_error =
            app_settings_get_snapshot(&snapshot);
        if (snapshot_error == ESP_OK) {
            settings = snapshot.settings;
            settings_generation = snapshot.generation;
        } else {
            app_settings_defaults(&settings);
            settings_error = snapshot_error;
        }
    }
    if (settings_error != ESP_OK) {
        ESP_LOGW(TAG, "using safe settings defaults: %s",
                 esp_err_to_name(settings_error));
    }
    if (app_settings_apply_timezone(&settings) != ESP_OK) {
        ESP_LOGW(TAG, "could not apply configured time zone; using process default");
    }
    app_power_runtime_t power_runtime;
    if (!app_power_runtime_init(
            &power_runtime, settings.manual_saving_requested)) {
        app_settings_defaults(&settings);
        (void)app_power_runtime_init(
            &power_runtime, settings.manual_saving_requested);
    }
    app_power_policy_t power_policy;
    if (!app_power_policy_for_runtime(&power_runtime, &power_policy)) {
        ESP_LOGE(TAG, "could not initialize power policy");
        return;
    }
    const esp_err_t volume_error =
        audio_diagnostics_set_playback_volume(
            settings.audio_playback_volume);
    if (volume_error != ESP_OK) {
        ESP_LOGW(TAG, "could not apply audio playback volume: %s",
                 esp_err_to_name(volume_error));
    }

    const esp_err_t i2c_error = board_i2c_init();
    const bool i2c_ready = i2c_error == ESP_OK;
    if (!i2c_ready) {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(i2c_error));
    }

    bool rtc_driver_ready = false;
    bool startup_rtc_readable = false;
    bool sensor_driver_ready = false;
    uint16_t sensor_id = 0;
    pcf85063_datetime_t startup_datetime = {0};

    if (i2c_ready) {
        esp_err_t error = board_i2c_probe(PCF85063_I2C_ADDRESS, 100);
        if (error == ESP_OK) {
            error = pcf85063_init(board_i2c_bus());
        }
        rtc_driver_ready = error == ESP_OK;
        if (rtc_driver_ready) {
            ESP_LOGI(TAG, "PCF85063 detected at I2C address 0x%02X", PCF85063_I2C_ADDRESS);
            error = pcf85063_read(&startup_datetime);
            startup_rtc_readable = error == ESP_OK;
            if (!startup_rtc_readable) {
                ESP_LOGW(TAG, "initial RTC read failed: %s",
                         esp_err_to_name(error));
            }
        } else {
            ESP_LOGW(TAG, "PCF85063 not ready: %s", esp_err_to_name(error));
        }

        error = board_i2c_probe(SHTC3_I2C_ADDRESS, 100);
        if (error == ESP_OK) {
            error = shtc3_init(board_i2c_bus(), &sensor_id);
        }
        sensor_driver_ready = error == ESP_OK;
        if (sensor_driver_ready) {
            ESP_LOGI(TAG, "SHTC3 detected at 0x%02X, sensor ID=0x%04X",
                     SHTC3_I2C_ADDRESS, sensor_id);
        } else {
            ESP_LOGW(TAG, "SHTC3 not ready: %s", esp_err_to_name(error));
        }
    }

    const esp_err_t audio_init_error =
        i2c_ready ? audio_diagnostics_init(board_i2c_bus())
                  : ESP_ERR_INVALID_STATE;
    audio_diagnostics_status_t audio_status = {0};
    audio_diagnostics_get_status(&audio_status);
    if (audio_init_error == ESP_OK) {
        ESP_LOGI(TAG, "audio diagnostics ready: ES8311 speaker and ES7210 microphones");
    } else {
        ESP_LOGW(TAG,
                 "audio diagnostics partially unavailable: speaker=%d microphones=%d error=%s",
                 audio_status.speaker_ready,
                 audio_status.microphones_ready,
                 esp_err_to_name(audio_init_error));
    }

    bool rtc_backup_monitor_ready = false;
    if (storage_error == ESP_OK) {
        const esp_reset_reason_t reset_reason = esp_reset_reason();
        const esp_err_t error = rtc_backup_monitor_init(
            reset_reason == ESP_RST_POWERON, startup_rtc_readable,
            startup_rtc_readable ? &startup_datetime : NULL);
        rtc_backup_monitor_ready = error == ESP_OK;
        if (rtc_backup_monitor_ready) {
            ESP_LOGI(TAG, "RTC backup status: %s (reset reason %d)",
                     rtc_backup_status_name(rtc_backup_monitor_status()),
                     (int)reset_reason);
        } else {
            ESP_LOGW(TAG, "RTC backup monitor unavailable: %s",
                     esp_err_to_name(error));
        }
    }

    battery_measurement_t battery_measurement = {0};
    bool startup_battery_valid = false;
    const esp_err_t battery_init_error = battery_init();
    const bool battery_driver_ready = battery_init_error == ESP_OK;
    if (battery_driver_ready) {
        ESP_LOGI(TAG, "battery monitor ready on GPIO %d", BOARD_BATTERY_ADC_GPIO);
        const esp_err_t initial_battery_error =
            battery_read(&battery_measurement);
        if (initial_battery_error == ESP_OK) {
            startup_battery_valid = true;
            bool effective_mode_changed = false;
            if (app_power_runtime_observe_battery(
                    &power_runtime, true, battery_measurement.percent,
                    &effective_mode_changed) &&
                effective_mode_changed) {
                (void)app_power_policy_for_runtime(&power_runtime,
                                                   &power_policy);
            }
            ESP_LOGI(TAG,
                     "initial battery %u mV, %u%%; manual=%s state=%s",
                     battery_measurement.voltage_mv,
                     battery_measurement.percent,
                     settings.manual_saving_requested ? "on" : "off",
                     power_runtime.effective_state ==
                             APP_POWER_STATE_SAVING
                         ? "saving"
                         : "normal");
        } else {
            ESP_LOGW(TAG, "initial battery read failed: %s",
                     esp_err_to_name(initial_battery_error));
        }
    } else {
        ESP_LOGW(TAG, "battery monitor unavailable: %s", esp_err_to_name(battery_init_error));
    }

    /* The stock board exposes neither charger STAT nor VBUS to an ESP32 GPIO.
     * USB Serial/JTAG SOF therefore provides a reliable data-host signal only;
     * wall chargers and power banks without USB data remain undetectable. */
    const bool startup_usb_data_host_connected =
        usb_serial_jtag_is_connected();
    bool usb_power_state_changed = false;
    if (!app_power_runtime_observe_usb_data_host(
            &power_runtime, startup_usb_data_host_connected,
            &usb_power_state_changed) ||
        !app_power_policy_for_runtime(&power_runtime, &power_policy)) {
        ESP_LOGW(TAG, "could not initialize USB data-host power policy");
    } else if (startup_usb_data_host_connected) {
        ESP_LOGI(TAG,
                 "USB data host connected at startup; manual=%s state=%s",
                 settings.manual_saving_requested ? "on" : "off",
                 power_runtime.effective_state == APP_POWER_STATE_SAVING
                     ? "saving"
                     : "normal");
    }

    /* Resolve the automatic low-battery rule from the initial sample before
     * enabling DFS or allowing the network task to start automatic work. */
    const esp_err_t power_manager_error = power_manager_init(
        power_runtime.effective_state == APP_POWER_STATE_SAVING);
    if (power_manager_error != ESP_OK) {
        ESP_LOGW(TAG,
                 "dynamic frequency scaling unavailable; display and network saving policy remains active: %s",
                 esp_err_to_name(power_manager_error));
    }

    const esp_err_t button_init_error = board_buttons_init();
    const bool buttons_ready = button_init_error == ESP_OK;
    if (buttons_ready) {
        ESP_LOGI(TAG, "BOOT button ready on GPIO %d; KEY button ready on GPIO %d",
                 BOARD_BOOT_GPIO, BOARD_KEY_GPIO);
    } else {
        ESP_LOGW(TAG, "board buttons unavailable: %s",
                 esp_err_to_name(button_init_error));
    }

    const esp_err_t network_error =
        network_time_init(power_policy.automatic_network);
    if (network_error != ESP_OK) {
        ESP_LOGW(TAG, "automatic network time unavailable: %s",
                 esp_err_to_name(network_error));
    }
    if (startup_rtc_readable && startup_datetime.clock_integrity) {
        const esp_err_t seed_error =
            seed_system_time_from_rtc(&startup_datetime);
        if (seed_error == ESP_OK) {
            ESP_LOGI(TAG, "system clock initialized from RTC for HTTPS");
        } else {
            ESP_LOGW(TAG, "could not initialize system clock from RTC: %s",
                     esp_err_to_name(seed_error));
        }
    }

    const esp_err_t usb_commands_error = usb_commands_init();
    if (usb_commands_error != ESP_OK) {
        ESP_LOGW(TAG, "USB command console unavailable: %s",
                 esp_err_to_name(usb_commands_error));
    }

    const esp_err_t gallery_error = gallery_download_init();
    if (gallery_error != ESP_OK) {
        ESP_LOGW(TAG, "gallery service unavailable: %s",
                 esp_err_to_name(gallery_error));
    }
    const esp_err_t firmware_update_error = firmware_update_init();
    if (firmware_update_error != ESP_OK) {
        ESP_LOGW(TAG, "firmware update service unavailable: %s",
                 esp_err_to_name(firmware_update_error));
    }
    const esp_err_t online_update_error =
        online_firmware_update_init(
            app->version,
            settings.update_channel == APP_UPDATE_CHANNEL_BETA);
    if (online_update_error != ESP_OK) {
        ESP_LOGW(TAG, "online update service unavailable: %s",
                 esp_err_to_name(online_update_error));
    }
    bool running_image_confirmation_pending =
        firmware_update_error == ESP_OK && display_ready && buttons_ready &&
        network_error == ESP_OK;

    display_dashboard_t dashboard = {
        .lunar_text = NULL,
        .show_seconds = power_policy.show_seconds,
        .temperature_fahrenheit =
            settings.temperature_unit ==
            APP_TEMPERATURE_UNIT_FAHRENHEIT,
        .battery_valid = startup_battery_valid,
        .battery_percent = battery_measurement.percent,
        .usb_data_host_connected = startup_usb_data_host_connected,
    };
    pcf85063_datetime_t datetime = {0};
    shtc3_measurement_t measurement = {0};
    environment_comfort_tracker_t comfort_tracker;
    environment_comfort_init(&comfort_tracker);
    environment_observation_t environment_observation;
    environment_observation_init(&environment_observation);
    bool environment_stale = false;
    chinese_lunar_date_t lunar_date = {0};
    char lunar_text[64] = {0};
    network_time_status_t network_status = {0};
    network_time_datetime_t last_sync_time = {0};
    bool last_sync_valid = false;
    firmware_update_status_t firmware_update_status = {0};
    (void)firmware_update_get_status(&firmware_update_status);
    online_firmware_update_status_t online_update_status = {0};
    (void)online_firmware_update_get_status(&online_update_status);
    gallery_download_status_t gallery_status = {0};
    (void)gallery_download_get_status(&gallery_status);
    button_state_t key_button_state;
    button_state_t boot_button_state;
    button_state_init_custom(&key_button_state,
                             buttons_ready && board_key_is_pressed(),
                             BUTTON_HOLD_PROMPT_MS,
                             BUTTON_LONG_PRESS_DISABLED_MS);
    button_state_init_custom(&boot_button_state,
                             buttons_ready && board_boot_is_pressed(),
                             BUTTON_HOLD_PROMPT_MS,
                             BUTTON_LONG_PRESS_DISABLED_MS);
    app_page_state_t page_state;
    app_page_state_init(&page_state);
    voice_session_state_t voice_session;
    voice_session_state_init(&voice_session);
    audio_voice_status_t voice_status = {0};
    audio_voice_get_status(&voice_status);
    uint32_t previous_voice_revision = voice_status.revision;
    conversation_config_status_t cloud_config_status = {0};
    if (conversation_config_error == ESP_OK) {
        (void)conversation_config_get_status(&cloud_config_status);
    }
    audio_conversation_status_t cloud_voice_status = {0};
    audio_conversation_get_status(&cloud_voice_status);
    uint32_t previous_cloud_voice_revision =
        cloud_voice_status.revision;
    audio_conversation_state_t previous_cloud_voice_state =
        cloud_voice_status.state;
    bool cloud_session_selected = false;
    voice_command_action_t pending_voice_action =
        VOICE_COMMAND_ACTION_NONE;
    esp_err_t voice_session_error = ESP_OK;
    app_image_delete_ui_t image_delete_ui;
    app_image_delete_ui_init(&image_delete_ui);
    char image_delete_target[SD_IMAGE_FILENAME_CAPACITY] = {0};
    app_image_delete_target_t image_delete_target_snapshot = {
        .filename = image_delete_target,
    };
    esp_err_t image_delete_error = ESP_OK;
    bool image_delete_wait_for_button_release = false;
    const esp_err_t sd_image_init_error = sd_image_store_init();
    if (sd_image_init_error != ESP_OK) {
        ESP_LOGW(TAG, "microSD image service unavailable: %s",
                 esp_err_to_name(sd_image_init_error));
    }
    sd_image_status_t initial_sd_image_status = {0};
    sd_image_store_get_status(&initial_sd_image_status);
    app_page_state_set_image_available(
        &page_state,
        image_bitmap_snapshot != NULL &&
            initial_sd_image_status.state == SD_IMAGE_STATE_READY);
    sd_image_state_t previous_sd_image_state =
        initial_sd_image_status.state;
    uint32_t previous_sd_image_revision =
        initial_sd_image_status.revision;
    sd_image_delete_status_t initial_image_delete_status = {0};
    sd_image_store_get_delete_status(&initial_image_delete_status);
    uint32_t previous_image_delete_revision =
        initial_image_delete_status.revision;
    alarm_schedule_t alarm_schedule =
        alarm_schedule_from_settings(&settings);
    alarm_scheduler_t alarm_scheduler;
    alarm_scheduler_init(&alarm_scheduler);
    alarm_input_gate_t alarm_input_gate = {0};
    if (storage_error == ESP_OK) {
        alarm_history_record_t alarm_history_record = {0};
        bool alarm_history_found = false;
        const esp_err_t alarm_history_error = alarm_history_load(
            &alarm_history_record, &alarm_history_found);
        if (alarm_history_error != ESP_OK) {
            ESP_LOGW(TAG, "alarm history unavailable: %s",
                     esp_err_to_name(alarm_history_error));
        } else if (alarm_history_found) {
            (void)alarm_scheduler_restore_last_fired(
                &alarm_scheduler, alarm_history_record.date_key,
                alarm_history_record.schedule_revision);
            ESP_LOGI(TAG,
                     "restored alarm occurrence date=%u revision=0x%08x",
                     (unsigned)alarm_history_record.date_key,
                     (unsigned)alarm_history_record.schedule_revision);
        }
    }
    const TickType_t initial_tick = xTaskGetTickCount();
    TickType_t last_cloud_voice_ui_update = initial_tick;
    TickType_t last_button_update = initial_tick;
    TickType_t last_periodic_update = initial_tick;
    TickType_t last_rtc_read = initial_tick;
    TickType_t last_sensor_read = initial_tick;
    TickType_t last_battery_read = initial_tick;
    uint32_t rtc_read_wait_ms = 0U;
    TickType_t manual_sync_ui_started = initial_tick;
    TickType_t power_setting_ui_started = initial_tick;
    TickType_t firmware_update_result_started = initial_tick;
    TickType_t gallery_result_started = initial_tick;
    network_time_state_t previous_network_state = NETWORK_TIME_STATE_UNINITIALIZED;
    network_time_state_t previous_manual_sync_network_state =
        NETWORK_TIME_STATE_UNINITIALIZED;
    TickType_t provisioning_started = initial_tick;
    app_display_mode_t previous_display_mode = APP_DISPLAY_NONE;
    manual_sync_ui_t manual_sync_ui = MANUAL_SYNC_UI_NONE;
    power_setting_ui_t power_setting_ui = POWER_SETTING_UI_NONE;
    bool power_setting_target = settings.manual_saving_requested;
    bool power_setting_apply_pending = false;
    bool power_setting_result_timer_started = false;
    esp_err_t manual_sync_error = ESP_OK;
    bool first_periodic_update = true;
    bool settings_refresh_requested = false;
    bool battery_read_pending = false;
    bool dual_button_release_gate = false;
    bool voice_button_release_gate = false;
    bool status_refresh_pending = false;
    uint8_t previous_portal_seconds = 0U;
    uint8_t previous_update_percent = 0U;
    bool setup_screen_dismissed = false;
    firmware_update_state_t previous_update_state =
        FIRMWARE_UPDATE_STATE_IDLE;
    online_update_state_t previous_online_update_state =
        ONLINE_UPDATE_STATE_IDLE;
    uint8_t previous_online_update_percent = 0U;
    gallery_download_state_t previous_gallery_state =
        GALLERY_DOWNLOAD_STATE_IDLE;
    uint8_t previous_gallery_percent = 0U;
    bool automatic_update_check_pending = false;
    uint32_t cycle = 0;

    while (true) {
        const TickType_t now = xTaskGetTickCount();
        const uint32_t button_elapsed_ms =
            (uint32_t)(now - last_button_update) * portTICK_PERIOD_MS;
        last_button_update = now;
        bool render_requested = false;
        bool dashboard_data_changed = false;
        bool calendar_data_changed = false;
        bool system_status_data_changed = false;
        bool online_update_data_changed = false;
        bool image_delete_ui_transitioned = false;
        sd_image_status_t latest_sd_image_status = {0};
        sd_image_store_get_status(&latest_sd_image_status);
        if (latest_sd_image_status.state != previous_sd_image_state ||
            latest_sd_image_status.revision !=
                previous_sd_image_revision) {
            const bool image_ready =
                image_bitmap_snapshot != NULL &&
                latest_sd_image_status.state == SD_IMAGE_STATE_READY;
            app_page_state_set_image_available(&page_state, image_ready);
            const bool state_changed =
                latest_sd_image_status.state != previous_sd_image_state;
            previous_sd_image_state = latest_sd_image_status.state;
            previous_sd_image_revision =
                latest_sd_image_status.revision;
            render_requested = true;
            if (state_changed) {
                ESP_LOGI(TAG, "microSD image state changed: %s",
                         sd_image_state_name(
                             latest_sd_image_status.state));
            } else {
                ESP_LOGI(TAG,
                         "microSD image cache changed: revision=%u, "
                         "images=%u",
                         (unsigned)latest_sd_image_status.revision,
                         (unsigned)latest_sd_image_status.image_count);
            }
        }
        sd_image_delete_status_t latest_image_delete_status = {0};
        sd_image_store_get_delete_status(&latest_image_delete_status);
        if (latest_image_delete_status.revision !=
            previous_image_delete_revision) {
            previous_image_delete_revision =
                latest_image_delete_status.revision;
            if (latest_image_delete_status.state ==
                    SD_IMAGE_DELETE_STATE_SUCCESS &&
                app_image_delete_ui_complete(&image_delete_ui, true)) {
                image_delete_error = ESP_OK;
                image_delete_ui_transitioned = true;
                ESP_LOGI(TAG, "image deleted from microSD: %s",
                         latest_image_delete_status.filename);
                render_requested = true;
            } else if (latest_image_delete_status.state ==
                           SD_IMAGE_DELETE_STATE_FAILED &&
                       app_image_delete_ui_complete(&image_delete_ui,
                                                    false)) {
                image_delete_error =
                    latest_image_delete_status.last_error;
                image_delete_ui_transitioned = true;
                ESP_LOGW(TAG, "image delete failed for %s: %s",
                         latest_image_delete_status.filename,
                         esp_err_to_name(image_delete_error));
                render_requested = true;
            }
        }
        bool voice_data_changed = false;
        conversation_config_status_t latest_cloud_config = {0};
        if (conversation_config_error == ESP_OK &&
            conversation_config_get_status(&latest_cloud_config) ==
                ESP_OK &&
            !conversation_config_status_equal(
                &latest_cloud_config, &cloud_config_status)) {
            cloud_config_status = latest_cloud_config;
            voice_data_changed = true;
            render_requested = true;
        }

        audio_voice_status_t latest_voice_status = {0};
        audio_voice_get_status(&latest_voice_status);
        const bool offline_voice_data_changed =
            latest_voice_status.revision != previous_voice_revision;
        voice_status = latest_voice_status;
        if (offline_voice_data_changed) {
            previous_voice_revision = voice_status.revision;
            const bool image_available =
                latest_sd_image_status.state ==
                    SD_IMAGE_STATE_READY &&
                latest_sd_image_status.image_count > 0U;
            if (!cloud_session_selected &&
                accept_voice_engine_result(
                    &voice_session, &voice_status,
                    image_available, &pending_voice_action,
                    &voice_session_error)) {
                ESP_LOGI(TAG,
                         "offline voice result accepted: %s",
                         voice_session_state_name(
                             voice_session_state_phase(
                                 &voice_session)));
            }
            if (!cloud_session_selected) {
                voice_data_changed = true;
            }
            render_requested = true;
        }

        audio_conversation_get_status(&cloud_voice_status);
        if (cloud_voice_status.revision !=
            previous_cloud_voice_revision) {
            const bool phase_changed =
                cloud_voice_status.state !=
                previous_cloud_voice_state;
            previous_cloud_voice_revision =
                cloud_voice_status.revision;
            previous_cloud_voice_state =
                cloud_voice_status.state;

            bool session_transitioned = false;
            if (cloud_session_selected) {
                session_transitioned =
                    accept_cloud_conversation_status(
                        &voice_session, &cloud_voice_status,
                        &voice_session_error);
            }

            const bool ui_update_due =
                phase_changed || session_transitioned ||
                conversation_state_is_terminal(
                    cloud_voice_status.state) ||
                now - last_cloud_voice_ui_update >=
                    pdMS_TO_TICKS(APP_CLOUD_VOICE_UI_UPDATE_MS);
            if (cloud_session_selected && ui_update_due) {
                last_cloud_voice_ui_update = now;
                voice_data_changed = true;
                render_requested = true;
            }
        }
        if (!cloud_session_selected &&
            conversation_state_is_terminal(cloud_voice_status.state) &&
            audio_conversation_dismiss() == ESP_OK) {
            /* This also covers an alarm invalidating the UI generation after
             * the terminal revision was already consumed. Refresh the local
             * copy immediately so response text does not linger on app_main's
             * stack until a later status change. */
            audio_conversation_get_status(&cloud_voice_status);
            previous_cloud_voice_revision = cloud_voice_status.revision;
            previous_cloud_voice_state = cloud_voice_status.state;
            voice_data_changed = true;
            render_requested = true;
        }
        const bool voice_session_active =
            voice_session_state_is_active(&voice_session);

        (void)firmware_update_get_status(&firmware_update_status);
        if (firmware_update_status.state != previous_update_state ||
            firmware_update_status.percent != previous_update_percent) {
            if (firmware_update_status.state != previous_update_state &&
                firmware_update_state_is_dismissible(
                    firmware_update_status.state)) {
                firmware_update_result_started = now;
            }
            previous_update_state = firmware_update_status.state;
            previous_update_percent = firmware_update_status.percent;
            render_requested = true;
        }
        if (firmware_update_state_is_dismissible(
                firmware_update_status.state) &&
            now - firmware_update_result_started >=
                pdMS_TO_TICKS(APP_FIRMWARE_UPDATE_RESULT_MS)) {
            (void)firmware_update_dismiss_result();
            (void)firmware_update_get_status(&firmware_update_status);
            previous_update_state = firmware_update_status.state;
            previous_update_percent = firmware_update_status.percent;
            render_requested = true;
        }
        bool firmware_update_ui_active =
            firmware_update_status.state != FIRMWARE_UPDATE_STATE_IDLE;

        (void)gallery_download_get_status(&gallery_status);
        if (gallery_status.state != previous_gallery_state ||
            gallery_status.percent != previous_gallery_percent) {
            if ((gallery_status.state == GALLERY_DOWNLOAD_STATE_FAILED ||
                 gallery_status.state == GALLERY_DOWNLOAD_STATE_SUCCESS) &&
                gallery_status.state != previous_gallery_state) {
                gallery_result_started = now;
            }
            previous_gallery_state = gallery_status.state;
            previous_gallery_percent = gallery_status.percent;
            render_requested = true;
        }
        if ((gallery_status.state == GALLERY_DOWNLOAD_STATE_FAILED ||
             gallery_status.state == GALLERY_DOWNLOAD_STATE_SUCCESS) &&
            now - gallery_result_started >=
                pdMS_TO_TICKS(APP_GALLERY_RESULT_MS)) {
            (void)gallery_download_dismiss_result();
            (void)gallery_download_get_status(&gallery_status);
            previous_gallery_state = gallery_status.state;
            previous_gallery_percent = gallery_status.percent;
            render_requested = true;
        }
        const bool gallery_download_ui_active =
            gallery_download_state_is_active(gallery_status.state);

        (void)online_firmware_update_get_status(&online_update_status);
        if (online_update_status.state != previous_online_update_state ||
            online_update_status.percent != previous_online_update_percent) {
            previous_online_update_state = online_update_status.state;
            previous_online_update_percent = online_update_status.percent;
            online_update_data_changed = true;
            if (app_page_state_current(&page_state) ==
                APP_PAGE_ONLINE_UPDATE) {
                render_requested = true;
            }
        }
        const bool online_update_busy =
            online_update_state_is_busy(online_update_status.state);
        const bool online_update_confirmation_active =
            online_update_status.state ==
            ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION;
        const bool periodic_update =
            first_periodic_update ||
            now - last_periodic_update >=
                pdMS_TO_TICKS(APP_PERIODIC_UPDATE_MS);
        if (periodic_update) {
            const bool usb_data_host_connected =
                usb_serial_jtag_is_connected();
            if (usb_data_host_connected !=
                dashboard.usb_data_host_connected) {
                /* The confirmed USB data-host indication is independent of
                 * whether the CPU/network power transition can be applied
                 * in this pass. On disconnect, request a fresh battery read
                 * before the next dashboard draw. */
                dashboard.usb_data_host_connected =
                    usb_data_host_connected;
                if (!usb_data_host_connected) {
                    battery_read_pending = true;
                }
                dashboard_data_changed = true;
                system_status_data_changed = true;
                render_requested = true;
            }
            if (usb_data_host_connected !=
                power_runtime.usb_data_host_connected) {
                app_power_runtime_t next_power_runtime = power_runtime;
                app_power_policy_t next_power_policy = {0};
                bool effective_mode_changed = false;
                esp_err_t apply_error =
                    app_power_runtime_observe_usb_data_host(
                        &next_power_runtime, usb_data_host_connected,
                        &effective_mode_changed) &&
                            app_power_policy_for_runtime(
                                &next_power_runtime,
                                &next_power_policy)
                        ? ESP_OK
                        : ESP_ERR_INVALID_ARG;
                if (apply_error == ESP_OK && effective_mode_changed) {
                    apply_error = apply_runtime_power_transition(
                        &power_runtime, &power_policy,
                        &next_power_runtime, &next_power_policy,
                        network_error == ESP_OK);
                }
                if (apply_error == ESP_OK) {
                    power_runtime = next_power_runtime;
                    power_policy = next_power_policy;
                    if (effective_mode_changed) {
                        dashboard.show_seconds =
                            power_policy.show_seconds;
                        rtc_read_wait_ms = 0U;
                        calendar_data_changed = true;
                    }
                    ESP_LOGI(
                        TAG, "USB data host %s%s",
                        usb_data_host_connected ? "connected"
                                                : "no longer detected",
                        effective_mode_changed
                            ? (next_power_runtime.effective_state ==
                                       APP_POWER_STATE_SAVING
                                   ? "; effective state changed to saving"
                                   : "; effective state changed to normal")
                            : "");
                } else if (apply_error != ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(TAG,
                             "USB data-host power transition pending: %s",
                             esp_err_to_name(apply_error));
                }
            }
        }
        bool runtime_settings_changed = false;
        if ((periodic_update || settings_refresh_requested) &&
            (settings_error == ESP_OK ||
             settings_error == ESP_ERR_NOT_SUPPORTED)) {
            /* A device-side save requests one immediate refresh. If the
             * runtime transition is temporarily unavailable, the existing
             * periodic path retries it without spinning or flooding logs. */
            settings_refresh_requested = false;
            app_settings_snapshot_t snapshot = {0};
            const esp_err_t snapshot_error =
                app_settings_get_snapshot(&snapshot);
            if (snapshot_error == ESP_OK &&
                snapshot.generation != settings_generation) {
                const bool configured_power_changed =
                    snapshot.settings.manual_saving_requested !=
                    settings.manual_saving_requested;
                app_power_runtime_t next_power_runtime = power_runtime;
                app_power_policy_t next_power_policy = {0};
                bool effective_mode_changed = false;
                esp_err_t apply_error =
                    app_power_runtime_set_manual_saving_requested(
                        &next_power_runtime,
                        snapshot.settings.manual_saving_requested,
                        &effective_mode_changed) &&
                            app_power_policy_for_runtime(
                                &next_power_runtime,
                                &next_power_policy)
                        ? ESP_OK
                        : ESP_ERR_INVALID_ARG;
                if (apply_error == ESP_OK &&
                    snapshot.settings.audio_playback_volume !=
                        settings.audio_playback_volume) {
                    apply_error =
                        audio_diagnostics_set_playback_volume(
                            snapshot.settings.audio_playback_volume);
                }
                if (apply_error == ESP_OK &&
                    snapshot.settings.utc_offset_minutes !=
                        settings.utc_offset_minutes) {
                    apply_error = app_settings_apply_timezone(
                        &snapshot.settings);
                }
                if (apply_error == ESP_OK &&
                    online_update_error == ESP_OK &&
                    snapshot.settings.update_channel !=
                        settings.update_channel) {
                    apply_error =
                        online_firmware_update_set_beta_channel(
                            snapshot.settings.update_channel ==
                            APP_UPDATE_CHANNEL_BETA);
                }
                if (apply_error == ESP_OK) {
                    apply_error = apply_runtime_power_transition(
                        &power_runtime, &power_policy,
                        &next_power_runtime, &next_power_policy,
                        network_error == ESP_OK);
                }

                if (apply_error == ESP_OK) {
                    settings = snapshot.settings;
                    settings_generation = snapshot.generation;
                    power_runtime = next_power_runtime;
                    power_policy = next_power_policy;
                    if (configured_power_changed) {
                        battery_read_pending = true;
                    }
                    power_setting_apply_pending = false;
                    if (power_runtime.effective_state ==
                        APP_POWER_STATE_SAVING) {
                        automatic_update_check_pending = false;
                    }
                    alarm_schedule =
                        alarm_schedule_from_settings(&settings);
                    dashboard.show_seconds = power_policy.show_seconds;
                    dashboard.temperature_fahrenheit =
                        settings.temperature_unit ==
                        APP_TEMPERATURE_UNIT_FAHRENHEIT;
                    rtc_read_wait_ms = 0U;
                    runtime_settings_changed = true;
                    dashboard_data_changed = true;
                    calendar_data_changed = true;
                    system_status_data_changed = true;
                    online_update_data_changed = true;
                    render_requested = true;
                    if (power_setting_ui ==
                            POWER_SETTING_UI_APPLYING ||
                        power_setting_ui ==
                            POWER_SETTING_UI_PENDING) {
                        power_setting_ui =
                            settings.manual_saving_requested ==
                                    power_setting_target
                                ? POWER_SETTING_UI_SAVED
                                : POWER_SETTING_UI_SUPERSEDED;
                        power_setting_result_timer_started = false;
                    }
                    ESP_LOGI(TAG,
                             "saved settings applied without restart "
                             "(generation=%u manual=%s state=%s%s)",
                             (unsigned)settings_generation,
                             settings.manual_saving_requested ? "on"
                                                              : "off",
                             power_runtime.effective_state ==
                                     APP_POWER_STATE_SAVING
                                 ? "saving"
                                 : "normal",
                             effective_mode_changed ? " changed" : "");
                } else {
                    if (power_setting_apply_pending) {
                        power_setting_target =
                            snapshot.settings.manual_saving_requested;
                    }
                    if (power_setting_ui ==
                        POWER_SETTING_UI_APPLYING) {
                        power_setting_ui =
                            snapshot.settings.manual_saving_requested ==
                                    power_setting_target
                                ? POWER_SETTING_UI_PENDING
                                : POWER_SETTING_UI_SUPERSEDED;
                        power_setting_result_timer_started = false;
                        render_requested = true;
                    }
                    if (apply_error != ESP_ERR_INVALID_STATE) {
                        ESP_LOGW(
                            TAG,
                            "saved settings are pending runtime apply: %s",
                            esp_err_to_name(apply_error));
                    }
                }
            } else if (snapshot_error != ESP_OK) {
                if (power_setting_ui ==
                    POWER_SETTING_UI_APPLYING) {
                    power_setting_ui = POWER_SETTING_UI_PENDING;
                    power_setting_result_timer_started = false;
                    render_requested = true;
                }
                ESP_LOGW(TAG, "could not refresh saved settings: %s",
                         esp_err_to_name(snapshot_error));
            }
        }
        button_event_t key_event = BUTTON_EVENT_NONE;
        button_event_t boot_event = BUTTON_EVENT_NONE;
        bool key_pressed = false;
        bool boot_pressed = false;
        alarm_scheduler_input_t alarm_input =
            ALARM_SCHEDULER_INPUT_NONE;
        const bool alarm_was_ringing =
            alarm_scheduler.state == ALARM_SCHEDULER_RINGING;
        const app_page_t input_page = app_page_state_current(&page_state);
        if (app_image_delete_ui_state(&image_delete_ui) ==
                APP_IMAGE_DELETE_UI_CONFIRMING &&
            (input_page != APP_PAGE_IMAGE ||
             !image_delete_target_matches_status(
                 &image_delete_target_snapshot,
                 &latest_sd_image_status))) {
            (void)app_image_delete_ui_cancel(&image_delete_ui);
            image_delete_wait_for_button_release = true;
            memset(image_delete_target, 0,
                   sizeof(image_delete_target));
            image_delete_target_snapshot.ready = false;
            render_requested = true;
            ESP_LOGI(TAG,
                     "image delete confirmation cancelled: image changed");
        }
        if (buttons_ready) {
            configure_key_timing(&key_button_state, input_page,
                                 online_update_status.state);
            configure_boot_timing(&boot_button_state, input_page);
            key_pressed = board_key_is_pressed();
            boot_pressed = board_boot_is_pressed();
            key_event = button_state_update(
                &key_button_state, key_pressed, button_elapsed_ms);
            boot_event = button_state_update(
                &boot_button_state, boot_pressed, button_elapsed_ms);
        }
        const bool key_debounced_pressed =
            button_state_is_pressed(&key_button_state);
        const bool boot_debounced_pressed =
            button_state_is_pressed(&boot_button_state);
        const bool key_pressed_or_debounced =
            key_pressed || key_debounced_pressed;
        const bool boot_pressed_or_debounced =
            boot_pressed || boot_debounced_pressed;
        if (key_pressed_or_debounced &&
            boot_pressed_or_debounced) {
            dual_button_release_gate = true;
        }
        if (!key_pressed_or_debounced &&
            app_image_delete_ui_note_key_released(
                &image_delete_ui)) {
            render_requested = true;
            ESP_LOGI(TAG,
                     "image delete confirmation armed after KEY release");
        }
        const bool image_delete_release_gate_was_active =
            image_delete_wait_for_button_release;
        if (image_delete_wait_for_button_release &&
            !key_pressed_or_debounced && !boot_pressed_or_debounced) {
            image_delete_wait_for_button_release = false;
        }
        const bool alarm_gate_was_blocking =
            alarm_input_gate_is_blocking(&alarm_input_gate);
        if (alarm_was_ringing || alarm_gate_was_blocking) {
            alarm_input = alarm_input_gate_update(
                &alarm_input_gate, key_pressed_or_debounced,
                boot_pressed_or_debounced,
                key_event == BUTTON_EVENT_SHORT_PRESS,
                boot_event == BUTTON_EVENT_SHORT_PRESS);
            if (!alarm_was_ringing) {
                alarm_input = ALARM_SCHEDULER_INPUT_NONE;
            }
        }

        if (periodic_update) {
            network_time_datetime_t synchronized_time = {0};
            if (network_time_take_datetime(&synchronized_time)) {
                last_sync_time = synchronized_time;
                last_sync_valid = true;
                automatic_update_check_pending =
                    power_policy.automatic_network;
                system_status_data_changed = true;
                esp_err_t rtc_sync_error = ESP_OK;
                if (!rtc_driver_ready) {
                    rtc_sync_error = ESP_ERR_INVALID_STATE;
                    ESP_LOGW(TAG,
                             "SNTP succeeded but PCF85063 is unavailable");
                } else {
                    rtc_sync_error =
                        write_network_time_to_rtc(&synchronized_time);
                    if (rtc_sync_error != ESP_OK) {
                        ESP_LOGW(TAG,
                                 "could not write SNTP time to RTC: %s",
                                 esp_err_to_name(rtc_sync_error));
                    }
                }
                if (manual_sync_ui == MANUAL_SYNC_UI_ACTIVE) {
                    manual_sync_error = rtc_sync_error;
                    manual_sync_ui = rtc_sync_error == ESP_OK
                                         ? MANUAL_SYNC_UI_SUCCESS
                                         : MANUAL_SYNC_UI_FAILED;
                    manual_sync_ui_started = now;
                    render_requested = true;
                }
            }
        }
        usb_commands_poll(rtc_driver_ready);

        bool rtc_alarm_sample_attempted = false;
        esp_err_t rtc_alarm_sample_error = ESP_OK;
        if (settings.alarm_enabled && rtc_driver_ready && periodic_update) {
            const bool status_page_needs_fresh_alarm_sample =
                app_page_state_current(&page_state) == APP_PAGE_STATUS &&
                status_refresh_pending;
            const bool rtc_alarm_sample_due =
                first_periodic_update ||
                status_page_needs_fresh_alarm_sample ||
                now - last_rtc_read >= pdMS_TO_TICKS(rtc_read_wait_ms);
            if (rtc_alarm_sample_due) {
                rtc_alarm_sample_attempted = true;
                rtc_alarm_sample_error = pcf85063_read(&datetime);
            }
        }

        const alarm_clock_observation_t alarm_clock =
            rtc_alarm_sample_attempted
                ? (rtc_alarm_sample_error == ESP_OK
                       ? alarm_clock_from_rtc(&datetime)
                       : (alarm_clock_observation_t){0})
                : alarm_clock_from_dashboard(&dashboard);
        const alarm_scheduler_result_t alarm_result =
            alarm_scheduler_update(
                &alarm_scheduler, &alarm_schedule, &alarm_clock,
                (uint32_t)((uint64_t)now * portTICK_PERIOD_MS),
                alarm_input);
        if (alarm_result.output ==
            ALARM_SCHEDULER_OUTPUT_START_RINGING) {
            if (alarm_result.snooze_available) {
                const alarm_history_record_t record = {
                    .schedule_revision = alarm_schedule.revision,
                    .date_key = alarm_clock.date_key,
                };
                const esp_err_t history_error =
                    alarm_history_store(&record);
                if (history_error != ESP_OK) {
                    ESP_LOGW(TAG,
                             "could not persist alarm occurrence: %s",
                             esp_err_to_name(history_error));
                }
            }
            if (voice_session_state_is_active(&voice_session)) {
                (void)cancel_voice_backend(
                    cloud_session_selected);
                (void)voice_session_state_alarm_started(
                    &voice_session);
                cloud_session_selected = false;
                pending_voice_action = VOICE_COMMAND_ACTION_NONE;
                voice_session_error = ESP_OK;
                ESP_LOGI(TAG,
                         "alarm preempted voice session");
            }
            const esp_err_t alert_error = audio_alert_start();
            if (alert_error != ESP_OK) {
                ESP_LOGW(TAG,
                         "alarm visual started without audio: %s",
                         esp_err_to_name(alert_error));
            }
            alarm_input_gate_arm(&alarm_input_gate,
                                 key_pressed_or_debounced,
                                 boot_pressed_or_debounced);
            app_page_state_note_activity(&page_state);
            render_requested = true;
            ESP_LOGI(TAG, "alarm ringing%s",
                     alarm_result.snooze_available ? ""
                                                   : " after snooze");
        } else if (alarm_result.output ==
                   ALARM_SCHEDULER_OUTPUT_STOP_RINGING) {
            const esp_err_t stop_error = audio_alert_stop();
            if (stop_error != ESP_OK &&
                stop_error != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "could not stop alarm audio: %s",
                         esp_err_to_name(stop_error));
            }
            alarm_input_gate_arm(&alarm_input_gate,
                                 key_pressed_or_debounced,
                                 boot_pressed_or_debounced);
            app_page_state_note_activity(&page_state);
            render_requested = true;
            ESP_LOGI(TAG, "alarm stopped%s",
                     alarm_result.state == ALARM_SCHEDULER_SNOOZED
                         ? "; snoozed for 5 minutes"
                         : "");
        }
        const bool alarm_modal_active =
            alarm_result.state == ALARM_SCHEDULER_RINGING;
        const bool alarm_started_this_loop =
            alarm_result.output ==
            ALARM_SCHEDULER_OUTPUT_START_RINGING;
        const bool alarm_button_events_suppressed =
            alarm_was_ringing || alarm_started_this_loop ||
            alarm_gate_was_blocking;

        if (alarm_started_this_loop &&
            power_setting_ui != POWER_SETTING_UI_NONE) {
            /* The alarm has visual priority. Give the power result a full
             * display interval again after the alarm is dismissed. */
            power_setting_result_timer_started = false;
        }

        if (alarm_started_this_loop &&
            app_image_delete_ui_cancel(&image_delete_ui)) {
            memset(image_delete_target, 0,
                   sizeof(image_delete_target));
            image_delete_target_snapshot.ready = false;
            render_requested = true;
            ESP_LOGI(TAG,
                     "image delete confirmation cancelled by alarm");
        }

        if (alarm_button_events_suppressed) {
            if (key_event != BUTTON_EVENT_NONE ||
                boot_event != BUTTON_EVENT_NONE) {
                app_page_state_note_activity(&page_state);
            }
            key_event = BUTTON_EVENT_NONE;
            boot_event = BUTTON_EVENT_NONE;
        } else if (dual_button_release_gate) {
            if (key_event != BUTTON_EVENT_NONE ||
                boot_event != BUTTON_EVENT_NONE) {
                app_page_state_note_activity(&page_state);
            }
            key_event = BUTTON_EVENT_NONE;
            boot_event = BUTTON_EVENT_NONE;
            if (!key_pressed_or_debounced &&
                !boot_pressed_or_debounced) {
                dual_button_release_gate = false;
                ESP_LOGI(TAG,
                         "both buttons released; input restored");
            }
        } else if (voice_button_release_gate) {
            if (key_event != BUTTON_EVENT_NONE ||
                boot_event != BUTTON_EVENT_NONE) {
                app_page_state_note_activity(&page_state);
            }
            key_event = BUTTON_EVENT_NONE;
            boot_event = BUTTON_EVENT_NONE;
            if (!key_pressed_or_debounced &&
                !boot_pressed_or_debounced) {
                voice_button_release_gate = false;
                ESP_LOGI(TAG,
                         "voice-session button release consumed");
            }
        } else if (image_delete_release_gate_was_active) {
            if (key_event != BUTTON_EVENT_NONE ||
                boot_event != BUTTON_EVENT_NONE) {
                app_page_state_note_activity(&page_state);
            }
            key_event = BUTTON_EVENT_NONE;
            boot_event = BUTTON_EVENT_NONE;
        } else if (voice_session_active) {
            const voice_session_phase_t voice_phase =
                voice_session_state_phase(&voice_session);
            audio_conversation_state_t cloud_input_state =
                cloud_voice_status.state;
            if (cloud_session_selected) {
                audio_conversation_get_status(&cloud_voice_status);
                cloud_input_state = cloud_voice_status.state;
            }
            if (boot_event == BUTTON_EVENT_SHORT_PRESS) {
                bool cancel_session =
                    !cloud_session_selected ||
                    cloud_input_state !=
                        AUDIO_CONVERSATION_STATE_FOLLOW_UP;
                if (cloud_session_selected &&
                    cloud_input_state ==
                        AUDIO_CONVERSATION_STATE_FOLLOW_UP) {
                    const esp_err_t end_error =
                        audio_conversation_end();
                    if (end_error == ESP_OK) {
                        ESP_LOGI(TAG,
                                 "BOOT short press: AI conversation ended");
                    } else {
                        audio_conversation_get_status(
                            &cloud_voice_status);
                        cancel_session =
                            cloud_voice_status.running &&
                            cloud_voice_status.state !=
                                AUDIO_CONVERSATION_STATE_FOLLOW_UP &&
                            !conversation_state_is_terminal(
                                cloud_voice_status.state);
                    }
                }
                if (cancel_session) {
                    (void)cancel_voice_backend(
                        cloud_session_selected);
                    if (voice_session_state_boot_short_press(
                            &voice_session) !=
                        VOICE_SESSION_ACTION_NONE) {
                        pending_voice_action =
                            VOICE_COMMAND_ACTION_NONE;
                        voice_session_error = ESP_OK;
                        ESP_LOGI(TAG,
                                 "BOOT short press: voice session cancelled");
                    }
                }
                render_requested = true;
            } else if (voice_phase ==
                           VOICE_SESSION_PHASE_WAITING_FOR_RELEASE &&
                       !key_pressed_or_debounced) {
                const voice_session_action_t release_action =
                    voice_session_state_key_released(
                        &voice_session);
                if (release_action ==
                    VOICE_SESSION_ACTION_START_LISTENING) {
                    const uint32_t generation =
                        voice_session_state_generation(
                            &voice_session);
                    const esp_err_t start_error =
                        cloud_session_selected
                            ? audio_conversation_release_key()
                            : audio_voice_start(generation);
                    if (start_error == ESP_OK) {
                        if (cloud_session_selected) {
                            audio_conversation_get_status(
                                &cloud_voice_status);
                            previous_cloud_voice_revision =
                                cloud_voice_status.revision;
                            ESP_LOGI(
                                TAG,
                                "KEY released: cloud voice capture requested");
                        } else {
                            audio_voice_get_status(&voice_status);
                            previous_voice_revision =
                                voice_status.revision;
                            ESP_LOGI(
                                TAG,
                                "KEY released: offline voice listening started");
                        }
                    } else {
                        voice_session_error = start_error;
                        if (cloud_session_selected) {
                            (void)voice_session_state_report_failure(
                                &voice_session, generation);
                            (void)audio_conversation_cancel();
                        } else {
                            (void)voice_session_state_alarm_started(
                                &voice_session);
                            uint32_t unused_generation = 0U;
                            (void)voice_session_state_begin(
                                &voice_session, false,
                                &unused_generation);
                        }
                        ESP_LOGW(TAG,
                                 "%s voice could not start: %s",
                                 cloud_session_selected ? "cloud"
                                                        : "offline",
                                 esp_err_to_name(start_error));
                    }
                    render_requested = true;
                }
            } else if (key_event == BUTTON_EVENT_SHORT_PRESS) {
                if (cloud_session_selected) {
                    if (cloud_input_state ==
                        AUDIO_CONVERSATION_STATE_LISTENING) {
                        if (audio_conversation_request_stop() == ESP_OK) {
                            ESP_LOGI(TAG,
                                     "KEY short press: finishing AI voice input");
                        }
                    } else if (cloud_input_state ==
                                   AUDIO_CONVERSATION_STATE_SPEAKING ||
                               cloud_input_state ==
                                   AUDIO_CONVERSATION_STATE_FOLLOW_UP) {
                        if (audio_conversation_continue() == ESP_OK) {
                            ESP_LOGI(TAG,
                                     "KEY short press: continuing AI conversation");
                        }
                    }
                } else if (voice_phase ==
                               VOICE_SESSION_PHASE_LISTENING ||
                           voice_phase ==
                               VOICE_SESSION_PHASE_RECOGNIZING) {
                    const esp_err_t stop_error =
                        stop_voice_backend(false);
                    if (stop_error == ESP_OK) {
                        ESP_LOGI(TAG,
                                 "KEY short press: finishing voice input");
                    }
                }
                render_requested = true;
            }
            if (key_event != BUTTON_EVENT_NONE ||
                boot_event != BUTTON_EVENT_NONE) {
                app_page_state_note_activity(&page_state);
            }
            key_event = BUTTON_EVENT_NONE;
            boot_event = BUTTON_EVENT_NONE;
        }
        if (gallery_download_ui_active) {
            if (key_event != BUTTON_EVENT_NONE ||
                boot_event != BUTTON_EVENT_NONE) {
                app_page_state_note_activity(&page_state);
            }
            key_event = BUTTON_EVENT_NONE;
            boot_event = BUTTON_EVENT_NONE;
        }
        if (power_setting_ui != POWER_SETTING_UI_NONE) {
            if (key_event != BUTTON_EVENT_NONE ||
                boot_event != BUTTON_EVENT_NONE) {
                app_page_state_note_activity(&page_state);
            }
            key_event = BUTTON_EVENT_NONE;
            boot_event = BUTTON_EVENT_NONE;
        }
        const app_image_delete_ui_state_t image_delete_input_state =
            app_image_delete_ui_state(&image_delete_ui);
        if (image_delete_input_state != APP_IMAGE_DELETE_UI_IDLE) {
            if (image_delete_input_state ==
                APP_IMAGE_DELETE_UI_CONFIRMING) {
                if (boot_event == BUTTON_EVENT_SHORT_PRESS) {
                    (void)app_image_delete_ui_cancel(&image_delete_ui);
                    image_delete_wait_for_button_release = true;
                    memset(image_delete_target, 0,
                           sizeof(image_delete_target));
                    image_delete_target_snapshot.ready = false;
                    ESP_LOGI(TAG,
                             "BOOT short press: image delete cancelled");
                    render_requested = true;
                } else if (key_event == BUTTON_EVENT_SHORT_PRESS) {
                    const bool target_unchanged =
                        image_delete_target_matches_status(
                            &image_delete_target_snapshot,
                            &latest_sd_image_status);
                    if (target_unchanged &&
                        app_image_delete_ui_confirm(
                            &image_delete_ui)) {
                        const esp_err_t delete_error =
                            sd_image_store_request_delete(
                                image_delete_target);
                        if (delete_error == ESP_OK) {
                            image_delete_error = ESP_OK;
                            ESP_LOGI(TAG,
                                     "KEY short press: deleting image %s",
                                     image_delete_target);
                        } else {
                            image_delete_error = delete_error;
                            (void)app_image_delete_ui_complete(
                                &image_delete_ui, false);
                            image_delete_ui_transitioned = true;
                            ESP_LOGW(TAG,
                                     "image delete could not start: %s",
                                     esp_err_to_name(delete_error));
                        }
                    } else {
                        (void)app_image_delete_ui_cancel(
                            &image_delete_ui);
                        image_delete_wait_for_button_release = true;
                        memset(image_delete_target, 0,
                               sizeof(image_delete_target));
                        image_delete_target_snapshot.ready = false;
                        ESP_LOGI(TAG,
                                 "image delete cancelled: image changed");
                    }
                    render_requested = true;
                }
            }
            if (key_event != BUTTON_EVENT_NONE ||
                boot_event != BUTTON_EVENT_NONE) {
                app_page_state_note_activity(&page_state);
            }
            key_event = BUTTON_EVENT_NONE;
            boot_event = BUTTON_EVENT_NONE;
        }
        if (key_event == BUTTON_EVENT_SHORT_PRESS) {
            if (manual_sync_ui == MANUAL_SYNC_UI_NONE &&
                !firmware_update_ui_active &&
                !online_update_confirmation_active &&
                !(online_update_busy &&
                  input_page == APP_PAGE_ONLINE_UPDATE)) {
                if (previous_display_mode == APP_DISPLAY_NETWORK_SETUP) {
                    setup_screen_dismissed = true;
                }
                const bool image_next_available =
                    input_page == APP_PAGE_IMAGE &&
                    previous_display_mode != APP_DISPLAY_NETWORK_SETUP &&
                    latest_sd_image_status.state == SD_IMAGE_STATE_READY &&
                    latest_sd_image_status.image_count > 1U;
                if (image_next_available) {
                    const size_t current_image =
                        sd_image_store_selected_index();
                    const size_t selected_image =
                        current_image <
                                latest_sd_image_status.image_count - 1U
                            ? current_image + 1U
                            : 0U;
                    char selected_filename[SD_IMAGE_FILENAME_CAPACITY] = {0};
                    const bool target_available = sd_image_store_filename_at(
                        selected_image, selected_filename,
                        sizeof(selected_filename));
                    const esp_err_t preferred_error = target_available
                        ? sd_image_store_select_preferred(selected_filename)
                        : ESP_ERR_NOT_FOUND;
                    if (preferred_error == ESP_OK) {
                        ESP_LOGI(TAG,
                                 "KEY short press: image selected %u/%u",
                                 (unsigned)(selected_image + 1U),
                                 (unsigned)
                                     latest_sd_image_status.image_count);
                    } else {
                        ESP_LOGW(TAG,
                                 "KEY short press: next image could not be "
                                 "selected: %s",
                                 esp_err_to_name(preferred_error));
                    }
                    app_page_state_note_activity(&page_state);
                } else {
                    app_page_state_key_short_press(&page_state);
                    if (app_page_state_current(&page_state) ==
                        APP_PAGE_STATUS) {
                        status_refresh_pending = true;
                    }
                    ESP_LOGI(TAG, "KEY short press: showing %s page",
                             page_name(
                                 app_page_state_current(&page_state)));
                }
                render_requested = true;
            }
        } else if (key_event == BUTTON_EVENT_HOLD_CANCELLED) {
            app_page_state_note_activity(&page_state);
            render_requested = true;
            if (input_page == APP_PAGE_SETTINGS) {
                ESP_LOGI(TAG,
                         "KEY hold cancelled; settings portal not opened");
            } else if (input_page == APP_PAGE_ONLINE_UPDATE) {
                ESP_LOGI(TAG,
                         "KEY hold cancelled; online update unchanged");
            } else {
                ESP_LOGI(TAG, "KEY hold released without an action");
            }
        } else if (key_event == BUTTON_EVENT_LONG_PRESS) {
            app_page_state_note_activity(&page_state);
            const app_page_action_t key_action =
                app_page_key_hold_action(input_page);
            if (key_action == APP_PAGE_ACTION_DELETE_IMAGE &&
                previous_display_mode != APP_DISPLAY_NETWORK_SETUP &&
                manual_sync_ui == MANUAL_SYNC_UI_NONE &&
                !firmware_update_ui_active &&
                !gallery_download_ui_active && !online_update_busy &&
                !online_update_confirmation_active &&
                latest_sd_image_status.state == SD_IMAGE_STATE_READY &&
                latest_sd_image_status.image_count > 0U &&
                latest_sd_image_status.filename[0] != '\0' &&
                latest_image_delete_status.state ==
                    SD_IMAGE_DELETE_STATE_IDLE &&
                app_image_delete_ui_begin(&image_delete_ui)) {
                memcpy(image_delete_target,
                       latest_sd_image_status.filename,
                       sizeof(image_delete_target));
                image_delete_target[
                    sizeof(image_delete_target) - 1U] = '\0';
                image_delete_target_snapshot.ready = true;
                image_delete_target_snapshot.revision =
                    latest_sd_image_status.revision;
                image_delete_target_snapshot.selected_index =
                    latest_sd_image_status.selected_index;
                image_delete_target_snapshot.image_count =
                    latest_sd_image_status.image_count;
                image_delete_error = ESP_OK;
                image_delete_ui_transitioned = true;
                ESP_LOGI(TAG,
                         "KEY long press: review image delete %u/%u",
                         (unsigned)(
                             image_delete_target_snapshot.selected_index +
                             1U),
                         (unsigned)
                             image_delete_target_snapshot.image_count);
                render_requested = true;
            } else if (key_action == APP_PAGE_ACTION_SYNC_TIME &&
                manual_sync_ui != MANUAL_SYNC_UI_ACTIVE &&
                !online_update_busy) {
                manual_sync_error = network_time_request_sync();
                manual_sync_ui_started = now;
                previous_manual_sync_network_state =
                    NETWORK_TIME_STATE_UNINITIALIZED;
                if (manual_sync_error == ESP_OK) {
                    manual_sync_ui = MANUAL_SYNC_UI_ACTIVE;
                    ESP_LOGI(TAG,
                             "KEY long press: manual time sync started");
                } else {
                    manual_sync_ui = MANUAL_SYNC_UI_UNAVAILABLE;
                    ESP_LOGW(TAG,
                             "KEY long press: manual time sync unavailable: %s",
                             esp_err_to_name(manual_sync_error));
                }
                render_requested = true;
            } else if (key_action == APP_PAGE_ACTION_START_VOICE &&
                       manual_sync_ui == MANUAL_SYNC_UI_NONE &&
                       !firmware_update_ui_active &&
                       !gallery_download_ui_active &&
                       !online_update_busy &&
                       !online_update_confirmation_active &&
                       !app_image_delete_ui_is_active(
                           &image_delete_ui)) {
                audio_voice_get_status(&voice_status);
                audio_conversation_get_status(
                    &cloud_voice_status);
                const app_voice_backend_t backend =
                    select_voice_backend(
                        &cloud_config_status,
                        &cloud_voice_status, &voice_status,
                        &network_status,
                        power_runtime.effective_state ==
                            APP_POWER_STATE_NORMAL);
                const bool engine_available =
                    backend != APP_VOICE_BACKEND_UNAVAILABLE;
                uint32_t generation = 0U;
                if (voice_session_state_begin(
                        &voice_session, engine_available,
                        &generation)) {
                    cloud_session_selected =
                        backend == APP_VOICE_BACKEND_CLOUD;
                    pending_voice_action =
                        VOICE_COMMAND_ACTION_NONE;
                    voice_session_error =
                        engine_available ? ESP_OK
                                         : voice_status.last_error;
                    if (cloud_session_selected) {
                        const esp_err_t start_error =
                            audio_conversation_start(generation);
                        if (start_error == ESP_OK) {
                            audio_conversation_get_status(
                                &cloud_voice_status);
                            previous_cloud_voice_revision =
                                cloud_voice_status.revision;
                            previous_cloud_voice_state =
                                cloud_voice_status.state;
                            last_cloud_voice_ui_update = now;
                            ESP_LOGI(
                                TAG,
                                "KEY long press: preparing cloud voice generation=%u",
                                (unsigned)generation);
                        } else if (voice_engine_available(
                                       &voice_status)) {
                            cloud_session_selected = false;
                            ESP_LOGW(
                                TAG,
                                "cloud voice start unavailable; using offline voice: %s",
                                esp_err_to_name(start_error));
                        } else {
                            voice_session_error = start_error;
                            (void)voice_session_state_report_failure(
                                &voice_session, generation);
                            ESP_LOGW(
                                TAG,
                                "cloud voice could not start: %s",
                                esp_err_to_name(start_error));
                        }
                    } else if (engine_available) {
                        ESP_LOGI(TAG,
                                 "KEY long press: release to start offline voice generation=%u",
                                 (unsigned)generation);
                    } else {
                        ESP_LOGW(TAG,
                                 "offline voice unavailable: %s",
                                 voice_unavailable_detail(
                                     &voice_status));
                    }
                }
                previous_display_mode = APP_DISPLAY_NONE;
                render_requested = true;
            } else if (key_action ==
                           APP_PAGE_ACTION_CHECK_ONLINE_UPDATE &&
                       manual_sync_ui == MANUAL_SYNC_UI_NONE &&
                       !firmware_update_ui_active) {
                esp_err_t action_error = ESP_ERR_INVALID_STATE;
                if (online_update_status.state ==
                    ONLINE_UPDATE_STATE_AVAILABLE) {
                    action_error =
                        online_firmware_update_request_confirmation();
                    ESP_LOGI(TAG,
                             "KEY long press: reviewing online update");
                } else if (online_update_status.state ==
                           ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION) {
                    action_error =
                        online_firmware_update_start_install();
                    ESP_LOGI(TAG,
                             "KEY long press: online update confirmed");
                } else if (!online_update_busy) {
                    action_error =
                        online_firmware_update_request_check();
                    ESP_LOGI(TAG,
                             "KEY long press: checking online update");
                }
                if (action_error != ESP_OK) {
                    ESP_LOGW(TAG, "online update action unavailable: %s",
                             esp_err_to_name(action_error));
                }
                (void)online_firmware_update_get_status(
                    &online_update_status);
                previous_online_update_state =
                    online_update_status.state;
                previous_online_update_percent =
                    online_update_status.percent;
                render_requested = true;
            } else if (key_action == APP_PAGE_ACTION_OPEN_SETTINGS &&
                       manual_sync_ui == MANUAL_SYNC_UI_NONE &&
                       !firmware_update_ui_active &&
                       !online_update_busy &&
                       !online_update_confirmation_active &&
                       !boot_pressed_or_debounced) {
                const esp_err_t start_error = firmware_update_start();
                if (start_error != ESP_OK) {
                    ESP_LOGW(TAG, "could not open settings portal: %s",
                             esp_err_to_name(start_error));
                } else {
                    ESP_LOGI(TAG,
                             "KEY long press: settings portal started");
                }
                (void)firmware_update_get_status(&firmware_update_status);
                firmware_update_ui_active =
                    firmware_update_status.state !=
                    FIRMWARE_UPDATE_STATE_IDLE;
                previous_update_state = firmware_update_status.state;
                previous_update_percent = firmware_update_status.percent;
                render_requested = true;
            }
        }

        if (boot_event == BUTTON_EVENT_HOLD_CANCELLED) {
            app_page_state_note_activity(&page_state);
            render_requested = true;
            if (input_page == APP_PAGE_SETTINGS) {
                ESP_LOGI(TAG,
                         "BOOT hold cancelled; manual saving unchanged");
            } else {
                ESP_LOGI(TAG,
                         "BOOT hold released without an action");
            }
        } else if (boot_event == BUTTON_EVENT_LONG_PRESS) {
            app_page_state_note_activity(&page_state);
            const app_page_action_t boot_action =
                app_page_boot_hold_action(input_page);
            if (boot_action ==
                    APP_PAGE_ACTION_TOGGLE_MANUAL_SAVING &&
                manual_sync_ui == MANUAL_SYNC_UI_NONE &&
                !firmware_update_ui_active &&
                !gallery_download_ui_active && !online_update_busy &&
                !online_update_confirmation_active &&
                !key_pressed_or_debounced) {
                bool next_manual_saving = false;
                const esp_err_t save_error =
                    app_settings_toggle_manual_saving(
                        &next_manual_saving);

                if (save_error == ESP_OK) {
                    power_setting_target = next_manual_saving;
                    power_setting_apply_pending = true;
                    power_setting_ui = POWER_SETTING_UI_APPLYING;
                    power_setting_result_timer_started = false;
                    settings_refresh_requested = true;
                    if (next_manual_saving) {
                        automatic_update_check_pending = false;
                    }
                    ESP_LOGI(TAG,
                             "BOOT long press: manual saving saved as %s",
                             next_manual_saving ? "on" : "off");
                } else {
                    power_setting_ui = POWER_SETTING_UI_FAILED;
                    power_setting_result_timer_started = false;
                    ESP_LOGW(TAG,
                             "BOOT long press: manual saving unchanged: %s",
                             esp_err_to_name(save_error));
                }
                render_requested = true;
            }
        } else if (boot_event == BUTTON_EVENT_SHORT_PRESS) {
            if (app_image_delete_ui_cancel(&image_delete_ui)) {
                image_delete_wait_for_button_release = true;
                memset(image_delete_target, 0,
                       sizeof(image_delete_target));
                image_delete_target_snapshot.ready = false;
                ESP_LOGI(TAG,
                         "BOOT short press: image delete cancelled");
                render_requested = true;
            } else if (firmware_update_status.state ==
                    FIRMWARE_UPDATE_STATE_STARTING ||
                firmware_update_status.state ==
                    FIRMWARE_UPDATE_STATE_READY) {
                const esp_err_t cancel_error = firmware_update_cancel();
                if (cancel_error == ESP_OK) {
                    ESP_LOGI(TAG, "BOOT short press: closing update mode");
                }
                render_requested = true;
            } else if (firmware_update_ui_active) {
                ESP_LOGI(TAG,
                         "BOOT short press ignored while firmware update is active");
            } else if (input_page == APP_PAGE_ONLINE_UPDATE &&
                       (online_update_status.state ==
                            ONLINE_UPDATE_STATE_CHECKING ||
                        online_update_status.state ==
                            ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION ||
                        online_update_status.state ==
                            ONLINE_UPDATE_STATE_CONNECTING)) {
                const esp_err_t cancel_error =
                    online_firmware_update_cancel();
                if (cancel_error == ESP_OK) {
                    ESP_LOGI(TAG,
                             "BOOT short press: cancelling online update action");
                }
                render_requested = true;
            } else if (input_page == APP_PAGE_ONLINE_UPDATE &&
                       online_update_busy) {
                ESP_LOGI(TAG,
                         "BOOT short press ignored while online update is active");
            } else if (manual_sync_ui == MANUAL_SYNC_UI_ACTIVE) {
                ESP_LOGI(TAG, "BOOT short press ignored while time sync is active");
            } else if (previous_display_mode == APP_DISPLAY_NETWORK_SETUP) {
                setup_screen_dismissed = true;
                app_page_state_go_home(&page_state);
                render_requested = true;
                ESP_LOGI(TAG,
                         "BOOT short press: continuing with offline dashboard");
            } else {
                manual_sync_ui = MANUAL_SYNC_UI_NONE;
                app_page_state_boot_short_press(&page_state);
                render_requested = true;
                ESP_LOGI(TAG, "BOOT short press: showing %s page",
                         page_name(app_page_state_current(&page_state)));
            }
        }

        const app_hold_prompt_context_t hold_prompt_context = {
            .buttons_ready = buttons_ready,
            .alarm_input_blocked =
                alarm_button_events_suppressed || alarm_modal_active,
            .dual_button_release_gate = dual_button_release_gate,
            .voice_button_release_gate = voice_button_release_gate,
            .image_delete_release_gate =
                image_delete_wait_for_button_release,
            .network_setup_visible =
                previous_display_mode == APP_DISPLAY_NETWORK_SETUP,
            .power_setting_ui_active =
                power_setting_ui != POWER_SETTING_UI_NONE,
            .firmware_update_ui_active = firmware_update_ui_active,
            .gallery_download_ui_active = gallery_download_ui_active,
            .online_update_busy = online_update_busy,
            .image_delete_ui_active =
                app_image_delete_ui_is_active(&image_delete_ui),
            .voice_session_active =
                voice_session_state_is_active(&voice_session),
            .manual_sync_idle =
                manual_sync_ui == MANUAL_SYNC_UI_NONE,
            .manual_sync_active =
                manual_sync_ui == MANUAL_SYNC_UI_ACTIVE,
            .online_update_confirmation_active =
                online_update_confirmation_active,
            .image_delete_available =
                latest_sd_image_status.state == SD_IMAGE_STATE_READY &&
                latest_sd_image_status.image_count > 0U &&
                latest_sd_image_status.filename[0] != '\0' &&
                latest_image_delete_status.state ==
                    SD_IMAGE_DELETE_STATE_IDLE,
        };
        const app_page_action_t key_hold_action =
            app_page_key_hold_action(input_page);
        const app_page_action_t boot_hold_action =
            app_page_boot_hold_action(input_page);
        const bool key_hold_prompt_active =
            app_hold_prompt_allowed(
                &hold_prompt_context, key_hold_action,
                boot_pressed_or_debounced) &&
            button_state_hold_prompt_active(&key_button_state);
        const bool boot_hold_prompt_active =
            app_hold_prompt_allowed(
                &hold_prompt_context, boot_hold_action,
                key_pressed_or_debounced) &&
            button_state_hold_prompt_active(&boot_button_state);
        const bool hold_prompt_active =
            key_hold_prompt_active || boot_hold_prompt_active;
        const app_page_action_t active_hold_action =
            boot_hold_prompt_active ? boot_hold_action : key_hold_action;
        const uint8_t hold_seconds_remaining = (uint8_t)(
            boot_hold_prompt_active
                ? button_state_hold_seconds_remaining(
                      &boot_button_state)
                : button_state_hold_seconds_remaining(
                      &key_button_state));
        const app_hold_update_intent_t hold_update_intent =
            online_update_status.state ==
                    ONLINE_UPDATE_STATE_AWAITING_CONFIRMATION
                ? APP_HOLD_UPDATE_INSTALL
                : online_update_status.state ==
                          ONLINE_UPDATE_STATE_AVAILABLE
                      ? APP_HOLD_UPDATE_REVIEW
                      : APP_HOLD_UPDATE_CHECK;
        const char *hold_title = app_hold_prompt_title(
            active_hold_action, hold_update_intent,
            settings.manual_saving_requested);

        const bool button_interaction_active =
            key_pressed || boot_pressed ||
            button_state_is_pressed(&key_button_state) ||
            button_state_is_pressed(&boot_button_state);
        if (button_interaction_active) {
            app_page_state_note_activity(&page_state);
        }

        if (manual_sync_ui != MANUAL_SYNC_UI_NONE &&
            manual_sync_ui != MANUAL_SYNC_UI_ACTIVE &&
            now - manual_sync_ui_started >=
                pdMS_TO_TICKS(APP_MANUAL_SYNC_RESULT_MS)) {
            manual_sync_ui = MANUAL_SYNC_UI_NONE;
            render_requested = true;
        }
        if (power_setting_ui != POWER_SETTING_UI_NONE &&
            power_setting_ui != POWER_SETTING_UI_APPLYING &&
            !display_ready &&
            !power_setting_result_timer_started) {
            /* A failed display must not turn a reversible setting into a
             * permanent input lock. */
            power_setting_ui_started = now;
            power_setting_result_timer_started = true;
        }
        if (power_setting_ui != POWER_SETTING_UI_NONE &&
            power_setting_ui != POWER_SETTING_UI_APPLYING &&
            power_setting_result_timer_started &&
            now - power_setting_ui_started >=
                pdMS_TO_TICKS(APP_POWER_SETTING_RESULT_MS) &&
            !key_pressed_or_debounced &&
            !boot_pressed_or_debounced) {
            /* Do not let a press started on the result screen continue into
             * the restored SETTINGS page and trigger another action. */
            power_setting_ui = POWER_SETTING_UI_NONE;
            power_setting_result_timer_started = false;
            app_page_state_note_activity(&page_state);
            render_requested = true;
        }
        const app_image_delete_ui_state_t image_delete_state_before_tick =
            app_image_delete_ui_state(&image_delete_ui);
        if (!alarm_modal_active && !image_delete_ui_transitioned &&
            app_image_delete_ui_tick(&image_delete_ui,
                                     button_elapsed_ms)) {
            image_delete_wait_for_button_release = true;
            if (image_delete_state_before_tick ==
                    APP_IMAGE_DELETE_UI_SUCCESS ||
                image_delete_state_before_tick ==
                    APP_IMAGE_DELETE_UI_FAILED) {
                (void)sd_image_store_dismiss_delete_result();
                ESP_LOGI(TAG, "image delete result dismissed");
            } else {
                ESP_LOGI(TAG, "image delete confirmation timed out");
            }
            memset(image_delete_target, 0,
                   sizeof(image_delete_target));
            image_delete_target_snapshot.ready = false;
            render_requested = true;
        }
        const voice_session_phase_t voice_phase_before_tick =
            voice_session_state_phase(&voice_session);
        if (!alarm_modal_active &&
            voice_phase_before_tick !=
                VOICE_SESSION_PHASE_LISTENING &&
            voice_phase_before_tick !=
                VOICE_SESSION_PHASE_RECOGNIZING) {
            const voice_session_action_t voice_tick_action =
                voice_session_state_tick(&voice_session,
                                         button_elapsed_ms);
            if (voice_tick_action ==
                VOICE_SESSION_ACTION_CANCEL_AND_CLEAR) {
                (void)cancel_voice_backend(
                    cloud_session_selected);
                pending_voice_action = VOICE_COMMAND_ACTION_NONE;
                render_requested = true;
            } else if (voice_tick_action ==
                       VOICE_SESSION_ACTION_COMPLETE_COMMAND) {
                voice_button_release_gate =
                    key_pressed_or_debounced ||
                    boot_pressed_or_debounced;
                if (cloud_session_selected) {
                    const esp_err_t dismiss_error =
                        audio_conversation_dismiss();
                    cloud_session_selected = false;
                    if (dismiss_error == ESP_OK) {
                        ESP_LOGI(TAG,
                                 "cloud conversation feedback dismissed");
                    } else {
                        ESP_LOGI(TAG,
                                 "cloud conversation cleanup still in progress");
                    }
                } else {
                    const app_page_t target_page =
                        voice_command_page(pending_voice_action);
                    if (app_page_state_open_page(&page_state,
                                                 target_page)) {
                        if (target_page == APP_PAGE_STATUS) {
                            status_refresh_pending = true;
                        }
                        ESP_LOGI(TAG,
                                 "offline voice opened %s page",
                                 page_name(target_page));
                    }
                }
                pending_voice_action = VOICE_COMMAND_ACTION_NONE;
                voice_session_error = ESP_OK;
                render_requested = true;
            } else if (voice_tick_action ==
                       VOICE_SESSION_ACTION_RETURN_TO_READY) {
                voice_button_release_gate =
                    key_pressed_or_debounced ||
                    boot_pressed_or_debounced;
                if (cloud_session_selected) {
                    const esp_err_t dismiss_error =
                        audio_conversation_dismiss();
                    cloud_session_selected = false;
                    if (dismiss_error != ESP_OK) {
                        ESP_LOGI(TAG,
                                 "cloud conversation cleanup still in progress");
                    }
                }
                pending_voice_action = VOICE_COMMAND_ACTION_NONE;
                voice_session_error = ESP_OK;
                render_requested = true;
            }
        }
        if (manual_sync_ui == MANUAL_SYNC_UI_NONE &&
            !hold_prompt_active &&
            !app_image_delete_ui_is_active(&image_delete_ui) &&
            power_setting_ui == POWER_SETTING_UI_NONE &&
            !firmware_update_ui_active &&
            !gallery_download_ui_active &&
            !(input_page == APP_PAGE_ONLINE_UPDATE &&
              (online_update_busy ||
               online_update_confirmation_active)) &&
            !alarm_button_events_suppressed && !alarm_modal_active &&
            !voice_session_state_is_active(&voice_session) &&
            !button_interaction_active &&
            app_page_state_tick(&page_state, button_elapsed_ms)) {
            render_requested = true;
            ESP_LOGI(TAG, "page timeout: returning home");
        }

        if (running_image_confirmation_pending &&
            now - initial_tick >= pdMS_TO_TICKS(APP_OTA_CONFIRM_DELAY_MS)) {
            const esp_err_t confirm_error =
                firmware_update_confirm_running_image();
            if (confirm_error == ESP_OK) {
                ESP_LOGI(TAG,
                         "running OTA image confirmed after stable startup");
            } else {
                ESP_LOGE(TAG, "could not confirm running OTA image: %s",
                         esp_err_to_name(confirm_error));
            }
            running_image_confirmation_pending = false;
        }

        if (periodic_update) {
            const bool first_update = first_periodic_update;
            first_periodic_update = false;
            last_periodic_update = now;
            const bool status_page_needs_fresh_data =
                app_page_state_current(&page_state) == APP_PAGE_STATUS &&
                status_refresh_pending;
            const bool rtc_read_due =
                first_update || runtime_settings_changed ||
                status_page_needs_fresh_data ||
                now - last_rtc_read >=
                    pdMS_TO_TICKS(rtc_read_wait_ms);
            const bool sensor_read_due =
                first_update || runtime_settings_changed ||
                status_page_needs_fresh_data ||
                now - last_sensor_read >=
                    pdMS_TO_TICKS(power_policy.sensor_read_interval_ms);
            const bool battery_read_due =
                first_update || runtime_settings_changed ||
                battery_read_pending ||
                status_page_needs_fresh_data ||
                now - last_battery_read >=
                    pdMS_TO_TICKS(power_policy.battery_read_interval_ms);

            if (rtc_driver_ready && rtc_read_due) {
                last_rtc_read = now;
                const esp_err_t error =
                    rtc_alarm_sample_attempted
                        ? rtc_alarm_sample_error
                        : pcf85063_read(&datetime);
                if (error == ESP_OK) {
                    rtc_read_wait_ms =
                        app_power_policy_next_clock_delay_ms(
                            &power_policy, datetime.second);
                    if (settings.alarm_enabled &&
                        rtc_read_wait_ms > APP_PERIODIC_UPDATE_MS) {
                        rtc_read_wait_ms = APP_PERIODIC_UPDATE_MS;
                    }
                    const bool rtc_display_changed =
                        dashboard.time_valid != datetime.clock_integrity ||
                        (datetime.clock_integrity &&
                         (dashboard.year != datetime.year ||
                          dashboard.month != datetime.month ||
                          dashboard.day != datetime.day ||
                          dashboard.weekday != datetime.weekday ||
                          dashboard.hour != datetime.hour ||
                          dashboard.minute != datetime.minute ||
                          (dashboard.show_seconds &&
                           dashboard.second != datetime.second)));
                    dashboard.time_valid = datetime.clock_integrity;
                    dashboard.year = datetime.year;
                    dashboard.month = datetime.month;
                    dashboard.day = datetime.day;
                    dashboard.weekday = datetime.weekday;
                    dashboard.hour = datetime.hour;
                    dashboard.minute = datetime.minute;
                    dashboard.second = datetime.second;
                    dashboard.lunar_valid =
                        datetime.clock_integrity &&
                        chinese_lunar_from_gregorian(
                            datetime.year, datetime.month, datetime.day,
                            &lunar_date) &&
                        chinese_lunar_format(&lunar_date, lunar_text,
                                             sizeof(lunar_text));
                    dashboard.lunar_text =
                        dashboard.lunar_valid ? lunar_text : NULL;
                    if (rtc_backup_monitor_ready &&
                        datetime.clock_integrity) {
                        const esp_err_t arm_error =
                            rtc_backup_monitor_arm(&datetime);
                        if (arm_error != ESP_OK && (cycle % 10U) == 0U) {
                            ESP_LOGW(TAG,
                                     "could not arm RTC backup monitor: %s",
                                     esp_err_to_name(arm_error));
                        }
                    }
                    if (rtc_display_changed) {
                        dashboard_data_changed = true;
                        calendar_data_changed = true;
                        system_status_data_changed = true;
                    }
                } else {
                    rtc_read_wait_ms = power_policy.rtc_read_interval_ms;
                    if (settings.alarm_enabled &&
                        rtc_read_wait_ms > APP_PERIODIC_UPDATE_MS) {
                        rtc_read_wait_ms = APP_PERIODIC_UPDATE_MS;
                    }
                    const bool rtc_display_changed = dashboard.time_valid;
                    dashboard.time_valid = false;
                    dashboard.lunar_valid = false;
                    dashboard.lunar_text = NULL;
                    if ((cycle % 10U) == 0U) {
                        ESP_LOGW(TAG, "RTC read failed: %s",
                                 esp_err_to_name(error));
                    }
                    if (rtc_display_changed) {
                        dashboard_data_changed = true;
                        calendar_data_changed = true;
                        system_status_data_changed = true;
                    }
                }
            }

            if (sensor_driver_ready && sensor_read_due &&
                !firmware_update_ui_active &&
                !gallery_download_ui_active) {
                last_sensor_read = now;
                const uint32_t sensor_timestamp_ms =
                    (uint32_t)((uint64_t)now * portTICK_PERIOD_MS);
                const esp_err_t error = shtc3_read(&measurement);
                if (error == ESP_OK) {
                    const bool had_measurement =
                        environment_observation.has_success;
                    const uint8_t recovered_failure_count =
                        environment_observation.consecutive_failures;
                    const bool was_stale = environment_stale;
                    environment_observation_record_success(
                        &environment_observation, sensor_timestamp_ms);
                    const environment_comfort_level_t comfort =
                        environment_comfort_update(
                            &comfort_tracker,
                            measurement.temperature_c,
                            measurement.humidity_percent,
                            sensor_timestamp_ms);
                    float displayed_temperature_c =
                        measurement.temperature_c;
                    float filtered_humidity_percent =
                        measurement.humidity_percent;
                    (void)environment_comfort_filtered_measurement(
                        &comfort_tracker, &displayed_temperature_c,
                        &filtered_humidity_percent);
                    const display_environment_comfort_t display_comfort =
                        dashboard_environment_comfort(comfort);
                    const bool sensor_display_changed =
                        !dashboard.environment_valid ||
                        environment_display_values_differ(
                            dashboard.temperature_c,
                            dashboard.humidity_percent,
                            displayed_temperature_c,
                            filtered_humidity_percent,
                            dashboard.temperature_fahrenheit) ||
                        dashboard.environment_comfort != display_comfort;
                    dashboard.environment_valid = true;
                    dashboard.temperature_c = displayed_temperature_c;
                    dashboard.humidity_percent =
                        filtered_humidity_percent;
                    dashboard.environment_comfort = display_comfort;
                    environment_stale = false;
                    if (!had_measurement || (cycle % 300U) == 0U) {
                        if (dashboard.time_valid) {
                            ESP_LOGI(
                                TAG,
                                "RTC %04u-%02u-%02u %02u:%02u:%02u, temp %.1f C, humidity %.1f %%",
                                datetime.year, datetime.month,
                                datetime.day, datetime.hour,
                                datetime.minute, datetime.second,
                                (double)displayed_temperature_c,
                                (double)filtered_humidity_percent);
                        } else {
                            ESP_LOGI(
                                TAG,
                                "RTC time invalid, temp %.1f C, humidity %.1f %%",
                                (double)displayed_temperature_c,
                                (double)filtered_humidity_percent);
                        }
                    }
                    if (recovered_failure_count > 0U) {
                        ESP_LOGI(
                            TAG,
                            "SHTC3 recovered after %u failed sample%s; temp %.1f C, humidity %.1f %%",
                            recovered_failure_count,
                            recovered_failure_count == 1U ? "" : "s",
                            (double)displayed_temperature_c,
                            (double)filtered_humidity_percent);
                    }
                    system_status_data_changed |=
                        sensor_display_changed || was_stale;
                    dashboard_data_changed |= sensor_display_changed;
                } else {
                    const bool was_valid = dashboard.environment_valid;
                    const bool was_stale = environment_stale;
                    environment_observation_record_failure(
                        &environment_observation);
                    const environment_observation_status_t observation =
                        environment_observation_evaluate(
                            &environment_observation,
                            sensor_timestamp_ms);
                    environment_comfort_mark_invalid(&comfort_tracker);
                    environment_stale = observation.stale;
                    if (!observation.display_valid) {
                        dashboard.environment_valid = false;
                        dashboard.environment_comfort =
                            DISPLAY_ENVIRONMENT_COMFORT_UNKNOWN;
                        if (was_valid) {
                            environment_comfort_reset(&comfort_tracker);
                            dashboard_data_changed = true;
                        }
                    }
                    if (was_stale != environment_stale ||
                        was_valid != dashboard.environment_valid) {
                        system_status_data_changed = true;
                    }
                    if (observation.consecutive_failures == 1U) {
                        if (observation.display_valid) {
                            ESP_LOGW(
                                TAG,
                                "SHTC3 read failed: %s; retaining last valid sample (%u ms old)",
                                esp_err_to_name(error),
                                (unsigned)observation.age_ms);
                        } else {
                            ESP_LOGW(TAG,
                                     "SHTC3 read failed: %s; no current valid sample",
                                     esp_err_to_name(error));
                        }
                    } else if (was_valid &&
                               !dashboard.environment_valid) {
                        ESP_LOGW(
                            TAG,
                            "SHTC3 last valid sample expired after %u ms",
                            (unsigned)observation.age_ms);
                    }
                }
            }

            if (battery_driver_ready && battery_read_due) {
                last_battery_read = now;
                const uint16_t previous_voltage_mv =
                    battery_measurement.voltage_mv;
                const esp_err_t error = battery_read(&battery_measurement);
                if (error == ESP_OK) {
                    battery_read_pending = false;
                    const bool battery_display_changed =
                        !dashboard.battery_valid ||
                        dashboard.battery_percent !=
                            battery_measurement.percent ||
                        previous_voltage_mv !=
                            battery_measurement.voltage_mv;
                    dashboard.battery_valid = true;
                    dashboard.battery_percent = battery_measurement.percent;
                    if ((cycle % 300U) == 0U) {
                        ESP_LOGI(TAG, "battery %u mV, %u%% (%s)",
                                 battery_measurement.voltage_mv,
                                 battery_measurement.percent,
                                 battery_measurement.calibrated ? "calibrated"
                                                                : "nominal");
                    }
                    system_status_data_changed |= battery_display_changed;
                    dashboard_data_changed |= battery_display_changed;

                    app_power_runtime_t next_power_runtime =
                        power_runtime;
                    bool effective_mode_changed = false;
                    if (!app_power_runtime_observe_battery(
                            &next_power_runtime, true,
                            battery_measurement.percent,
                            &effective_mode_changed)) {
                        ESP_LOGW(TAG,
                                 "could not update automatic power policy");
                    } else if (!effective_mode_changed) {
                        power_runtime = next_power_runtime;
                    } else {
                        app_power_policy_t next_power_policy = {0};
                        if (app_power_policy_for_runtime(
                                &next_power_runtime,
                                &next_power_policy)) {
                            const esp_err_t transition_error =
                                apply_runtime_power_transition(
                                    &power_runtime, &power_policy,
                                    &next_power_runtime,
                                    &next_power_policy,
                                    network_error == ESP_OK);
                            if (transition_error != ESP_OK) {
                                ESP_LOGW(
                                    TAG,
                                    "automatic power transition pending: %s",
                                    esp_err_to_name(transition_error));
                            } else {
                                power_runtime = next_power_runtime;
                                power_policy = next_power_policy;
                                if (power_runtime.effective_state ==
                                    APP_POWER_STATE_SAVING) {
                                    automatic_update_check_pending =
                                        false;
                                }
                                dashboard.show_seconds =
                                    power_policy.show_seconds;
                                rtc_read_wait_ms = 0U;
                                dashboard_data_changed = true;
                                calendar_data_changed = true;
                                system_status_data_changed = true;
                                render_requested = true;
                                ESP_LOGI(
                                    TAG,
                                    "automatic low-battery state switched to %s at %u%%",
                                    power_runtime.effective_state ==
                                            APP_POWER_STATE_SAVING
                                        ? "saving"
                                        : "normal",
                                    battery_measurement.percent);
                            }
                        }
                    }
                } else {
                    dashboard_data_changed |= dashboard.battery_valid;
                    system_status_data_changed |= dashboard.battery_valid;
                    dashboard.battery_valid = false;
                    bool ignored_mode_change = false;
                    (void)app_power_runtime_observe_battery(
                        &power_runtime, false, 0U,
                        &ignored_mode_change);
                    if ((cycle % 300U) == 0U) {
                        ESP_LOGW(TAG, "battery read failed: %s",
                                 esp_err_to_name(error));
                    }
                }
            }

            const bool previous_network_configured =
                network_status.configured;
            const bool previous_station_connected =
                network_status.station_connected;
            (void)network_time_get_status(&network_status);
            dashboard.network_state = dashboard_network_state(&network_status);
            if (network_status.state != previous_network_state ||
                network_status.configured != previous_network_configured ||
                network_status.station_connected !=
                    previous_station_connected) {
                if (network_status.state == NETWORK_TIME_STATE_PROVISIONING) {
                    provisioning_started = now;
                    setup_screen_dismissed = false;
                }
                previous_network_state = network_status.state;
                dashboard_data_changed = true;
                system_status_data_changed = true;
                render_requested = true;
            }
            if (status_page_needs_fresh_data) {
                status_refresh_pending = false;
            }
            if (manual_sync_ui == MANUAL_SYNC_UI_ACTIVE &&
                (network_status.state == NETWORK_TIME_STATE_RETRY_WAIT ||
                 network_status.state == NETWORK_TIME_STATE_ERROR ||
                 (network_status.state == NETWORK_TIME_STATE_PROVISIONING &&
                  network_status.configured))) {
                manual_sync_error = network_status.last_error;
                manual_sync_ui = MANUAL_SYNC_UI_FAILED;
                manual_sync_ui_started = now;
                render_requested = true;
            }
            ++cycle;
        }

        if (automatic_update_check_pending &&
            power_policy.automatic_network &&
            online_update_error == ESP_OK &&
            network_status.state == NETWORK_TIME_STATE_SYNCHRONIZED &&
            !firmware_update_ui_active && !gallery_download_ui_active &&
            !app_image_delete_ui_is_active(&image_delete_ui) &&
            !voice_session_state_is_active(&voice_session) &&
            !online_update_busy &&
            !online_update_confirmation_active) {
            if (online_update_status.state == ONLINE_UPDATE_STATE_AVAILABLE) {
                automatic_update_check_pending = false;
            } else {
                const esp_err_t check_error =
                    online_firmware_update_request_check();
                if (check_error == ESP_OK) {
                    automatic_update_check_pending = false;
                    ESP_LOGI(TAG,
                             "automatic online update check requested");
                } else if (check_error != ESP_ERR_INVALID_STATE) {
                    automatic_update_check_pending = false;
                    ESP_LOGW(TAG,
                             "automatic online update check unavailable: %s",
                             esp_err_to_name(check_error));
                }
            }
        }

        const app_page_t active_page = app_page_state_current(&page_state);
        if (display_ready && alarm_modal_active) {
            if (render_requested ||
                previous_display_mode != APP_DISPLAY_ALARM) {
                const display_alarm_status_t alarm_status = {
                    .hour = dashboard.hour,
                    .minute = dashboard.minute,
                    .snooze_available = alarm_result.snooze_available,
                };
                display_show_alarm(&alarm_status);
            }
            previous_display_mode = APP_DISPLAY_ALARM;
            previous_portal_seconds = 0U;
        } else if (display_ready &&
                   power_setting_ui != POWER_SETTING_UI_NONE) {
            if (render_requested ||
                previous_display_mode !=
                    APP_DISPLAY_SETTINGS_POWER_RESULT) {
                if (power_setting_ui == POWER_SETTING_UI_APPLYING) {
                    display_show_status(
                        power_setting_target
                            ? "MANUAL SAVING ON"
                            : "MANUAL SAVING OFF",
                        "Applying setting");
                } else if (power_setting_ui ==
                           POWER_SETTING_UI_SAVED) {
                    display_show_status(
                        power_setting_target
                            ? "MANUAL SAVING ON"
                            : "MANUAL SAVING OFF",
                        power_runtime.usb_data_host_connected &&
                                power_setting_target
                            ? "Saved; active after USB"
                            : "Setting saved");
                } else if (power_setting_ui ==
                           POWER_SETTING_UI_PENDING) {
                    display_show_status("SAVING SETTING",
                                        "Apply pending; retrying");
                } else if (power_setting_ui ==
                           POWER_SETTING_UI_SUPERSEDED) {
                    display_show_status("POWER CHANGED",
                                        "Newer settings kept");
                } else {
                    display_show_status("POWER NOT SAVED",
                                        "Settings unchanged");
                }
            }
            if (power_setting_ui != POWER_SETTING_UI_APPLYING &&
                !power_setting_result_timer_started) {
                /* Start the result interval only after the first completed
                 * draw, so NVS and display work cannot shorten it. */
                power_setting_ui_started = xTaskGetTickCount();
                power_setting_result_timer_started = true;
            }
            previous_display_mode = APP_DISPLAY_SETTINGS_POWER_RESULT;
            previous_portal_seconds = 0U;
        } else if (display_ready &&
                   app_image_delete_ui_is_active(&image_delete_ui)) {
            const app_image_delete_ui_state_t image_delete_state =
                app_image_delete_ui_state(&image_delete_ui);
            if (image_delete_state ==
                APP_IMAGE_DELETE_UI_CONFIRMING) {
                if (render_requested ||
                    previous_display_mode !=
                        APP_DISPLAY_IMAGE_DELETE_CONFIRMATION) {
                    const esp_err_t copy_error =
                        image_bitmap_snapshot != NULL
                            ? sd_image_store_copy_bitmap(
                                  image_delete_target,
                                  image_bitmap_snapshot,
                                  MONO_IMAGE_BITMAP_BYTES)
                            : ESP_ERR_NO_MEM;
                    if (copy_error == ESP_OK) {
                        display_show_image_delete_confirmation(
                            image_bitmap_snapshot,
                            image_delete_target_snapshot.selected_index,
                            image_delete_target_snapshot.image_count,
                            app_image_delete_ui_confirmation_armed(
                                &image_delete_ui));
                    } else {
                        (void)app_image_delete_ui_confirm(
                            &image_delete_ui);
                        (void)app_image_delete_ui_complete(
                            &image_delete_ui, false);
                        image_delete_error = copy_error;
                        display_show_status(
                            "DELETE FAILED",
                            image_delete_error_detail(copy_error));
                    }
                }
                previous_display_mode =
                    app_image_delete_ui_state(&image_delete_ui) ==
                            APP_IMAGE_DELETE_UI_CONFIRMING
                        ? APP_DISPLAY_IMAGE_DELETE_CONFIRMATION
                        : APP_DISPLAY_IMAGE_DELETE_STATUS;
            } else {
                if (render_requested ||
                    previous_display_mode !=
                        APP_DISPLAY_IMAGE_DELETE_STATUS) {
                    if (image_delete_state ==
                        APP_IMAGE_DELETE_UI_DELETING) {
                        display_show_image_delete_status(
                            DISPLAY_IMAGE_DELETE_DELETING);
                    } else if (image_delete_state ==
                               APP_IMAGE_DELETE_UI_SUCCESS) {
                        display_show_image_delete_status(
                            DISPLAY_IMAGE_DELETE_DELETED);
                    } else {
                        display_show_status(
                            "DELETE FAILED",
                            image_delete_error_detail(
                                image_delete_error));
                    }
                }
                previous_display_mode =
                    APP_DISPLAY_IMAGE_DELETE_STATUS;
            }
            previous_portal_seconds = 0U;
        } else if (display_ready && hold_prompt_active) {
            if (previous_display_mode != APP_DISPLAY_HOLD_PROMPT ||
                hold_seconds_remaining != previous_portal_seconds) {
                display_show_hold_prompt(hold_title,
                                         hold_seconds_remaining);
            }
            previous_display_mode = APP_DISPLAY_HOLD_PROMPT;
            previous_portal_seconds = hold_seconds_remaining;
        } else if (display_ready && gallery_download_ui_active) {
            if (render_requested ||
                previous_display_mode != APP_DISPLAY_GALLERY_DOWNLOAD) {
                if (gallery_status.state ==
                    GALLERY_DOWNLOAD_STATE_CONNECTING) {
                    display_show_status("INSTALL GALLERY",
                                        "Connecting to saved Wi-Fi");
                } else if (gallery_status.state ==
                           GALLERY_DOWNLOAD_STATE_FETCHING_CATALOG) {
                    display_show_status("INSTALL GALLERY",
                                        "Checking starter pack");
                } else if (gallery_status.state ==
                           GALLERY_DOWNLOAD_STATE_DOWNLOADING) {
                    char detail[64];
                    snprintf(detail, sizeof(detail),
                             "IMAGE %u/%u | %u%%",
                             (unsigned)gallery_status.image_index,
                             (unsigned)gallery_status.image_count,
                             gallery_status.percent);
                    display_show_status("DOWNLOADING IMAGES", detail);
                } else if (gallery_status.state ==
                           GALLERY_DOWNLOAD_STATE_VERIFYING) {
                    char detail[48];
                    snprintf(detail, sizeof(detail), "Checking image %u/%u",
                             (unsigned)gallery_status.image_index,
                             (unsigned)gallery_status.image_count);
                    display_show_status("VERIFYING IMAGE", detail);
                } else if (gallery_status.state ==
                           GALLERY_DOWNLOAD_STATE_SUCCESS) {
                    display_show_status("GALLERY READY",
                                        "Images are ready to view");
                } else {
                    display_show_status(
                        "GALLERY FAILED",
                        gallery_download_error_detail(
                            gallery_status.last_error));
                }
            }
            previous_display_mode = APP_DISPLAY_GALLERY_DOWNLOAD;
            previous_portal_seconds = 0U;
        } else if (display_ready && firmware_update_ui_active) {
            app_display_mode_t update_display_mode =
                APP_DISPLAY_FIRMWARE_UPDATE_RESULT;
            if (firmware_update_status.state ==
                FIRMWARE_UPDATE_STATE_STARTING) {
                update_display_mode =
                    APP_DISPLAY_FIRMWARE_UPDATE_STARTING;
            } else if (firmware_update_status.state ==
                       FIRMWARE_UPDATE_STATE_READY) {
                update_display_mode = APP_DISPLAY_FIRMWARE_UPDATE_READY;
            } else if (firmware_update_status.state ==
                       FIRMWARE_UPDATE_STATE_RECEIVING) {
                update_display_mode =
                    APP_DISPLAY_FIRMWARE_UPDATE_RECEIVING;
            } else if (firmware_update_status.state ==
                       FIRMWARE_UPDATE_STATE_VERIFYING) {
                update_display_mode =
                    APP_DISPLAY_FIRMWARE_UPDATE_VERIFYING;
            }

            if (render_requested ||
                previous_display_mode != update_display_mode) {
                if (firmware_update_status.state ==
                    FIRMWARE_UPDATE_STATE_STARTING) {
                    display_show_status("STARTING SETTINGS",
                                        "Opening temporary Wi-Fi");
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_READY) {
                    display_show_settings_portal_ready(
                        firmware_update_status.access_point_ssid,
                        firmware_update_status.access_point_password,
                        firmware_update_status.access_url);
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_WIFI_VALIDATING) {
                    display_show_status("TESTING WI-FI",
                                        "Old network remains saved");
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_WIFI_SAVED) {
                    display_show_status("WI-FI SAVED",
                                        "Connecting without restart");
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_RECEIVING) {
                    char detail[64];
                    snprintf(
                        detail, sizeof(detail), "%u%% | %u / %u KiB",
                        firmware_update_status.percent,
                        (unsigned)(firmware_update_status.received_bytes /
                                   1024U),
                        (unsigned)((firmware_update_status.total_bytes +
                                    1023U) /
                                   1024U));
                    display_show_status("RECEIVING UPDATE", detail);
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_VERIFYING) {
                    display_show_status("VERIFYING UPDATE",
                                        "Checking firmware image");
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_SUCCESS) {
                    display_show_status("UPDATE COMPLETE",
                                        "Restarting safely");
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_EXPIRED) {
                    display_show_status("SETTINGS CLOSED",
                                        "Session expired");
                } else if (firmware_update_status.state ==
                           FIRMWARE_UPDATE_STATE_CANCELLED) {
                    display_show_status("SETTINGS CLOSED",
                                        "No changes made");
                } else {
                    display_show_status(
                        "UPDATE FAILED",
                        firmware_update_error_detail(
                            firmware_update_status.last_error));
                }
            }
            previous_display_mode = update_display_mode;
            previous_portal_seconds = 0U;
        } else if (display_ready && manual_sync_ui != MANUAL_SYNC_UI_NONE) {
            if (manual_sync_ui == MANUAL_SYNC_UI_ACTIVE) {
                if (previous_display_mode != APP_DISPLAY_MANUAL_SYNC ||
                    render_requested ||
                    network_status.state !=
                        previous_manual_sync_network_state) {
                    display_show_status(
                        "SYNC TIME",
                        manual_sync_detail(network_status.state));
                }
                previous_manual_sync_network_state = network_status.state;
                previous_display_mode = APP_DISPLAY_MANUAL_SYNC;
            } else {
                if (previous_display_mode != APP_DISPLAY_MANUAL_SYNC_RESULT ||
                    render_requested) {
                    if (manual_sync_ui == MANUAL_SYNC_UI_SUCCESS) {
                        display_show_status("TIME SYNCED", "RTC updated");
                    } else if (manual_sync_ui ==
                               MANUAL_SYNC_UI_UNAVAILABLE) {
                        display_show_status(
                            "SYNC UNAVAILABLE",
                            network_status.configured
                                ? "Network service is busy"
                                : "Configure Wi-Fi first");
                    } else {
                        display_show_status(
                            "SYNC FAILED",
                            !rtc_driver_ready
                                ? "RTC is not ready"
                                : esp_err_to_name(manual_sync_error));
                    }
                }
                previous_display_mode = APP_DISPLAY_MANUAL_SYNC_RESULT;
            }
            previous_portal_seconds = 0U;
        } else if (display_ready && app_page_is_system(active_page)) {
            app_display_mode_t system_display_mode = APP_DISPLAY_STATUS;
            if (active_page == APP_PAGE_VOICE) {
                system_display_mode = APP_DISPLAY_VOICE;
            } else if (active_page == APP_PAGE_SETTINGS) {
                system_display_mode = APP_DISPLAY_SETTINGS;
            } else if (active_page == APP_PAGE_ONLINE_UPDATE) {
                system_display_mode = APP_DISPLAY_ONLINE_UPDATE;
            }
            const bool system_data_changed =
                (active_page == APP_PAGE_STATUS &&
                 system_status_data_changed) ||
                (active_page == APP_PAGE_VOICE &&
                 voice_data_changed) ||
                (active_page == APP_PAGE_ONLINE_UPDATE &&
                 online_update_data_changed);
            if (render_requested ||
                previous_display_mode != system_display_mode ||
                system_data_changed) {
                const display_system_status_t system_status = {
                    .rtc_ready = rtc_driver_ready,
                    .time_valid = dashboard.time_valid,
                    .rtc_backup_state =
                        rtc_backup_monitor_ready
                            ? rtc_backup_status_name(
                                  rtc_backup_monitor_status())
                            : "NOT READY",
                    .sensor_ready = sensor_driver_ready,
                    .environment_valid = dashboard.environment_valid,
                    .environment_stale = environment_stale,
                    .temperature_c = dashboard.temperature_c,
                    .temperature_fahrenheit =
                        dashboard.temperature_fahrenheit,
                    .humidity_percent = dashboard.humidity_percent,
                    .battery_ready = battery_driver_ready,
                    .battery_valid = dashboard.battery_valid,
                    .battery_voltage_mv = battery_measurement.voltage_mv,
                    .battery_percent = dashboard.battery_percent,
                    .usb_data_host_connected =
                        dashboard.usb_data_host_connected,
                    .time_sync_state = device_time_sync_state_name(
                        network_error == ESP_OK, last_sync_valid,
                        &network_status),
                    .wifi_state = device_wifi_state_name(
                        network_error == ESP_OK, &network_status),
                    .last_sync_valid = last_sync_valid,
                    .last_sync_month = last_sync_time.month,
                    .last_sync_day = last_sync_time.day,
                    .last_sync_hour = last_sync_time.hour,
                    .last_sync_minute = last_sync_time.minute,
                };
                if (active_page == APP_PAGE_STATUS) {
                    display_show_system_status(&system_status);
                } else if (active_page == APP_PAGE_VOICE) {
                    audio_voice_get_status(&voice_status);
                    audio_conversation_get_status(
                        &cloud_voice_status);
                    const voice_session_phase_t voice_phase =
                        voice_session_state_phase(&voice_session);
                    const app_voice_backend_t idle_backend =
                        select_voice_backend(
                            &cloud_config_status,
                            &cloud_voice_status, &voice_status,
                            &network_status,
                            power_runtime.effective_state ==
                                APP_POWER_STATE_NORMAL);
                    const bool display_cloud_mode =
                        cloud_session_selected ||
                        (!voice_session_state_is_active(
                             &voice_session) &&
                         idle_backend == APP_VOICE_BACKEND_CLOUD);
                    const bool display_backend_available =
                        cloud_session_selected
                            ? cloud_audio_ready(&cloud_voice_status)
                            : idle_backend !=
                                  APP_VOICE_BACKEND_UNAVAILABLE;
                    const char *voice_detail = NULL;
                    if (!cloud_session_selected &&
                        cloud_voice_status.running) {
                        voice_detail = "Finishing cloud session";
                    } else if (cloud_session_selected &&
                        voice_phase == VOICE_SESSION_PHASE_FAILED) {
                        voice_detail = cloud_voice_error_detail(
                            &cloud_voice_status);
                    } else if (voice_phase ==
                        VOICE_SESSION_PHASE_SUCCEEDED) {
                        voice_detail = voice_command_detail(
                            pending_voice_action);
                    } else if (voice_phase ==
                               VOICE_SESSION_PHASE_TARGET_UNAVAILABLE) {
                        voice_detail = "No image available";
                    } else if (voice_phase ==
                               VOICE_SESSION_PHASE_ENGINE_UNAVAILABLE) {
                        voice_detail = voice_unavailable_detail(
                            &voice_status);
                    } else if (voice_phase ==
                               VOICE_SESSION_PHASE_FAILED) {
                        voice_detail =
                            voice_session_error == ESP_OK
                                ? "Recognition stopped unexpectedly"
                                : esp_err_to_name(
                                      voice_session_error);
                    }
                    const display_voice_status_t display_voice_status = {
                        .engine_available =
                            display_backend_available,
                        .cloud_mode = display_cloud_mode,
                        .session_active =
                            voice_session_state_is_active(
                                &voice_session),
                        .turn_number =
                            cloud_session_selected
                                ? cloud_voice_status.turn_number
                                : 0U,
                        .max_turns =
                            display_cloud_mode
                                ? cloud_voice_status.max_turns
                                : 0U,
                        .elapsed_ms =
                            cloud_session_selected
                                ? cloud_voice_status.elapsed_ms
                                : (voice_phase ==
                                        VOICE_SESSION_PHASE_LISTENING ||
                                    voice_phase ==
                                        VOICE_SESSION_PHASE_RECOGNIZING
                                       ? voice_status.elapsed_ms
                                       : voice_session.elapsed_ms),
                        .max_listening_ms =
                            display_cloud_mode
                                ? AUDIO_CONVERSATION_MAX_LISTENING_MS
                                : AUDIO_VOICE_MAX_LISTENING_MS,
                        .state = display_voice_state(
                            &voice_session, &voice_status,
                            cloud_session_selected, display_cloud_mode,
                            display_backend_available,
                            &cloud_voice_status),
                        .detail = voice_detail,
                        .transcript =
                            cloud_voice_status.transcript,
                        .response = cloud_voice_status.response,
                    };
                    display_show_voice(&display_voice_status);
                } else if (active_page == APP_PAGE_SETTINGS) {
                    const display_settings_status_t settings_status = {
                        .manual_saving_requested =
                            power_setting_apply_pending
                                ? power_setting_target
                                : settings.manual_saving_requested,
                        .automatic_saving_active =
                            power_runtime.automatic_saving_active,
                        .effective_low_power =
                            power_runtime.effective_state ==
                            APP_POWER_STATE_SAVING,
                        .usb_data_host_connected =
                            power_runtime.usb_data_host_connected,
                        .power_apply_pending =
                            power_setting_apply_pending,
                        .utc_offset_minutes = settings.utc_offset_minutes,
                        .temperature_fahrenheit =
                            settings.temperature_unit ==
                            APP_TEMPERATURE_UNIT_FAHRENHEIT,
                        .playback_volume_percent =
                            settings.audio_playback_volume,
                        .alarm_enabled = settings.alarm_enabled,
                        .alarm_hour = settings.alarm_hour,
                        .alarm_minute = settings.alarm_minute,
                        .alarm_weekdays = settings.alarm_weekdays,
                    };
                    display_show_settings(&settings_status);
                } else if (active_page == APP_PAGE_ONLINE_UPDATE) {
                    const char *detail = NULL;
                    if (online_update_status.state ==
                        ONLINE_UPDATE_STATE_CHECKING) {
                        detail = "Connecting to update service";
                    } else if (online_update_status.state ==
                               ONLINE_UPDATE_STATE_CONNECTING) {
                        detail = "Rechecking release details";
                    } else if (online_update_status.state ==
                               ONLINE_UPDATE_STATE_VERIFYING) {
                        detail = "Checking download and image";
                    } else if (online_update_status.state ==
                               ONLINE_UPDATE_STATE_SUCCESS) {
                        detail = "Restarting automatically";
                    } else if (online_update_status.state ==
                               ONLINE_UPDATE_STATE_FAILED) {
                        detail = !network_status.configured &&
                                         online_update_status.last_error ==
                                             ESP_ERR_INVALID_STATE
                                     ? "CONFIGURE WI-FI"
                                     : online_firmware_update_error_detail(
                                           &online_update_status);
                    }
                    const display_online_update_status_t display_status = {
                        .state = display_online_update_state(
                            online_update_status.state),
                        .beta_channel = online_update_status.beta_channel,
                        .current_version =
                            online_update_status.current_version,
                        .latest_version =
                            online_update_status.latest_version[0] != '\0'
                                ? online_update_status.latest_version
                                : NULL,
                        .last_checked =
                            online_update_status.last_checked[0] != '\0'
                                ? online_update_status.last_checked
                                : NULL,
                        .detail = detail,
                        .downloaded_bytes =
                            (uint32_t)online_update_status.downloaded_bytes,
                        .total_bytes =
                            (uint32_t)online_update_status.total_bytes,
                        .progress_percent = online_update_status.percent,
                    };
                    display_show_online_update(&display_status);
                }
            }
            previous_display_mode = system_display_mode;
            previous_portal_seconds = 0U;
        } else if (display_ready &&
                   (periodic_update || render_requested ||
                    previous_display_mode == APP_DISPLAY_STATUS ||
                    previous_display_mode == APP_DISPLAY_VOICE ||
                    previous_display_mode == APP_DISPLAY_SETTINGS ||
                    previous_display_mode == APP_DISPLAY_ALARM ||
                    previous_display_mode == APP_DISPLAY_ONLINE_UPDATE ||
                    previous_display_mode == APP_DISPLAY_MONOCHROME_IMAGE ||
                    previous_display_mode == APP_DISPLAY_HOLD_PROMPT ||
                    previous_display_mode ==
                        APP_DISPLAY_IMAGE_DELETE_CONFIRMATION ||
                    previous_display_mode ==
                        APP_DISPLAY_IMAGE_DELETE_STATUS ||
                    previous_display_mode == APP_DISPLAY_MANUAL_SYNC ||
                    previous_display_mode == APP_DISPLAY_MANUAL_SYNC_RESULT ||
                    previous_display_mode == APP_DISPLAY_GALLERY_DOWNLOAD)) {
            const TickType_t provisioning_elapsed = now - provisioning_started;
            const bool show_setup =
                app_network_setup_should_overlay(
                    network_status.state == NETWORK_TIME_STATE_PROVISIONING,
                    network_status.configured,
                    setup_screen_dismissed,
                    (uint32_t)provisioning_elapsed * portTICK_PERIOD_MS);
            app_display_mode_t display_mode = APP_DISPLAY_DASHBOARD;
            if (active_page == APP_PAGE_CALENDAR) {
                display_mode = APP_DISPLAY_CALENDAR;
            } else if (active_page == APP_PAGE_IMAGE) {
                display_mode = APP_DISPLAY_MONOCHROME_IMAGE;
            }
            if (show_setup) {
                display_mode = APP_DISPLAY_NETWORK_SETUP;
            }

            if (display_mode == APP_DISPLAY_DASHBOARD) {
                if (display_mode != previous_display_mode ||
                    render_requested || dashboard_data_changed) {
                    display_show_dashboard(&dashboard);
                }
            } else if (display_mode == APP_DISPLAY_CALENDAR) {
                if (display_mode != previous_display_mode ||
                    render_requested || calendar_data_changed) {
                    const bool image_page_available =
                        image_bitmap_snapshot != NULL &&
                        latest_sd_image_status.state ==
                            SD_IMAGE_STATE_READY;
                    display_show_calendar(&dashboard,
                                          image_page_available);
                }
            } else if (display_mode == APP_DISPLAY_MONOCHROME_IMAGE) {
                if (display_mode != previous_display_mode ||
                    render_requested) {
                    size_t selected_index = 0U;
                    size_t image_count = 0U;
                    if (image_bitmap_snapshot != NULL &&
                        sd_image_store_copy_selected(
                            image_bitmap_snapshot,
                            MONO_IMAGE_BITMAP_BYTES,
                            &selected_index, &image_count)) {
                        display_show_monochrome_image(
                            image_bitmap_snapshot, selected_index,
                            image_count);
                    } else {
                        app_page_state_set_image_available(&page_state,
                                                           false);
                        display_show_dashboard(&dashboard);
                        display_mode = APP_DISPLAY_DASHBOARD;
                    }
                }
            } else if (display_mode == previous_display_mode &&
                       !render_requested) {
                /* The setup page is static and needs no one-second redraw. */
            } else if (display_mode == APP_DISPLAY_NETWORK_SETUP) {
                display_show_network_setup(network_status.setup_ssid,
                                           network_status.setup_password,
                                           network_status.setup_url);
            }
            previous_display_mode = display_mode;
            previous_portal_seconds = 0U;
        }

        vTaskDelay(pdMS_TO_TICKS(APP_LOOP_INTERVAL_MS));
    }
}
