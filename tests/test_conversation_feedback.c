#include "conversation_feedback.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_classification(void)
{
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_NONE, 0, NULL) ==
           CONVERSATION_FEEDBACK_ERROR_NONE);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 401, "HandshakeError") ==
           CONVERSATION_FEEDBACK_ERROR_AUTH);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_TIMEOUT, 403, "HandshakeError") ==
           CONVERSATION_FEEDBACK_ERROR_ACCESS);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 404, "HandshakeError") ==
           CONVERSATION_FEEDBACK_ERROR_ACCESS);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 429, "HandshakeError") ==
           CONVERSATION_FEEDBACK_ERROR_LIMIT);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 503, "HandshakeError") ==
           CONVERSATION_FEEDBACK_ERROR_SERVICE);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 504, "HandshakeError") ==
           CONVERSATION_FEEDBACK_ERROR_TIMEOUT);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 400, "HandshakeError") ==
           CONVERSATION_FEEDBACK_ERROR_UNKNOWN);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "InvalidApiKey") ==
           CONVERSATION_FEEDBACK_ERROR_AUTH);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_TIMEOUT, 0, "invalid_api_key") ==
           CONVERSATION_FEEDBACK_ERROR_AUTH);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "Workspace.AccessDenied") ==
           CONVERSATION_FEEDBACK_ERROR_ACCESS);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "Endpoint.AccessDenied") ==
           CONVERSATION_FEEDBACK_ERROR_ACCESS);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "ModelNotFound") ==
           CONVERSATION_FEEDBACK_ERROR_MODEL);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "model_not_found") ==
           CONVERSATION_FEEDBACK_ERROR_MODEL);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "Throttling.RateQuota") ==
           CONVERSATION_FEEDBACK_ERROR_LIMIT);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "Throttling.ServiceOverloaded") ==
           CONVERSATION_FEEDBACK_ERROR_SERVICE);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "TlsError") ==
           CONVERSATION_FEEDBACK_ERROR_TLS);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "TransportTimeout") ==
           CONVERSATION_FEEDBACK_ERROR_TIMEOUT);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_TIMEOUT, 110, "TransportError") ==
           CONVERSATION_FEEDBACK_ERROR_TIMEOUT);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_RESOURCE, 0, "TransportError") ==
           CONVERSATION_FEEDBACK_ERROR_RESOURCE);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "AudioBufferFull") ==
           CONVERSATION_FEEDBACK_ERROR_RESOURCE);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_RESOURCE, 0, NULL) ==
           CONVERSATION_FEEDBACK_ERROR_RESOURCE);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "Disconnected") ==
           CONVERSATION_FEEDBACK_ERROR_NETWORK);
    /* An errno or WS close code must never be interpreted as HTTP rejection. */
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 401, "TransportError") ==
           CONVERSATION_FEEDBACK_ERROR_NETWORK);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 403, "ServerClose") ==
           CONVERSATION_FEEDBACK_ERROR_CLOSED);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_TIMEOUT, 1008, "ServerClose") ==
           CONVERSATION_FEEDBACK_ERROR_CLOSED);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, "TransportStopped") ==
           CONVERSATION_FEEDBACK_ERROR_NETWORK);
    assert(conversation_feedback_classify(
               CONVERSATION_FEEDBACK_LOCAL_FAILURE, 429, "ServiceError") ==
           CONVERSATION_FEEDBACK_ERROR_UNKNOWN);
}

static void test_safe_presentation(void)
{
    const char *untrusted[] = {
        "sk-sensitive-material", "InvalidApiKey: sk-sensitive-material",
        "ModelNotFound: user transcript", "access_denied<svg onload=evil>",
        "Throttling.unrecognized", "model_not_found ", "INVALIDAPIKEY",
        "missing_key", "Your account is expired, please pay", "\"quoted\"",
        "ResponseFailed", "ResponseIncomplete",
    };
    for (size_t i = 0U; i < sizeof(untrusted) / sizeof(untrusted[0]); ++i) {
        const conversation_feedback_error_t error = conversation_feedback_classify(
            CONVERSATION_FEEDBACK_LOCAL_FAILURE, 0, untrusted[i]);
        assert(error == CONVERSATION_FEEDBACK_ERROR_UNKNOWN);
        assert(strcmp(conversation_feedback_error_hint(error),
                      "Please try again later") == 0);
        assert(strstr(conversation_feedback_error_hint_zh(error), untrusted[i]) == NULL);
    }
    for (int value = CONVERSATION_FEEDBACK_ERROR_NONE;
         value <= CONVERSATION_FEEDBACK_ERROR_UNKNOWN; ++value) {
        const conversation_feedback_error_t error = (conversation_feedback_error_t)value;
        const char *name = conversation_feedback_error_name(error);
        const char *hint = conversation_feedback_error_hint(error);
        const char *hint_zh = conversation_feedback_error_hint_zh(error);
        assert(strlen(hint) <= 29U);
        assert(strpbrk(name, "\"\\\n\r<>") == NULL);
        assert(strpbrk(hint, "\"\\\n\r<>") == NULL);
        assert(strpbrk(hint_zh, "\"\\\n\r<>") == NULL);
    }
    assert(strcmp(conversation_feedback_error_name((conversation_feedback_error_t)99),
                  "unknown") == 0);
    assert(strcmp(conversation_feedback_result_name((conversation_feedback_result_t)99),
                  "not_tested") == 0);
}

static void test_last_result(void)
{
    conversation_feedback_status_t status = {0};
    assert(status.result == CONVERSATION_FEEDBACK_RESULT_NOT_TESTED);
    conversation_feedback_complete(&status, 12U, 1U, false,
                                   CONVERSATION_FEEDBACK_ERROR_NONE);
    assert(status.result == CONVERSATION_FEEDBACK_RESULT_SUCCESS);
    assert(status.config_generation == 12U && status.completed_turns == 1U);
    assert(status.error == CONVERSATION_FEEDBACK_ERROR_NONE);
    /* Ending from FOLLOW-UP, five rounds, or its normal timeout is success. */
    conversation_feedback_complete(&status, 12U, 5U, false,
                                   CONVERSATION_FEEDBACK_ERROR_NONE);
    assert(status.result == CONVERSATION_FEEDBACK_RESULT_SUCCESS);
    assert(status.completed_turns == 5U);
    /* A successful handshake/empty answer/all skipped replies is not success. */
    conversation_feedback_complete(&status, 12U, 0U, false,
                                   CONVERSATION_FEEDBACK_ERROR_NONE);
    assert(status.result == CONVERSATION_FEEDBACK_RESULT_CANCELLED);
    /* A later failed round wins over earlier successful rounds. */
    conversation_feedback_complete(&status, 12U, 2U, false,
                                   CONVERSATION_FEEDBACK_ERROR_TIMEOUT);
    assert(status.result == CONVERSATION_FEEDBACK_RESULT_FAILED);
    assert(status.error == CONVERSATION_FEEDBACK_ERROR_TIMEOUT);
    assert(status.completed_turns == 2U);
    /* BOOT/alarm cancellation wins; teardown errors are not service failure. */
    conversation_feedback_complete(&status, 12U, 2U, true,
                                   CONVERSATION_FEEDBACK_ERROR_NETWORK);
    assert(status.result == CONVERSATION_FEEDBACK_RESULT_CANCELLED);
    assert(status.error == CONVERSATION_FEEDBACK_ERROR_NONE);
    conversation_feedback_complete(&status, 12U, 0U, true,
                                   CONVERSATION_FEEDBACK_ERROR_AUTH);
    assert(status.result == CONVERSATION_FEEDBACK_RESULT_CANCELLED);
    assert(status.error == CONVERSATION_FEEDBACK_ERROR_NONE);
    conversation_feedback_complete(NULL, 12U, 0U, false,
                                   CONVERSATION_FEEDBACK_ERROR_UNKNOWN);

    conversation_feedback_complete(&status, 12U, 1U, false,
                                   CONVERSATION_FEEDBACK_ERROR_NONE);
    conversation_feedback_status_t view = conversation_feedback_for_generation(&status, 12U);
    assert(view.result == CONVERSATION_FEEDBACK_RESULT_SUCCESS);
    assert(view.completed_turns == 1U);
    status.diagnostics.websocket_connected = true;
    status.diagnostics.session_update_sent = true;
    status.diagnostics.close_code = 1008U;
    view = conversation_feedback_for_generation(&status, 12U);
    assert(view.diagnostics.websocket_connected);
    assert(view.diagnostics.close_code == 1008U);
    view = conversation_feedback_for_generation(&status, 13U);
    assert(view.result == CONVERSATION_FEEDBACK_RESULT_NOT_TESTED);
    assert(view.error == CONVERSATION_FEEDBACK_ERROR_NONE);
    assert(view.completed_turns == 0U && view.config_generation == 0U);
    assert(!view.diagnostics.websocket_connected);
    assert(!view.diagnostics.session_update_sent);
    assert(view.diagnostics.close_code == 0U);
    view = conversation_feedback_for_generation(&status, 0U);
    assert(view.result == CONVERSATION_FEEDBACK_RESULT_NOT_TESTED);
    conversation_feedback_complete(&status, 14U, 0U, true,
                                   CONVERSATION_FEEDBACK_ERROR_CLOSED);
    assert(status.result == CONVERSATION_FEEDBACK_RESULT_CANCELLED);
    assert(status.diagnostics.close_code == 0U);
    assert(!status.diagnostics.websocket_connected);
    view = conversation_feedback_for_generation(NULL, 12U);
    assert(view.result == CONVERSATION_FEEDBACK_RESULT_NOT_TESTED);
    status.config_generation = 0U;
    view = conversation_feedback_for_generation(&status, 0U);
    assert(view.result == CONVERSATION_FEEDBACK_RESULT_NOT_TESTED);
}

int main(void)
{
    test_classification();
    test_safe_presentation();
    test_last_result();
    puts("conversation feedback tests passed");
    return 0;
}
