#pragma once

#include <stdbool.h>

#include "alarm_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool key_blocked_until_release;
    bool boot_blocked_until_release;
} alarm_input_gate_t;

/* Prevents a raw or debounced key held across a modal boundary from acting
 * until its debounced release event has also been consumed. */
void alarm_input_gate_arm(alarm_input_gate_t *gate,
                          bool key_pressed_or_debounced,
                          bool boot_pressed_or_debounced);

bool alarm_input_gate_is_blocking(const alarm_input_gate_t *gate);

/* BOOT/stop wins if both short-press events arrive together. */
alarm_scheduler_input_t alarm_input_gate_update(
    alarm_input_gate_t *gate, bool key_pressed_or_debounced,
    bool boot_pressed_or_debounced,
    bool key_short_press, bool boot_short_press);

#ifdef __cplusplus
}
#endif
