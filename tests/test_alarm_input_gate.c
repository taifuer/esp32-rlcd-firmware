#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "alarm_input_gate.h"

int main(void)
{
    alarm_input_gate_t gate = {0};
    assert(!alarm_input_gate_is_blocking(&gate));

    /* A raw-only press that has not completed debounce is still blocked. */
    alarm_input_gate_arm(&gate, true, false);
    assert(alarm_input_gate_is_blocking(&gate));
    assert(alarm_input_gate_update(&gate, true, false, false, false) ==
           ALARM_SCHEDULER_INPUT_NONE);
    /* Raw release leaves the combined state asserted until debounce catches
     * up, so the gate must remain closed across this intermediate loop. */
    assert(alarm_input_gate_update(&gate, true, false, false, false) ==
           ALARM_SCHEDULER_INPUT_NONE);
    assert(alarm_input_gate_is_blocking(&gate));
    /* The release event that clears the gate is itself consumed. */
    assert(alarm_input_gate_update(&gate, false, false, true, false) ==
           ALARM_SCHEDULER_INPUT_NONE);
    assert(!alarm_input_gate_is_blocking(&gate));
    assert(alarm_input_gate_update(&gate, false, false, true, false) ==
           ALARM_SCHEDULER_INPUT_SNOOZE);

    alarm_input_gate_arm(&gate, false, false);
    assert(alarm_input_gate_update(&gate, false, false, true, true) ==
           ALARM_SCHEDULER_INPUT_STOP);

    alarm_input_gate_arm(&gate, true, true);
    assert(alarm_input_gate_update(&gate, false, false, true, true) ==
           ALARM_SCHEDULER_INPUT_NONE);
    assert(alarm_input_gate_update(&gate, false, false, false, true) ==
           ALARM_SCHEDULER_INPUT_STOP);

    alarm_input_gate_arm(&gate, false, true);
    assert(alarm_input_gate_is_blocking(&gate));
    assert(alarm_input_gate_update(&gate, false, false, false, false) ==
           ALARM_SCHEDULER_INPUT_NONE);
    assert(!alarm_input_gate_is_blocking(&gate));
    assert(!alarm_input_gate_is_blocking(NULL));

    puts("alarm input gate tests passed");
    return 0;
}
