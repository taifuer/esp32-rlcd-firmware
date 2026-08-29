#include <assert.h>
#include <stdio.h>

#include "network_connection_policy.h"

static void test_normal_operation_completion_keeps_station(void)
{
    assert(network_connection_policy_on_operation_end(
               NETWORK_CONNECTION_MODE_NORMAL,
               NETWORK_CONNECTION_OPERATION_SYNC) ==
           NETWORK_CONNECTION_ACTION_KEEP);
    assert(network_connection_policy_on_operation_end(
               NETWORK_CONNECTION_MODE_NORMAL,
               NETWORK_CONNECTION_OPERATION_ONLINE) ==
           NETWORK_CONNECTION_ACTION_KEEP);
}

static void test_saving_operation_completion_stops_station(void)
{
    assert(network_connection_policy_on_operation_end(
               NETWORK_CONNECTION_MODE_SAVING,
               NETWORK_CONNECTION_OPERATION_SYNC) ==
           NETWORK_CONNECTION_ACTION_STOP);
    assert(network_connection_policy_on_operation_end(
               NETWORK_CONNECTION_MODE_SAVING,
               NETWORK_CONNECTION_OPERATION_ONLINE) ==
           NETWORK_CONNECTION_ACTION_STOP);
}

static void test_idle_mode_change_applies_immediately(void)
{
    assert(network_connection_policy_on_mode_change(
               NETWORK_CONNECTION_MODE_SAVING,
               NETWORK_CONNECTION_ACTIVITY_IDLE) ==
           NETWORK_CONNECTION_ACTION_STOP);
    assert(network_connection_policy_on_mode_change(
               NETWORK_CONNECTION_MODE_NORMAL,
               NETWORK_CONNECTION_ACTIVITY_IDLE) ==
           NETWORK_CONNECTION_ACTION_CONNECT);
}

static void test_busy_mode_change_waits_for_owner(void)
{
    const network_connection_activity_t activities[] = {
        NETWORK_CONNECTION_ACTIVITY_SYNC,
        NETWORK_CONNECTION_ACTIVITY_ONLINE,
        NETWORK_CONNECTION_ACTIVITY_MAINTENANCE,
    };
    for (size_t index = 0U;
         index < sizeof(activities) / sizeof(activities[0]); ++index) {
        assert(network_connection_policy_on_mode_change(
                   NETWORK_CONNECTION_MODE_SAVING, activities[index]) ==
               NETWORK_CONNECTION_ACTION_DEFER);
        assert(network_connection_policy_on_mode_change(
                   NETWORK_CONNECTION_MODE_NORMAL, activities[index]) ==
               NETWORK_CONNECTION_ACTION_DEFER);
    }
}

static void test_portal_end_uses_latest_mode(void)
{
    assert(network_connection_policy_on_operation_end(
               NETWORK_CONNECTION_MODE_NORMAL,
               NETWORK_CONNECTION_OPERATION_MAINTENANCE) ==
           NETWORK_CONNECTION_ACTION_CONNECT);
    assert(network_connection_policy_on_operation_end(
               NETWORK_CONNECTION_MODE_SAVING,
               NETWORK_CONNECTION_OPERATION_MAINTENANCE) ==
           NETWORK_CONNECTION_ACTION_STOP);
}

static void test_invalid_inputs_do_nothing(void)
{
    assert(network_connection_policy_on_mode_change(
               (network_connection_mode_t)99,
               NETWORK_CONNECTION_ACTIVITY_IDLE) ==
           NETWORK_CONNECTION_ACTION_NONE);
    assert(network_connection_policy_on_mode_change(
               NETWORK_CONNECTION_MODE_NORMAL,
               (network_connection_activity_t)99) ==
           NETWORK_CONNECTION_ACTION_NONE);
    assert(network_connection_policy_on_operation_end(
               (network_connection_mode_t)99,
               NETWORK_CONNECTION_OPERATION_SYNC) ==
           NETWORK_CONNECTION_ACTION_NONE);
    assert(network_connection_policy_on_operation_end(
               NETWORK_CONNECTION_MODE_NORMAL,
               (network_connection_operation_t)99) ==
           NETWORK_CONNECTION_ACTION_NONE);
}

static void test_explicit_setup_reopens_expired_window(void)
{
    assert(network_connection_policy_should_provision(false, false));
    assert(network_connection_policy_should_provision(false, true));
    assert(!network_connection_policy_should_provision(true, false));
    assert(network_connection_policy_should_provision(true, true));
}

int main(void)
{
    test_normal_operation_completion_keeps_station();
    test_saving_operation_completion_stops_station();
    test_idle_mode_change_applies_immediately();
    test_busy_mode_change_waits_for_owner();
    test_portal_end_uses_latest_mode();
    test_explicit_setup_reopens_expired_window();
    test_invalid_inputs_do_nothing();

    puts("network connection policy tests passed");
    return 0;
}
