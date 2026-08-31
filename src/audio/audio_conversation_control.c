#include "audio_conversation_control.h"

#include <stddef.h>
#include <string.h>

void audio_conversation_control_reset(
    audio_conversation_control_t *control)
{
    if (control != NULL) {
        memset(control, 0, sizeof(*control));
    }
}

bool audio_conversation_control_request_continue(
    audio_conversation_control_t *control)
{
    if (control == NULL || control->continue_requested ||
        control->continue_in_flight || control->end_requested) {
        return false;
    }
    control->continue_requested = true;
    control->continue_in_flight = true;
    return true;
}

bool audio_conversation_control_take_continue(
    audio_conversation_control_t *control)
{
    if (control == NULL || !control->continue_requested) {
        return false;
    }
    control->continue_requested = false;
    return true;
}

void audio_conversation_control_next_turn_started(
    audio_conversation_control_t *control)
{
    if (control != NULL) {
        control->continue_requested = false;
        control->continue_in_flight = false;
        control->end_requested = false;
    }
}

bool audio_conversation_control_request_end(
    audio_conversation_control_t *control)
{
    if (control == NULL || control->end_requested ||
        (control->continue_in_flight &&
         !control->continue_requested)) {
        return false;
    }
    control->continue_requested = false;
    control->continue_in_flight = false;
    control->end_requested = true;
    return true;
}

bool audio_conversation_control_take_end(
    audio_conversation_control_t *control)
{
    if (control == NULL || !control->end_requested) {
        return false;
    }
    control->end_requested = false;
    return true;
}
