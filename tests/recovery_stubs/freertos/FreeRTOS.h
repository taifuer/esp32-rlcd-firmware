#pragma once

typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0U
void recovery_test_enter_critical(portMUX_TYPE *lock);
void recovery_test_exit_critical(portMUX_TYPE *lock);
#define portENTER_CRITICAL(lock) recovery_test_enter_critical(lock)
#define portEXIT_CRITICAL(lock) recovery_test_exit_critical(lock)
