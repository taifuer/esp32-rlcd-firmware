#include "conversation_lifecycle.h"

#include <stddef.h>

bool conversation_lifecycle_accept_events(
    const conversation_diagnostics_t *diagnostics,
    bool local_shutdown, bool terminal_phase)
{
    return diagnostics != NULL && !local_shutdown && !terminal_phase &&
           diagnostics->transport_end == CONVERSATION_TRANSPORT_END_NONE;
}

bool conversation_lifecycle_valid_close_code(int close_code)
{
    /* RFC 6455: these three values are local sentinels, never wire codes.
     * 1004 is reserved. Preserve extension/application codes as numeric
     * evidence without attempting to infer a service-specific meaning. */
    return close_code >= 1000 && close_code <= 4999 &&
           close_code != 1004 && close_code != 1005 &&
           close_code != 1006 && close_code != 1015;
}

bool conversation_lifecycle_end(
    conversation_diagnostics_t *diagnostics,
    conversation_transport_end_t end, int close_code,
    bool expected_shutdown, bool already_failed)
{
    if (diagnostics == NULL || end < CONVERSATION_TRANSPORT_END_DISCONNECTED ||
        end > CONVERSATION_TRANSPORT_END_FINISHED) {
        return false;
    }
    if (diagnostics->transport_end == CONVERSATION_TRANSPORT_END_NONE) {
        diagnostics->transport_end = end;
    }
    if (diagnostics->close_code == 0U &&
        conversation_lifecycle_valid_close_code(close_code)) {
        diagnostics->close_code = (uint16_t)close_code;
    }
    return !expected_shutdown && !already_failed;
}
