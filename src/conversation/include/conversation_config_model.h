#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONVERSATION_CONFIG_SCHEMA_VERSION 2U
#define CONVERSATION_API_KEY_MAX_LENGTH 256U
#define CONVERSATION_API_HOST_MAX_LENGTH 127U
#define CONVERSATION_MODEL_NAME_MAX_LENGTH 48U
#define CONVERSATION_ENDPOINT_MAX_LENGTH 240U
/* application/x-www-form-urlencoded can expand each accepted byte to three
 * bytes. This bound covers the API key plus the small controlled fields. */
#define CONVERSATION_CONFIG_FORM_MAX_LENGTH 1536U
#define CONVERSATION_DEFAULT_API_HOST "dashscope.aliyuncs.com"
#define CONVERSATION_REALTIME_PATH "/api-ws/v1/realtime?model="

typedef enum {
    CONVERSATION_SERVICE_ALIYUN_REALTIME = 0,
    CONVERSATION_SERVICE_COUNT,
} conversation_service_t;

typedef enum {
    CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME = 0,
    CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH,
    CONVERSATION_MODEL_COUNT,
} conversation_model_t;

#define CONVERSATION_DEFAULT_MODEL                                        \
    CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME

typedef struct {
    uint16_t schema_version;
    conversation_service_t service;
    conversation_model_t model;
    bool enabled;
    char api_key[CONVERSATION_API_KEY_MAX_LENGTH + 1U];
    char api_host[CONVERSATION_API_HOST_MAX_LENGTH + 1U];
} conversation_config_t;

/* An empty api_key explicitly means "preserve the stored key". */
typedef struct {
    conversation_service_t service;
    conversation_model_t model;
    bool enabled;
    char api_key[CONVERSATION_API_KEY_MAX_LENGTH + 1U];
    char api_host[CONVERSATION_API_HOST_MAX_LENGTH + 1U];
} conversation_config_update_t;

/* Presentation code receives no secret and cannot accidentally serialize the
 * API key into portal state or logs. */
typedef struct {
    conversation_service_t service;
    conversation_model_t model;
    bool enabled;
    bool configured;
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
} conversation_config_result_t;

void conversation_config_defaults(conversation_config_t *config);
void conversation_config_reset(conversation_config_t *config);
void conversation_config_clear_sensitive(void *memory, size_t size);
bool conversation_service_is_supported(conversation_service_t service);
bool conversation_model_is_supported(conversation_model_t model);
const char *conversation_model_name(conversation_model_t model);
bool conversation_model_from_name(const char *name,
                                  conversation_model_t *model);
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
void conversation_config_make_status(
    const conversation_config_t *config,
    conversation_config_status_t *status);
const char *conversation_config_result_name(
    conversation_config_result_t result);

#ifdef __cplusplus
}
#endif
