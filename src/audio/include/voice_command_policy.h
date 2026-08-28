#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Recognition aliases are configured to emit one of these stable IDs. The
 * policy intentionally contains only navigation and cancellation commands.
 */
typedef enum {
    VOICE_COMMAND_ID_NONE = 0,
    VOICE_COMMAND_ID_HOME = 1,
    VOICE_COMMAND_ID_CALENDAR = 2,
    VOICE_COMMAND_ID_STATUS = 3,
    VOICE_COMMAND_ID_IMAGE = 4,
    VOICE_COMMAND_ID_SETTINGS = 5,
    VOICE_COMMAND_ID_CANCEL = 6,
} voice_command_id_t;

typedef enum {
    VOICE_COMMAND_ACTION_NONE = 0,
    VOICE_COMMAND_ACTION_OPEN_HOME,
    VOICE_COMMAND_ACTION_OPEN_CALENDAR,
    VOICE_COMMAND_ACTION_OPEN_STATUS,
    VOICE_COMMAND_ACTION_OPEN_IMAGE,
    VOICE_COMMAND_ACTION_OPEN_SETTINGS,
} voice_command_action_t;

typedef enum {
    VOICE_COMMAND_DECISION_REJECTED = 0,
    VOICE_COMMAND_DECISION_EXECUTE,
    VOICE_COMMAND_DECISION_UNAVAILABLE,
    VOICE_COMMAND_DECISION_CANCEL,
} voice_command_decision_kind_t;

typedef struct {
    voice_command_decision_kind_t kind;
    voice_command_action_t action;
} voice_command_decision_t;

voice_command_decision_t voice_command_policy_decide(
    int command_id, bool image_available);

#ifdef __cplusplus
}
#endif
