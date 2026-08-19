#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "network_credentials.h"

static network_credentials_result_t parse(const char *form, network_credentials_t *credentials)
{
    return network_credentials_parse_form(form, strlen(form), credentials);
}

int main(void)
{
    network_credentials_t credentials = {0};
    char setup_password[NETWORK_SETUP_PASSWORD_LENGTH + 1U] = {0};
    char setup_ssid[NETWORK_SSID_MAX_LENGTH + 1U] = {0};
    char qr_payload[NETWORK_SETUP_QR_PAYLOAD_CAPACITY] = {0};
    const uint8_t mac[6] = {0x02U, 0x00U, 0x00U, 0xA1U, 0xB2U, 0xC3U};

    assert(network_setup_ssid_build("ESP32-RLCD", true, mac, setup_ssid,
                                    sizeof(setup_ssid)));
    assert(strcmp(setup_ssid, "ESP32-RLCD-A1B2C3") == 0);
    assert(network_setup_ssid_build("Desk Clock", false, NULL, setup_ssid,
                                    sizeof(setup_ssid)));
    assert(strcmp(setup_ssid, "Desk Clock") == 0);
    assert(!network_setup_ssid_build("", true, mac, setup_ssid,
                                     sizeof(setup_ssid)));
    assert(!network_setup_ssid_build("12345678901234567890123456", true, mac,
                                     setup_ssid, sizeof(setup_ssid)));
    assert(!network_setup_ssid_build("Bad\nName", false, NULL, setup_ssid,
                                     sizeof(setup_ssid)));
    assert(!network_setup_ssid_build("ESP32-RLCD", true, mac, setup_ssid, 17U));

    assert(network_setup_password_is_valid(""));
    assert(network_setup_password_is_valid("ABCDEFG2"));
    assert(!network_setup_password_is_valid("1234567"));
    assert(!network_setup_password_is_valid("bad\npass"));
    assert(!network_setup_password_is_valid(NULL));

    assert(network_setup_wifi_qr_payload("ESP32-RLCD-A1B2C3", "ABCDEF23",
                                         qr_payload, sizeof(qr_payload)));
    assert(strcmp(qr_payload,
                  "WIFI:T:WPA;S:ESP32-RLCD-A1B2C3;P:ABCDEF23;;") == 0);
    assert(network_setup_wifi_qr_payload("Lab;Guest", "A:B,C\"D\\",
                                         qr_payload, sizeof(qr_payload)));
    assert(strcmp(qr_payload,
                  "WIFI:T:WPA;S:Lab\\;Guest;P:A\\:B\\,C\\\"D\\\\;;") == 0);
    assert(!network_setup_wifi_qr_payload("ESP32-RLCD", "short", qr_payload,
                                          sizeof(qr_payload)));
    assert(!network_setup_wifi_qr_payload("ESP32-RLCD", "ABCDEFG2", qr_payload, 24U));
    assert(qr_payload[0] == '\0');

    assert(network_setup_password_from_entropy(UINT64_C(0), setup_password,
                                               sizeof(setup_password)));
    assert(strcmp(setup_password, "22222222") == 0);
    assert(network_setup_password_from_entropy(UINT64_MAX, setup_password,
                                               sizeof(setup_password)));
    assert(strcmp(setup_password, "ZZZZZZZZ") == 0);
    assert(strlen(setup_password) == NETWORK_SETUP_PASSWORD_LENGTH);
    assert(strspn(setup_password, "23456789ABCDEFGHJKLMNPQRSTUVWXYZ") ==
           NETWORK_SETUP_PASSWORD_LENGTH);
    assert(!network_setup_password_from_entropy(UINT64_C(1), setup_password,
                                                NETWORK_SETUP_PASSWORD_LENGTH));

    assert(parse("ssid=Home+WiFi&password=correct%26horse", &credentials) ==
           NETWORK_CREDENTIALS_OK);
    assert(strcmp(credentials.ssid, "Home WiFi") == 0);
    assert(strcmp(credentials.password, "correct&horse") == 0);
    assert(network_credentials_are_valid(&credentials));

    assert(parse("password=&ssid=%E5%AE%B6%E9%87%8C", &credentials) ==
           NETWORK_CREDENTIALS_OK);
    assert(strcmp(credentials.ssid, "家里") == 0);
    assert(credentials.password[0] == '\0');

    assert(parse("ssid=Home&password=12345678&ignored=value", &credentials) ==
           NETWORK_CREDENTIALS_OK);
    assert(parse("ssid=&password=12345678", &credentials) ==
           NETWORK_CREDENTIALS_INVALID_SSID);
    assert(parse("ssid=Home&password=1234567", &credentials) ==
           NETWORK_CREDENTIALS_INVALID_PASSWORD);
    assert(parse("ssid=Home", &credentials) == NETWORK_CREDENTIALS_MISSING_FIELD);
    assert(parse("ssid=One&ssid=Two&password=12345678", &credentials) ==
           NETWORK_CREDENTIALS_DUPLICATE_FIELD);
    assert(parse("ssid=Bad%GG&password=12345678", &credentials) ==
           NETWORK_CREDENTIALS_INVALID_ENCODING);
    assert(parse("ssid=Bad%00Name&password=12345678", &credentials) ==
           NETWORK_CREDENTIALS_INVALID_ENCODING);
    assert(parse("ssid=Bad%0AName&password=12345678", &credentials) ==
           NETWORK_CREDENTIALS_INVALID_SSID);
    assert(parse("ssid&password=12345678", &credentials) ==
           NETWORK_CREDENTIALS_INVALID_FORM);

    char long_form[128];
    memset(long_form, 0, sizeof(long_form));
    strcpy(long_form, "ssid=");
    memset(long_form + 5, 'A', 33U);
    strcpy(long_form + 38, "&password=12345678");
    assert(parse(long_form, &credentials) == NETWORK_CREDENTIALS_INVALID_SSID);

    memset(long_form, 0, sizeof(long_form));
    strcpy(long_form, "ssid=");
    memset(long_form + 5, 'S', 32U);
    strcpy(long_form + 37, "&password=12345678");
    assert(parse(long_form, &credentials) == NETWORK_CREDENTIALS_OK);
    assert(strlen(credentials.ssid) == NETWORK_SSID_MAX_LENGTH);

    memset(long_form, 0, sizeof(long_form));
    strcpy(long_form, "ssid=Home&password=");
    memset(long_form + 19, 'P', 63U);
    assert(parse(long_form, &credentials) == NETWORK_CREDENTIALS_OK);
    assert(strlen(credentials.password) == NETWORK_PASSWORD_MAX_LENGTH);

    memset(long_form, 0, sizeof(long_form));
    strcpy(long_form, "ssid=Home&password=");
    memset(long_form + 19, 'P', 64U);
    assert(parse(long_form, &credentials) == NETWORK_CREDENTIALS_INVALID_PASSWORD);

    memset(&credentials, 'A', sizeof(credentials));
    assert(!network_credentials_are_valid(&credentials));
    assert(strcmp(network_credentials_result_name(NETWORK_CREDENTIALS_OK), "ok") == 0);

    puts("network credential tests passed");
    return 0;
}
