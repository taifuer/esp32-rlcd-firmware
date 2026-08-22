#pragma once

#include <stdbool.h>

typedef enum {
    NETWORK_SESSION_OWNER_NONE = 0,
    NETWORK_SESSION_OWNER_MAINTENANCE,
    NETWORK_SESSION_OWNER_ONLINE,
} network_session_owner_t;

typedef struct {
    network_session_owner_t owner;
    bool task_active;
} network_session_policy_t;

bool network_session_policy_try_begin_task(network_session_policy_t *policy);
void network_session_policy_end_task(network_session_policy_t *policy);
bool network_session_policy_try_acquire(network_session_policy_t *policy,
                                        network_session_owner_t owner);
bool network_session_policy_release(network_session_policy_t *policy,
                                    network_session_owner_t owner);
bool network_session_policy_is_exclusive(const network_session_policy_t *policy);
