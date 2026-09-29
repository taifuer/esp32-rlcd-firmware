#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONVERSATION_CONFIG_SCHEMA_VERSION 3U
#define CONVERSATION_API_KEY_MAX_LENGTH 256U
#define CONVERSATION_API_HOST_MAX_LENGTH 127U
#define CONVERSATION_MODEL_NAME_MAX_LENGTH 48U
#define CONVERSATION_ENDPOINT_MAX_LENGTH 240U
/* application/x-www-form-urlencoded can expand each accepted byte to three
 * bytes. This bound covers the API key plus the small controlled fields. */
#define CONVERSATION_CONFIG_FORM_MAX_LENGTH 1536U
#define CONVERSATION_PREFERENCES_FORM_MAX_LENGTH 1024U
#define CONVERSATION_API_KEY_FORM_MAX_LENGTH 1024U
#define CONVERSATION_DEFAULT_API_HOST "dashscope.aliyuncs.com"
#define CONVERSATION_REALTIME_PATH "/api-ws/v1/realtime?model="

typedef enum {
    CONVERSATION_SERVICE_ALIYUN_REALTIME = 0,
    CONVERSATION_SERVICE_COUNT,
} conversation_service_t;

typedef enum {
    /* Persisted ID 0 is retained only to decode and migrate old records.
     * Never reuse its value for a different model. */
    CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME = 0,
    CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH,
    CONVERSATION_MODEL_COUNT,
} conversation_model_t;

#define CONVERSATION_DEFAULT_MODEL                                        \
    CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH

/* Stable persisted IDs. Only the Audio 3 shared system voices are supported. */
typedef enum {
    CONVERSATION_VOICE_LONGANQIAN = 0,
    CONVERSATION_VOICE_LONGANLINGXIN,
    CONVERSATION_VOICE_LONGANLINGXI,
    CONVERSATION_VOICE_LONGANXIAOXIN,
    CONVERSATION_VOICE_LONGANLUFENG,
    CONVERSATION_VOICE_COUNT,
} conversation_voice_t;

typedef enum {
    CONVERSATION_REPLY_STYLE_CONCISE = 0,
    CONVERSATION_REPLY_STYLE_ENGLISH_PRACTICE,
    CONVERSATION_REPLY_STYLE_COUNT,
} conversation_reply_style_t;

#define CONVERSATION_DEFAULT_VOICE CONVERSATION_VOICE_LONGANQIAN
#define CONVERSATION_DEFAULT_REPLY_STYLE CONVERSATION_REPLY_STYLE_CONCISE

/* Boot-local migration feedback, not a history stored in NVS. A subsequent
 * boot loading the current schema/model reports NONE. PENDING still uses the
 * new config in RAM; a later successful save completes persistence. */
typedef enum {
    CONVERSATION_CONFIG_MIGRATION_NONE = 0,
    CONVERSATION_CONFIG_MIGRATION_COMPLETED,
    CONVERSATION_CONFIG_MIGRATION_PENDING,
} conversation_config_migration_t;

typedef struct {
    uint16_t schema_version;
    conversation_service_t service;
    conversation_model_t model;
    conversation_voice_t voice;
    conversation_reply_style_t reply_style;
    bool enabled;
    char api_key[CONVERSATION_API_KEY_MAX_LENGTH + 1U];
    char api_host[CONVERSATION_API_HOST_MAX_LENGTH + 1U];
} conversation_config_t;

/* An empty api_key explicitly means "preserve the stored key". */
typedef struct {
    conversation_service_t service;
    conversation_model_t model;
    conversation_voice_t voice;
    conversation_reply_style_t reply_style;
    /* Omitted optional settings preserve the latest stored choices. */
    bool has_voice;
    bool has_reply_style;
    /* Credential-only forms omit enabled; legacy callers still explicitly
     * set enabled because this flag defaults to false. */
    bool preserve_enabled;
    bool enabled;
    char api_key[CONVERSATION_API_KEY_MAX_LENGTH + 1U];
    char api_host[CONVERSATION_API_HOST_MAX_LENGTH + 1U];
} conversation_config_update_t;

/* The ordinary-settings route never carries an API key. The original three
 * fields are required; model and api_host must both be present or both omitted
 * for older portals. Omitted fields preserve the latest stored values. */
typedef struct {
    bool enabled;
    conversation_voice_t voice;
    conversation_reply_style_t reply_style;
    conversation_model_t model;
    bool has_model;
    bool has_api_host;
    char api_host[CONVERSATION_API_HOST_MAX_LENGTH + 1U];
} conversation_preferences_t;

/* A separate credential route can change only the key. Empty means preserve
 * the latest saved key; it never clears credentials or enables the service. */
typedef struct {
    char api_key[CONVERSATION_API_KEY_MAX_LENGTH + 1U];
} conversation_api_key_update_t;

/* Presentation code receives no secret and cannot accidentally serialize the
 * API key into portal state or logs. */
typedef struct {
    conversation_service_t service;
    conversation_model_t model;
    conversation_voice_t voice;
    conversation_reply_style_t reply_style;
    bool enabled;
    bool configured;
    conversation_config_migration_t migration;
    uint32_t generation;
    bool shared_endpoint;
    char api_host[CONVERSATION_API_HOST_MAX_LENGTH + 1U];
} conversation_config_status_t;

typedef enum {
    CONVERSATION_CONFIG_RESULT_OK = 0,
    CONVERSATION_CONFIG_RESULT_INVALID_ARGUMENT,
    CONVERSATION_CONFIG_RESULT_UNSUPPORTED_SERVICE,
    CONVERSATION_CONFIG_RESULT_INVALID_MODEL,
    CONVERSATION_CONFIG_RESULT_INVALID_API_HOST,
    CONVERSATION_CONFIG_RESULT_API_KEY_REQUIRED,
    CONVERSATION_CONFIG_RESULT_INVALID_API_KEY,
    CONVERSATION_CONFIG_RESULT_INVALID_FORM,
    CONVERSATION_CONFIG_RESULT_INVALID_ENCODING,
    CONVERSATION_CONFIG_RESULT_MISSING_FIELD,
    CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD,
    CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD,
    CONVERSATION_CONFIG_RESULT_INVALID_VOICE,
    CONVERSATION_CONFIG_RESULT_INVALID_REPLY_STYLE,
} conversation_config_result_t;

void conversation_config_defaults(conversation_config_t *config);
void conversation_config_reset(conversation_config_t *config);
void conversation_config_clear_sensitive(void *memory, size_t size);
bool conversation_service_is_supported(conversation_service_t service);
bool conversation_model_is_supported(conversation_model_t model);
const char *conversation_model_name(conversation_model_t model);
bool conversation_model_from_name(const char *name,
                                  conversation_model_t *model);
const char *conversation_voice_name(conversation_voice_t voice);
bool conversation_voice_from_name(const char *name,
                                  conversation_voice_t *voice);
const char *conversation_reply_style_name(conversation_reply_style_t style);
bool conversation_reply_style_from_name(const char *name,
                                        conversation_reply_style_t *style);
bool conversation_api_host_is_allowed(const char *api_host);
bool conversation_config_build_endpoint(const char *api_host,
                                        conversation_model_t model,
                                        char *endpoint, size_t capacity);
conversation_config_result_t conversation_config_validate(
    const conversation_config_t *config);
conversation_config_result_t conversation_config_apply_update(
    const conversation_config_t *current,
    const conversation_config_update_t *update,
    conversation_config_t *candidate);
conversation_config_result_t conversation_config_parse_form(
    const char *form, size_t form_length,
    conversation_config_update_t *update);
conversation_config_result_t conversation_config_parse_preferences_form(
    const char *form, size_t form_length,
    conversation_preferences_t *preferences);
conversation_config_result_t conversation_config_parse_api_key_form(
    const char *form, size_t form_length,
    conversation_api_key_update_t *update);
conversation_config_result_t conversation_config_apply_preferences(
    const conversation_config_t *current,
    const conversation_preferences_t *preferences,
    conversation_config_t *candidate);
void conversation_config_make_status(
    const conversation_config_t *config,
    conversation_config_status_t *status);
const char *conversation_config_result_name(
    conversation_config_result_t result);

#ifdef __cplusplus
}
#endif
