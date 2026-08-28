#include <assert.h>
#include <stdio.h>

#include "voice_command_policy.h"

static void assert_decision(int command_id, bool image_available,
                            voice_command_decision_kind_t expected_kind,
                            voice_command_action_t expected_action)
{
    voice_command_decision_t decision =
        voice_command_policy_decide(command_id, image_available);
    assert(decision.kind == expected_kind);
    assert(decision.action == expected_action);
}

int main(void)
{
    assert_decision(VOICE_COMMAND_ID_HOME, false,
                    VOICE_COMMAND_DECISION_EXECUTE,
                    VOICE_COMMAND_ACTION_OPEN_HOME);
    assert_decision(VOICE_COMMAND_ID_CALENDAR, false,
                    VOICE_COMMAND_DECISION_EXECUTE,
                    VOICE_COMMAND_ACTION_OPEN_CALENDAR);
    assert_decision(VOICE_COMMAND_ID_STATUS, false,
                    VOICE_COMMAND_DECISION_EXECUTE,
                    VOICE_COMMAND_ACTION_OPEN_STATUS);
    assert_decision(VOICE_COMMAND_ID_SETTINGS, false,
                    VOICE_COMMAND_DECISION_EXECUTE,
                    VOICE_COMMAND_ACTION_OPEN_SETTINGS);

    assert_decision(VOICE_COMMAND_ID_IMAGE, true,
                    VOICE_COMMAND_DECISION_EXECUTE,
                    VOICE_COMMAND_ACTION_OPEN_IMAGE);
    assert_decision(VOICE_COMMAND_ID_IMAGE, false,
                    VOICE_COMMAND_DECISION_UNAVAILABLE,
                    VOICE_COMMAND_ACTION_NONE);

    assert_decision(VOICE_COMMAND_ID_CANCEL, false,
                    VOICE_COMMAND_DECISION_CANCEL,
                    VOICE_COMMAND_ACTION_NONE);
    assert_decision(VOICE_COMMAND_ID_NONE, false,
                    VOICE_COMMAND_DECISION_REJECTED,
                    VOICE_COMMAND_ACTION_NONE);
    assert_decision(-1, true, VOICE_COMMAND_DECISION_REJECTED,
                    VOICE_COMMAND_ACTION_NONE);
    assert_decision(VOICE_COMMAND_ID_CANCEL + 1, true,
                    VOICE_COMMAND_DECISION_REJECTED,
                    VOICE_COMMAND_ACTION_NONE);

    puts("voice command policy tests passed");
    return 0;
}
