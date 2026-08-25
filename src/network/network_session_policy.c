#include "network_session_policy.h"

#include <stddef.h>

bool network_session_policy_try_begin_task(network_session_policy_t *policy)
{
    if (policy == NULL || policy->owner != NETWORK_SESSION_OWNER_NONE) {
        return false;
    }

    if (policy->task_begin_pending) {
        if (!policy->task_active) {
            return false;
        }
        policy->task_begin_pending = false;
        return true;
    }
    if (policy->task_active) {
        return false;
    }

    policy->task_active = true;
    return true;
}

void network_session_policy_end_task(network_session_policy_t *policy)
{
    if (policy != NULL) {
        policy->task_active = false;
        policy->task_begin_pending = false;
    }
}

bool network_session_policy_request_task(network_session_policy_t *policy)
{
    if (policy == NULL || policy->owner != NETWORK_SESSION_OWNER_NONE) {
        return false;
    }

    if (!policy->task_active) {
        policy->task_active = true;
        policy->task_begin_pending = true;
    }
    return true;
}

bool network_session_policy_try_acquire(network_session_policy_t *policy,
                                        network_session_owner_t owner)
{
    if (policy == NULL || owner == NETWORK_SESSION_OWNER_NONE ||
        policy->task_active || policy->owner != NETWORK_SESSION_OWNER_NONE) {
        return false;
    }

    policy->owner = owner;
    return true;
}

bool network_session_policy_release(network_session_policy_t *policy,
                                    network_session_owner_t owner)
{
    if (policy == NULL || owner == NETWORK_SESSION_OWNER_NONE ||
        policy->owner != owner) {
        return false;
    }

    policy->owner = NETWORK_SESSION_OWNER_NONE;
    return true;
}

bool network_session_policy_release_to_task(
    network_session_policy_t *policy, network_session_owner_t owner)
{
    if (policy == NULL || owner == NETWORK_SESSION_OWNER_NONE ||
        policy->owner != owner || policy->task_active ||
        policy->task_begin_pending) {
        return false;
    }

    policy->owner = NETWORK_SESSION_OWNER_NONE;
    policy->task_active = true;
    policy->task_begin_pending = true;
    return true;
}

bool network_session_policy_transfer(network_session_policy_t *policy,
                                     network_session_owner_t from,
                                     network_session_owner_t to)
{
    if (policy == NULL || from == NETWORK_SESSION_OWNER_NONE ||
        to == NETWORK_SESSION_OWNER_NONE || from == to ||
        policy->task_active || policy->owner != from) {
        return false;
    }

    policy->owner = to;
    return true;
}

bool network_session_policy_is_exclusive(const network_session_policy_t *policy)
{
    return policy != NULL && policy->owner != NETWORK_SESSION_OWNER_NONE;
}
