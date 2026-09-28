#pragma once

#include "conversation_diagnostics.h"

/* Call under the client's existing mutex. Once shutdown/terminal state has
 * been observed, late network/session callbacks must never reactivate it. */
bool conversation_lifecycle_accept_events(
    const conversation_diagnostics_t *diagnostics,
    bool local_shutdown, bool terminal_phase);

/* Record the first transport end and first valid peer CLOSE code. A normal
 * WebSocket code (1000) still represents failure when it ends a conversation
 * the user did not finish. Existing, more specific failures take precedence.
 * Returns true only when the client must enter a new FAILED state. */
bool conversation_lifecycle_end(
    conversation_diagnostics_t *diagnostics,
    conversation_transport_end_t end, int close_code,
    bool expected_shutdown, bool already_failed);

bool conversation_lifecycle_valid_close_code(int close_code);
