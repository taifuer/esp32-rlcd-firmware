#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_DEBOUNCE_MS 40U
#define BUTTON_HOLD_PROMPT_MS 1000U
#define BUTTON_LONG_PRESS_MS 5000U

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_SHORT_PRESS,
    BUTTON_EVENT_HOLD_CANCELLED,
    BUTTON_EVENT_LONG_PRESS,
} button_event_t;

typedef struct {
    bool candidate_pressed;
    bool debounced_pressed;
    bool long_press_reported;
    uint32_t candidate_ms;
    uint32_t hold_ms;
} button_state_t;

void button_state_init(button_state_t *state, bool raw_pressed);
button_event_t button_state_update(button_state_t *state, bool raw_pressed,
                                   uint32_t elapsed_ms);
bool button_state_is_pressed(const button_state_t *state);
uint32_t button_state_hold_ms(const button_state_t *state);

#ifdef __cplusplus
}
#endif
