#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUTTON_DEBOUNCE_MS 40U
#define BUTTON_HOLD_PROMPT_MS 1000U
#define BUTTON_LONG_PRESS_MS 5000U
#define BUTTON_LONG_PRESS_DISABLED_MS UINT32_MAX
#define BUTTON_CHORD_MAX_ONSET_MS 300U
#define BUTTON_CHORD_SHORT_PRESS_MS 1000U

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
    uint32_t hold_prompt_ms;
    uint32_t long_press_ms;
} button_state_t;

/* A navigation chord is separate from individual-button event handling.
 * It must see both buttons released before accepting its first gesture. */
typedef struct {
    bool release_required;
    bool tracking;
    bool overlap_seen;
    bool cancelled;
    uint32_t elapsed_ms;
} button_chord_t;

void button_state_init(button_state_t *state, bool raw_pressed);
void button_state_init_custom(button_state_t *state, bool raw_pressed,
                              uint32_t hold_prompt_ms,
                              uint32_t long_press_ms);
bool button_state_set_timing(button_state_t *state, uint32_t hold_prompt_ms,
                             uint32_t long_press_ms);
bool button_state_set_action_timing(button_state_t *state,
                                    uint32_t long_press_ms);
button_event_t button_state_update(button_state_t *state, bool raw_pressed,
                                   uint32_t elapsed_ms);
bool button_state_is_pressed(const button_state_t *state);
uint32_t button_state_hold_ms(const button_state_t *state);
bool button_state_hold_prompt_active(const button_state_t *state);
uint32_t button_state_hold_seconds_remaining(const button_state_t *state);

void button_chord_init(button_chord_t *chord);
/* Call after updating both button states. Eligibility must remain true for
 * the whole gesture; this does not consume or change single-button events.
 * True is returned once, only after both raw and debounced inputs release. */
bool button_chord_update(button_chord_t *chord,
                         const button_state_t *first,
                         const button_state_t *second,
                         bool eligible, uint32_t elapsed_ms);
bool button_chord_is_tracking(const button_chord_t *chord);

#ifdef __cplusplus
}
#endif
