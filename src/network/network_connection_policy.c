#include "network_connection_policy.h"

#include <stdbool.h>

static bool mode_is_valid(network_connection_mode_t mode)
{
    return mode == NETWORK_CONNECTION_MODE_NORMAL ||
           mode == NETWORK_CONNECTION_MODE_SAVING;
}

static bool activity_is_valid(network_connection_activity_t activity)
{
    return activity == NETWORK_CONNECTION_ACTIVITY_IDLE ||
           activity == NETWORK_CONNECTION_ACTIVITY_SYNC ||
           activity == NETWORK_CONNECTION_ACTIVITY_ONLINE ||
           activity == NETWORK_CONNECTION_ACTIVITY_MAINTENANCE;
}

static bool operation_is_valid(network_connection_operation_t operation)
{
    return operation == NETWORK_CONNECTION_OPERATION_SYNC ||
           operation == NETWORK_CONNECTION_OPERATION_ONLINE ||
           operation == NETWORK_CONNECTION_OPERATION_MAINTENANCE;
}

network_connection_action_t network_connection_policy_on_mode_change(
    network_connection_mode_t next_mode,
    network_connection_activity_t activity)
{
    if (!mode_is_valid(next_mode) || !activity_is_valid(activity)) {
        return NETWORK_CONNECTION_ACTION_NONE;
    }
    if (activity != NETWORK_CONNECTION_ACTIVITY_IDLE) {
        return NETWORK_CONNECTION_ACTION_DEFER;
    }
    return next_mode == NETWORK_CONNECTION_MODE_NORMAL
               ? NETWORK_CONNECTION_ACTION_CONNECT
               : NETWORK_CONNECTION_ACTION_STOP;
}

network_connection_action_t network_connection_policy_on_operation_end(
    network_connection_mode_t current_mode,
    network_connection_operation_t operation)
{
    if (!mode_is_valid(current_mode) || !operation_is_valid(operation)) {
        return NETWORK_CONNECTION_ACTION_NONE;
    }
    if (current_mode == NETWORK_CONNECTION_MODE_SAVING) {
        return NETWORK_CONNECTION_ACTION_STOP;
    }
    return operation == NETWORK_CONNECTION_OPERATION_MAINTENANCE
               ? NETWORK_CONNECTION_ACTION_CONNECT
               : NETWORK_CONNECTION_ACTION_KEEP;
}

bool network_connection_policy_should_provision(
    bool setup_window_completed, bool force_requested)
{
    return !setup_window_completed || force_requested;
}
