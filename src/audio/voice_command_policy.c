#include "voice_command_policy.h"

voice_command_decision_t voice_command_policy_decide(
    int command_id, bool image_available)
{
    voice_command_decision_t decision = {
        .kind = VOICE_COMMAND_DECISION_REJECTED,
        .action = VOICE_COMMAND_ACTION_NONE,
    };

    switch (command_id) {
    case VOICE_COMMAND_ID_HOME:
        decision.kind = VOICE_COMMAND_DECISION_EXECUTE;
        decision.action = VOICE_COMMAND_ACTION_OPEN_HOME;
        break;
    case VOICE_COMMAND_ID_CALENDAR:
        decision.kind = VOICE_COMMAND_DECISION_EXECUTE;
        decision.action = VOICE_COMMAND_ACTION_OPEN_CALENDAR;
        break;
    case VOICE_COMMAND_ID_STATUS:
        decision.kind = VOICE_COMMAND_DECISION_EXECUTE;
        decision.action = VOICE_COMMAND_ACTION_OPEN_STATUS;
        break;
    case VOICE_COMMAND_ID_IMAGE:
        if (image_available) {
            decision.kind = VOICE_COMMAND_DECISION_EXECUTE;
            decision.action = VOICE_COMMAND_ACTION_OPEN_IMAGE;
        } else {
            decision.kind = VOICE_COMMAND_DECISION_UNAVAILABLE;
        }
        break;
    case VOICE_COMMAND_ID_SETTINGS:
        decision.kind = VOICE_COMMAND_DECISION_EXECUTE;
        decision.action = VOICE_COMMAND_ACTION_OPEN_SETTINGS;
        break;
    case VOICE_COMMAND_ID_CANCEL:
        decision.kind = VOICE_COMMAND_DECISION_CANCEL;
        break;
    case VOICE_COMMAND_ID_NONE:
    default:
        break;
    }

    return decision;
}
