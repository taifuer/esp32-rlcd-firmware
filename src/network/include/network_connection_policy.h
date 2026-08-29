#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NETWORK_CONNECTION_MODE_NORMAL = 0,
    NETWORK_CONNECTION_MODE_SAVING,
} network_connection_mode_t;

typedef enum {
    NETWORK_CONNECTION_ACTIVITY_IDLE = 0,
    NETWORK_CONNECTION_ACTIVITY_SYNC,
    NETWORK_CONNECTION_ACTIVITY_ONLINE,
    NETWORK_CONNECTION_ACTIVITY_MAINTENANCE,
} network_connection_activity_t;

typedef enum {
    NETWORK_CONNECTION_OPERATION_SYNC = 0,
    NETWORK_CONNECTION_OPERATION_ONLINE,
    NETWORK_CONNECTION_OPERATION_MAINTENANCE,
} network_connection_operation_t;

typedef enum {
    NETWORK_CONNECTION_ACTION_NONE = 0,
    NETWORK_CONNECTION_ACTION_KEEP,
    NETWORK_CONNECTION_ACTION_STOP,
    NETWORK_CONNECTION_ACTION_CONNECT,
    NETWORK_CONNECTION_ACTION_DEFER,
} network_connection_action_t;

/* Decide the immediate radio action after the effective power mode changes.
 * An active operation owns its current link, so applying the new mode waits
 * until that operation completes. */
network_connection_action_t network_connection_policy_on_mode_change(
    network_connection_mode_t next_mode,
    network_connection_activity_t activity);

/* Decide how an operation releases the radio. Pass the latest effective mode,
 * not the mode captured when the operation began. Maintenance finishes with
 * its temporary AP stopped, so NORMAL reconnects instead of keeping a link. */
network_connection_action_t network_connection_policy_on_operation_end(
    network_connection_mode_t current_mode,
    network_connection_operation_t operation);

/* The automatic first-boot setup window stays closed after timing out, unless
 * the user explicitly asks to configure Wi-Fi again. */
bool network_connection_policy_should_provision(
    bool setup_window_completed, bool force_requested);

#ifdef __cplusplus
}
#endif
