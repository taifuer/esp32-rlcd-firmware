#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "network_credentials.h"
#include "qrcodegen.h"

static void assert_qr_encodes(const char *ssid, const char *password)
{
    char payload[NETWORK_SETUP_QR_PAYLOAD_CAPACITY] = {0};
    assert(network_setup_wifi_qr_payload(ssid, password, payload, sizeof(payload)));

    uint8_t temporary[qrcodegen_BUFFER_LEN_FOR_VERSION(10)] = {0};
    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(10)] = {0};
    assert(qrcodegen_encodeText(payload, temporary, qrcode, qrcodegen_Ecc_MEDIUM,
                                1, 10, qrcodegen_Mask_AUTO, true));
    const int module_count = qrcodegen_getSize(qrcode);
    assert(module_count >= 21);
    assert(module_count <= 57);
}

int main(void)
{
    assert_qr_encodes("ESP32-RLCD-A1B2C3", "ABCDEFG2");

    char maximum_ssid[NETWORK_SSID_MAX_LENGTH + 1U];
    char maximum_password[NETWORK_PASSWORD_MAX_LENGTH + 1U];
    memset(maximum_ssid, ':', NETWORK_SSID_MAX_LENGTH);
    maximum_ssid[NETWORK_SSID_MAX_LENGTH] = '\0';
    memset(maximum_password, ':', NETWORK_PASSWORD_MAX_LENGTH);
    maximum_password[NETWORK_PASSWORD_MAX_LENGTH] = '\0';
    assert_qr_encodes(maximum_ssid, maximum_password);

    puts("network QR tests passed");
    return 0;
}
