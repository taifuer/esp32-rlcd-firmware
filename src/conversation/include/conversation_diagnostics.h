#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONVERSATION_TRANSPORT_END_NONE = 0,
    CONVERSATION_TRANSPORT_END_DISCONNECTED = 1,
    CONVERSATION_TRANSPORT_END_CLOSED = 2,
    CONVERSATION_TRANSPORT_END_FINISHED = 3,
} conversation_transport_end_t;

/* Session-scoped, RAM-only evidence. The booleans are historical milestones,
 * not current connectivity. No credentials, conversation text, or peer close
 * reason may be retained here. Zero close_code means no valid code observed. */
typedef struct {
    bool websocket_connected;
    bool session_created;
    bool session_update_sent;
    bool session_ready;
    uint16_t close_code;
    conversation_transport_end_t transport_end;
} conversation_diagnostics_t;

#ifdef __cplusplus
}
#endif
