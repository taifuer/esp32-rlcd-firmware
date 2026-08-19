#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NETWORK_SSID_MAX_LENGTH 32U
#define NETWORK_PASSWORD_MAX_LENGTH 63U
#define NETWORK_SETUP_PASSWORD_LENGTH 8U
#define NETWORK_SETUP_DEVICE_ID_LENGTH 6U
#define NETWORK_SETUP_QR_PAYLOAD_CAPACITY 224U

typedef struct {
    char ssid[NETWORK_SSID_MAX_LENGTH + 1U];
    char password[NETWORK_PASSWORD_MAX_LENGTH + 1U];
} network_credentials_t;

typedef enum {
    NETWORK_CREDENTIALS_OK = 0,
    NETWORK_CREDENTIALS_INVALID_FORM,
    NETWORK_CREDENTIALS_INVALID_ENCODING,
    NETWORK_CREDENTIALS_MISSING_FIELD,
    NETWORK_CREDENTIALS_DUPLICATE_FIELD,
    NETWORK_CREDENTIALS_INVALID_SSID,
    NETWORK_CREDENTIALS_INVALID_PASSWORD,
} network_credentials_result_t;

bool network_credentials_are_valid(const network_credentials_t *credentials);
bool network_setup_ssid_build(const char *base_name, bool append_device_id,
                              const uint8_t mac[6], char *ssid,
                              size_t ssid_capacity);
bool network_setup_password_is_valid(const char *password);
bool network_setup_password_from_entropy(uint64_t entropy, char *password,
                                         size_t password_capacity);
bool network_setup_wifi_qr_payload(const char *ssid, const char *password,
                                   char *payload, size_t payload_capacity);
network_credentials_result_t network_credentials_parse_form(
    const char *form, size_t form_length, network_credentials_t *credentials);
const char *network_credentials_result_name(network_credentials_result_t result);

#ifdef __cplusplus
}
#endif
