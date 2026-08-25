#include <assert.h>
#include <stdio.h>

#include "network_session_policy.h"

int main(void)
{
    network_session_policy_t policy = {0};

    assert(network_session_policy_try_begin_task(&policy));
    assert(!network_session_policy_try_begin_task(&policy));
    assert(!network_session_policy_try_acquire(
        &policy, NETWORK_SESSION_OWNER_MAINTENANCE));
    assert(!network_session_policy_transfer(
        &policy, NETWORK_SESSION_OWNER_MAINTENANCE,
        NETWORK_SESSION_OWNER_ONLINE));
    assert(network_session_policy_request_task(&policy));
    network_session_policy_end_task(&policy);

    assert(network_session_policy_request_task(&policy));
    assert(policy.task_active);
    assert(policy.task_begin_pending);
    assert(!network_session_policy_try_acquire(
        &policy, NETWORK_SESSION_OWNER_MAINTENANCE));
    assert(network_session_policy_try_begin_task(&policy));
    assert(policy.task_active);
    assert(!policy.task_begin_pending);
    assert(!network_session_policy_try_begin_task(&policy));
    network_session_policy_end_task(&policy);

    assert(network_session_policy_try_acquire(
        &policy, NETWORK_SESSION_OWNER_MAINTENANCE));
    assert(network_session_policy_is_exclusive(&policy));
    assert(!network_session_policy_try_begin_task(&policy));
    assert(!network_session_policy_request_task(&policy));
    assert(!network_session_policy_try_acquire(
        &policy, NETWORK_SESSION_OWNER_ONLINE));
    assert(network_session_policy_transfer(
        &policy, NETWORK_SESSION_OWNER_MAINTENANCE,
        NETWORK_SESSION_OWNER_ONLINE));
    assert(!network_session_policy_transfer(
        &policy, NETWORK_SESSION_OWNER_MAINTENANCE,
        NETWORK_SESSION_OWNER_ONLINE));
    assert(!network_session_policy_release(
        &policy, NETWORK_SESSION_OWNER_MAINTENANCE));
    assert(network_session_policy_release(
        &policy, NETWORK_SESSION_OWNER_ONLINE));

    assert(network_session_policy_try_acquire(
        &policy, NETWORK_SESSION_OWNER_MAINTENANCE));
    assert(!network_session_policy_release_to_task(
        &policy, NETWORK_SESSION_OWNER_ONLINE));
    assert(network_session_policy_release_to_task(
        &policy, NETWORK_SESSION_OWNER_MAINTENANCE));
    assert(!network_session_policy_is_exclusive(&policy));
    assert(policy.task_active);
    assert(policy.task_begin_pending);
    assert(!network_session_policy_try_acquire(
        &policy, NETWORK_SESSION_OWNER_ONLINE));
    assert(network_session_policy_try_begin_task(&policy));
    network_session_policy_end_task(&policy);

    assert(network_session_policy_try_acquire(
        &policy, NETWORK_SESSION_OWNER_ONLINE));
    assert(network_session_policy_release(
        &policy, NETWORK_SESSION_OWNER_ONLINE));
    assert(!network_session_policy_is_exclusive(&policy));
    assert(network_session_policy_try_begin_task(&policy));
    network_session_policy_end_task(&policy);

    assert(!network_session_policy_try_begin_task(NULL));
    assert(!network_session_policy_try_acquire(
        NULL, NETWORK_SESSION_OWNER_ONLINE));
    assert(!network_session_policy_request_task(NULL));
    assert(!network_session_policy_try_acquire(
        &policy, NETWORK_SESSION_OWNER_NONE));
    assert(!network_session_policy_release(
        &policy, NETWORK_SESSION_OWNER_NONE));
    assert(!network_session_policy_transfer(
        NULL, NETWORK_SESSION_OWNER_MAINTENANCE,
        NETWORK_SESSION_OWNER_ONLINE));
    assert(!network_session_policy_transfer(
        &policy, NETWORK_SESSION_OWNER_NONE,
        NETWORK_SESSION_OWNER_ONLINE));
    assert(!network_session_policy_transfer(
        &policy, NETWORK_SESSION_OWNER_ONLINE,
        NETWORK_SESSION_OWNER_NONE));
    assert(!network_session_policy_transfer(
        &policy, NETWORK_SESSION_OWNER_ONLINE,
        NETWORK_SESSION_OWNER_ONLINE));
    assert(!network_session_policy_release_to_task(
        NULL, NETWORK_SESSION_OWNER_MAINTENANCE));
    assert(!network_session_policy_release_to_task(
        &policy, NETWORK_SESSION_OWNER_NONE));

    puts("network session policy tests passed");
    return 0;
}
