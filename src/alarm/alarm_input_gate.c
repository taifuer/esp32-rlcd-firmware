#include "alarm_input_gate.h"

#include <stddef.h>

void alarm_input_gate_arm(alarm_input_gate_t *gate,
                          bool key_pressed_or_debounced,
                          bool boot_pressed_or_debounced)
{
    if (gate == NULL) {
        return;
    }
    gate->key_blocked_until_release = key_pressed_or_debounced;
    gate->boot_blocked_until_release = boot_pressed_or_debounced;
}

bool alarm_input_gate_is_blocking(const alarm_input_gate_t *gate)
{
    return gate != NULL && (gate->key_blocked_until_release ||
                            gate->boot_blocked_until_release);
}

alarm_scheduler_input_t alarm_input_gate_update(
    alarm_input_gate_t *gate, bool key_pressed_or_debounced,
    bool boot_pressed_or_debounced,
    bool key_short_press, bool boot_short_press)
{
    if (gate == NULL) {
        return ALARM_SCHEDULER_INPUT_NONE;
    }

    const bool key_blocked = gate->key_blocked_until_release;
    const bool boot_blocked = gate->boot_blocked_until_release;
    if (key_blocked && !key_pressed_or_debounced) {
        gate->key_blocked_until_release = false;
    }
    if (boot_blocked && !boot_pressed_or_debounced) {
        gate->boot_blocked_until_release = false;
    }

    if (boot_short_press && !boot_blocked) {
        return ALARM_SCHEDULER_INPUT_STOP;
    }
    if (key_short_press && !key_blocked) {
        return ALARM_SCHEDULER_INPUT_SNOOZE;
    }
    return ALARM_SCHEDULER_INPUT_NONE;
}
