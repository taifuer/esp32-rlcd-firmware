#include "network_session_policy.h"

#include <stddef.h>

bool network_session_policy_try_begin_task(network_session_policy_t *policy)
{
    if (policy == NULL || policy->task_active ||
        policy->owner != NETWORK_SESSION_OWNER_NONE) {
        return false;
    }

    policy->task_active = true;
    return true;
}

void network_session_policy_end_task(network_session_policy_t *policy)
{
    if (policy != NULL) {
        policy->task_active = false;
    }
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

bool network_session_policy_is_exclusive(const network_session_policy_t *policy)
{
    return policy != NULL && policy->owner != NETWORK_SESSION_OWNER_NONE;
}
