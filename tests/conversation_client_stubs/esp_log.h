#pragma once
void conversation_client_test_log(const char *tag, const char *format, ...);
#define ESP_LOGE(tag, ...) conversation_client_test_log(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) conversation_client_test_log(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) conversation_client_test_log(tag, __VA_ARGS__)
