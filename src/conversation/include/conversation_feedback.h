#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "conversation_diagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONVERSATION_FEEDBACK_ERROR_NONE = 0,
    CONVERSATION_FEEDBACK_ERROR_AUTH,
    CONVERSATION_FEEDBACK_ERROR_ACCESS,
    CONVERSATION_FEEDBACK_ERROR_MODEL,
    CONVERSATION_FEEDBACK_ERROR_LIMIT,
    CONVERSATION_FEEDBACK_ERROR_TIMEOUT,
    CONVERSATION_FEEDBACK_ERROR_NETWORK,
    CONVERSATION_FEEDBACK_ERROR_TLS,
    CONVERSATION_FEEDBACK_ERROR_SERVICE,
    CONVERSATION_FEEDBACK_ERROR_RESOURCE,
    CONVERSATION_FEEDBACK_ERROR_CLOSED,
    CONVERSATION_FEEDBACK_ERROR_UNKNOWN,
} conversation_feedback_error_t;

typedef enum {
    CONVERSATION_FEEDBACK_LOCAL_NONE = 0,
    CONVERSATION_FEEDBACK_LOCAL_FAILURE,
    CONVERSATION_FEEDBACK_LOCAL_TIMEOUT,
    CONVERSATION_FEEDBACK_LOCAL_RESOURCE,
} conversation_feedback_local_error_t;

typedef enum {
    CONVERSATION_FEEDBACK_RESULT_NOT_TESTED = 0,
    CONVERSATION_FEEDBACK_RESULT_SUCCESS,
    CONVERSATION_FEEDBACK_RESULT_FAILED,
    CONVERSATION_FEEDBACK_RESULT_CANCELLED,
} conversation_feedback_result_t;

/* RAM-only summary: no credentials, audio, transcript, raw service text or
 * persistent history. A different configuration generation invalidates it. */
typedef struct {
    conversation_feedback_result_t result;
    conversation_feedback_error_t error;
    uint32_t config_generation;
    uint8_t completed_turns;
    conversation_diagnostics_t diagnostics;
} conversation_feedback_status_t;

/* Only exact allowlisted machine codes are interpreted. The integer is an
 * HTTP status only with HandshakeError/Unauthorized/Forbidden, never errno
 * or a WebSocket close code. Raw names are never returned to presentation. */
conversation_feedback_error_t conversation_feedback_classify(
    conversation_feedback_local_error_t local_error,
    int service_code, const char *service_name);
const char *conversation_feedback_error_name(conversation_feedback_error_t error);
const char *conversation_feedback_error_hint(conversation_feedback_error_t error);
const char *conversation_feedback_error_hint_zh(conversation_feedback_error_t error);
const char *conversation_feedback_result_name(conversation_feedback_result_t result);

/* Called after shared cleanup. Cancel/preemption is not a service failure;
 * otherwise a failure wins over earlier completed rounds. Success requires
 * at least one normally completed response whose local PCM was drained. */
void conversation_feedback_complete(
    conversation_feedback_status_t *status, uint32_t config_generation,
    uint8_t completed_turns, bool cancelled,
    conversation_feedback_error_t error);
conversation_feedback_status_t conversation_feedback_for_generation(
    const conversation_feedback_status_t *status, uint32_t current_generation);

#ifdef __cplusplus
}
#endif
