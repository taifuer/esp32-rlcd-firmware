#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>

#include "conversation_lifecycle.h"

static void test_early_normal_close_is_failure(void)
{
    conversation_diagnostics_t diagnostics = {.websocket_connected = true};
    assert(conversation_lifecycle_accept_events(&diagnostics, false, false));
    assert(conversation_lifecycle_end(
        &diagnostics, CONVERSATION_TRANSPORT_END_CLOSED, 1000, false, false));
    assert(diagnostics.transport_end == CONVERSATION_TRANSPORT_END_CLOSED);
    assert(diagnostics.close_code == 1000U);
    assert(diagnostics.websocket_connected);
    assert(!diagnostics.session_ready);
    assert(!conversation_lifecycle_accept_events(&diagnostics, false, false));
    assert(!conversation_lifecycle_end(
        &diagnostics, CONVERSATION_TRANSPORT_END_FINISHED, 0, false, true));
    assert(diagnostics.transport_end == CONVERSATION_TRANSPORT_END_CLOSED);
    assert(diagnostics.close_code == 1000U);
}

static void test_finish_without_close_is_a_failure(void)
{
    conversation_diagnostics_t diagnostics = {0};
    assert(conversation_lifecycle_end(
        &diagnostics, CONVERSATION_TRANSPORT_END_FINISHED, 0, false, false));
    assert(diagnostics.transport_end == CONVERSATION_TRANSPORT_END_FINISHED);
    assert(diagnostics.close_code == 0U);
    assert(!conversation_lifecycle_accept_events(&diagnostics, false, false));
}

static void test_local_shutdown_and_specific_error_take_precedence(void)
{
    const conversation_transport_end_t endings[] = {
        CONVERSATION_TRANSPORT_END_DISCONNECTED,
        CONVERSATION_TRANSPORT_END_CLOSED,
        CONVERSATION_TRANSPORT_END_FINISHED,
    };
    for (size_t index = 0U; index < sizeof(endings) / sizeof(endings[0]);
         ++index) {
        conversation_diagnostics_t cancelled = {0};
        conversation_diagnostics_t failed = {0};
        assert(!conversation_lifecycle_end(
            &cancelled, endings[index], 1000, true, false));
        assert(!conversation_lifecycle_end(
            &failed, endings[index], 1008, false, true));
        assert(cancelled.transport_end == endings[index]);
        assert(failed.transport_end == endings[index]);
        assert(cancelled.close_code == 1000U);
        assert(failed.close_code == 1008U);
        assert(!conversation_lifecycle_accept_events(&cancelled, false, false));
        assert(!conversation_lifecycle_accept_events(&failed, false, false));
    }
}

static void test_late_events_rejected_before_first_end_callback(void)
{
    conversation_diagnostics_t diagnostics = {0};
    /* The local cancel flag and FAILED/STOPPING/STOPPED phase each close the
     * gate immediately, even before the transport dispatches its last event. */
    assert(!conversation_lifecycle_accept_events(&diagnostics, true, false));
    assert(!conversation_lifecycle_accept_events(&diagnostics, false, true));
    assert(!conversation_lifecycle_accept_events(&diagnostics, true, true));
    assert(!conversation_lifecycle_accept_events(NULL, false, false));
}

static void test_first_reason_and_valid_code_survive_cleanup(void)
{
    conversation_diagnostics_t diagnostics = {
        .websocket_connected = true,
        .session_created = true,
        .session_update_sent = true,
        .session_ready = true,
    };
    assert(conversation_lifecycle_end(
        &diagnostics, CONVERSATION_TRANSPORT_END_DISCONNECTED, 0, false, false));
    assert(diagnostics.close_code == 0U);
    assert(!conversation_lifecycle_end(
        &diagnostics, CONVERSATION_TRANSPORT_END_CLOSED, 1008, true, true));
    assert(diagnostics.close_code == 1008U);
    assert(!conversation_lifecycle_end(
        &diagnostics, CONVERSATION_TRANSPORT_END_CLOSED, 1000, true, true));
    assert(!conversation_lifecycle_end(
        &diagnostics, CONVERSATION_TRANSPORT_END_FINISHED, 0, true, true));
    assert(diagnostics.transport_end == CONVERSATION_TRANSPORT_END_DISCONNECTED);
    assert(diagnostics.close_code == 1008U);
    assert(diagnostics.websocket_connected && diagnostics.session_created &&
           diagnostics.session_update_sent && diagnostics.session_ready);
}

static void test_no_fabricated_wire_code(void)
{
    const int invalid[] = {
        INT_MIN, -1, 0, 999, 1004, 1005, 1006, 1015, 5000, INT_MAX,
    };
    const int valid[] = {1000, 1001, 1002, 1007, 1008, 1011, 1014, 3000, 4999};
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]);
         ++index) {
        conversation_diagnostics_t diagnostics = {0};
        assert(!conversation_lifecycle_valid_close_code(invalid[index]));
        assert(conversation_lifecycle_end(
            &diagnostics, CONVERSATION_TRANSPORT_END_CLOSED,
            invalid[index], false, false));
        assert(diagnostics.close_code == 0U);
    }
    for (size_t index = 0U; index < sizeof(valid) / sizeof(valid[0]); ++index) {
        assert(conversation_lifecycle_valid_close_code(valid[index]));
    }
    conversation_diagnostics_t diagnostics = {0};
    assert(!conversation_lifecycle_end(
        &diagnostics, CONVERSATION_TRANSPORT_END_NONE, 1000, false, false));
    assert(!conversation_lifecycle_end(
        &diagnostics, (conversation_transport_end_t)99, 1000, false, false));
    assert(!conversation_lifecycle_end(
        NULL, CONVERSATION_TRANSPORT_END_CLOSED, 1000, false, false));
    assert(diagnostics.transport_end == CONVERSATION_TRANSPORT_END_NONE);
    assert(diagnostics.close_code == 0U);
}

int main(void)
{
    test_early_normal_close_is_failure();
    test_finish_without_close_is_a_failure();
    test_local_shutdown_and_specific_error_take_precedence();
    test_late_events_rejected_before_first_end_callback();
    test_first_reason_and_valid_code_survive_cleanup();
    test_no_fabricated_wire_code();
    puts("conversation_lifecycle: OK");
    return 0;
}
