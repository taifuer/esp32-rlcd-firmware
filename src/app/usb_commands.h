#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t usb_commands_init(void);
void usb_commands_poll(bool rtc_available);

#ifdef __cplusplus
}
#endif
