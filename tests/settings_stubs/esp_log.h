#pragma once
void settings_test_log(const char *tag, const char *format, ...);
#define ESP_LOGI(...) settings_test_log(__VA_ARGS__)
#define ESP_LOGW(...) settings_test_log(__VA_ARGS__)
#define ESP_LOGE(...) settings_test_log(__VA_ARGS__)
