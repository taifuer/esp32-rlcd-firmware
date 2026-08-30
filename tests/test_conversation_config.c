#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "conversation_config_model.h"
#include "conversation_config_record.h"

_Static_assert(
    CONVERSATION_CONFIG_FORM_MAX_LENGTH >=
        128U + 3U * CONVERSATION_API_KEY_MAX_LENGTH +
            3U * CONVERSATION_API_HOST_MAX_LENGTH,
    "conversation form capacity must cover maximum encoded valid fields");

static conversation_config_update_t make_update(
    const char *api_key, conversation_model_t model, bool enabled,
    const char *api_host)
{
    conversation_config_update_t update = {
        .service = CONVERSATION_SERVICE_ALIYUN_REALTIME,
        .model = model,
        .enabled = enabled,
    };
    if (api_key != NULL) {
        const int written = snprintf(update.api_key,
                                     sizeof(update.api_key), "%s",
                                     api_key);
        assert(written >= 0 &&
               (size_t)written < sizeof(update.api_key));
    }
    if (api_host != NULL) {
        const int written = snprintf(update.api_host,
                                     sizeof(update.api_host), "%s",
                                     api_host);
        assert(written >= 0 &&
               (size_t)written < sizeof(update.api_host));
    }
    return update;
}

static conversation_config_t make_config(const char *api_key)
{
    conversation_config_t current;
    conversation_config_defaults(&current);
    const conversation_config_update_t update = make_update(
        api_key, CONVERSATION_DEFAULT_MODEL, true, "");
    conversation_config_t config;
    assert(conversation_config_apply_update(&current, &update, &config) ==
           CONVERSATION_CONFIG_RESULT_OK);
    conversation_config_reset(&current);
    return config;
}

static bool memory_contains(const void *memory, size_t memory_size,
                            const char *needle)
{
    const uint8_t *bytes = memory;
    const size_t needle_size = strlen(needle);
    if (needle_size == 0U || needle_size > memory_size) {
        return false;
    }
    for (size_t index = 0U; index + needle_size <= memory_size; ++index) {
        if (memcmp(bytes + index, needle, needle_size) == 0) {
            return true;
        }
    }
    return false;
}

static void test_defaults_models_and_secret_free_status(void)
{
    conversation_config_t config;
    conversation_config_defaults(&config);
    assert(config.schema_version == CONVERSATION_CONFIG_SCHEMA_VERSION);
    assert(config.service == CONVERSATION_SERVICE_ALIYUN_REALTIME);
    assert(config.model == CONVERSATION_DEFAULT_MODEL);
    assert(!config.enabled);
    assert(config.api_key[0] == '\0');
    assert(config.api_host[0] == '\0');
    assert(conversation_config_validate(&config) ==
           CONVERSATION_CONFIG_RESULT_API_KEY_REQUIRED);

    assert(strcmp(conversation_model_name(
                      CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME),
                  "qwen3-omni-flash-realtime") == 0);
    assert(strcmp(conversation_model_name(
                      CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH),
                  "qwen-audio-3.0-realtime-flash") == 0);
    assert(conversation_model_name(CONVERSATION_MODEL_COUNT) == NULL);
    conversation_model_t model = CONVERSATION_MODEL_COUNT;
    assert(conversation_model_from_name(
        "qwen-audio-3.0-realtime-flash", &model));
    assert(model == CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH);
    assert(!conversation_model_from_name("qwen-uncontrolled", &model));

    conversation_config_status_t status;
    conversation_config_make_status(NULL, &status);
    assert(!status.enabled && !status.configured);
    assert(status.model == CONVERSATION_DEFAULT_MODEL);
    assert(status.shared_endpoint);
    assert(strcmp(status.api_host, CONVERSATION_DEFAULT_API_HOST) == 0);

    config = make_config("sk-secret-must-not-be-presented");
    conversation_config_make_status(&config, &status);
    assert(status.enabled && status.configured);
    assert(status.shared_endpoint);
    assert(strcmp(status.api_host, CONVERSATION_DEFAULT_API_HOST) == 0);
    assert(!memory_contains(&status, sizeof(status),
                            "sk-secret-must-not-be-presented"));
    conversation_config_reset(&config);
}

static void test_api_host_policy_and_endpoint_builder(void)
{
    static const char *const valid[] = {
        "",
        "dashscope.aliyuncs.com",
        "DASHSCOPE-INTL.ALIYUNCS.COM",
        "llm-abc123.cn-beijing.maas.aliyuncs.com",
        "workspace-7.ap-southeast-1.maas.aliyuncs.com",
        "A.CN-BEIJING.MAAS.ALIYUNCS.COM",
    };
    for (size_t index = 0U; index < sizeof(valid) / sizeof(valid[0]);
         ++index) {
        assert(conversation_api_host_is_allowed(valid[index]));
    }

    static const char *const invalid[] = {
        "https://dashscope.aliyuncs.com",
        "dashscope.aliyuncs.com/api-ws/v1/realtime",
        "dashscope.aliyuncs.com:443",
        "key@dashscope.aliyuncs.com",
        "dashscope.aliyuncs.com.evil.invalid",
        "evil.dashscope.aliyuncs.com",
        "127.0.0.1",
        "[::1]",
        ".cn-beijing.maas.aliyuncs.com",
        "-bad.cn-beijing.maas.aliyuncs.com",
        "bad-.cn-beijing.maas.aliyuncs.com",
        "two.labels.cn-beijing.maas.aliyuncs.com",
        "bad_label.cn-beijing.maas.aliyuncs.com",
        "workspace.us-east-1.maas.aliyuncs.com",
    };
    for (size_t index = 0U;
         index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        assert(!conversation_api_host_is_allowed(invalid[index]));
    }
    assert(!conversation_api_host_is_allowed(NULL));

    char endpoint[CONVERSATION_ENDPOINT_MAX_LENGTH + 1U];
    assert(conversation_config_build_endpoint(
        "", CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME,
        endpoint, sizeof(endpoint)));
    assert(strcmp(
               endpoint,
               "wss://dashscope.aliyuncs.com/api-ws/v1/realtime?"
               "model=qwen3-omni-flash-realtime") == 0);
    assert(conversation_config_build_endpoint(
        "llm-demo.ap-southeast-1.maas.aliyuncs.com",
        CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH,
        endpoint, sizeof(endpoint)));
    assert(strcmp(
               endpoint,
               "wss://llm-demo.ap-southeast-1.maas.aliyuncs.com/"
               "api-ws/v1/realtime?"
               "model=qwen-audio-3.0-realtime-flash") == 0);
    assert(!conversation_config_build_endpoint(
        "example.com", CONVERSATION_DEFAULT_MODEL, endpoint,
        sizeof(endpoint)));
    assert(endpoint[0] == '\0');
}

static void test_update_preserves_key_and_controls_model(void)
{
    conversation_config_t current;
    conversation_config_defaults(&current);
    conversation_config_t candidate;
    conversation_config_update_t update = make_update(
        "", CONVERSATION_DEFAULT_MODEL, true, "");
    assert(conversation_config_apply_update(&current, &update, &candidate) ==
           CONVERSATION_CONFIG_RESULT_API_KEY_REQUIRED);

    update = make_update("sk-new-value", CONVERSATION_DEFAULT_MODEL,
                         true, "");
    assert(conversation_config_apply_update(&current, &update, &candidate) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(candidate.enabled);
    assert(strcmp(candidate.api_key, "sk-new-value") == 0);

    conversation_config_update_t replacement = make_update(
        "", CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH, false,
        "LLM-DEMO.CN-BEIJING.MAAS.ALIYUNCS.COM");
    conversation_config_t replaced;
    assert(conversation_config_apply_update(&candidate, &replacement,
                                            &replaced) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(!replaced.enabled);
    assert(replaced.model ==
           CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH);
    assert(strcmp(replaced.api_key, "sk-new-value") == 0);
    assert(strcmp(replaced.api_host,
                  "llm-demo.cn-beijing.maas.aliyuncs.com") == 0);

    replacement = make_update("key with spaces", CONVERSATION_DEFAULT_MODEL,
                              true, "");
    assert(conversation_config_apply_update(&candidate, &replacement,
                                            &replaced) ==
           CONVERSATION_CONFIG_RESULT_INVALID_API_KEY);
    replacement = make_update("sk-valid", CONVERSATION_MODEL_COUNT, true,
                              "");
    assert(conversation_config_apply_update(&candidate, &replacement,
                                            &replaced) ==
           CONVERSATION_CONFIG_RESULT_INVALID_MODEL);
    replacement = make_update("sk-valid", CONVERSATION_DEFAULT_MODEL, true,
                              "example.com");
    assert(conversation_config_apply_update(&candidate, &replacement,
                                            &replaced) ==
           CONVERSATION_CONFIG_RESULT_INVALID_API_HOST);
    replacement = make_update("sk-valid", CONVERSATION_DEFAULT_MODEL, true,
                              "");
    replacement.service = CONVERSATION_SERVICE_COUNT;
    assert(conversation_config_apply_update(&candidate, &replacement,
                                            &replaced) ==
           CONVERSATION_CONFIG_RESULT_UNSUPPORTED_SERVICE);

    conversation_config_reset(&current);
    conversation_config_reset(&candidate);
    conversation_config_reset(&replaced);
}

static void test_bounds_and_clear(void)
{
    conversation_config_t current = make_config("sk-original-secret");
    conversation_config_update_t update = make_update(
        "sk-valid", CONVERSATION_DEFAULT_MODEL, true, "");
    memset(update.api_key, 'K', sizeof(update.api_key));
    conversation_config_t candidate;
    assert(conversation_config_apply_update(&current, &update, &candidate) ==
           CONVERSATION_CONFIG_RESULT_INVALID_API_KEY);

    update = make_update("sk-valid", CONVERSATION_DEFAULT_MODEL, true, "");
    memset(update.api_host, 'h', sizeof(update.api_host));
    assert(conversation_config_apply_update(&current, &update, &candidate) ==
           CONVERSATION_CONFIG_RESULT_INVALID_API_HOST);

    conversation_config_reset(&current);
    assert(current.api_key[0] == '\0');
    assert(current.api_host[0] == '\0');
    assert(!memory_contains(&current, sizeof(current),
                            "sk-original-secret"));
    conversation_config_reset(&candidate);
}

static void test_portal_form_parser(void)
{
    static const char valid_form[] =
        "service=aliyun_realtime&enabled=on&"
        "model=qwen3-omni-flash-realtime&api_host=&"
        "api_key=sk-test%2Bvalue";
    conversation_config_update_t update;
    assert(conversation_config_parse_form(valid_form, strlen(valid_form),
                                          &update) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(update.service == CONVERSATION_SERVICE_ALIYUN_REALTIME);
    assert(update.model == CONVERSATION_DEFAULT_MODEL);
    assert(update.enabled);
    assert(update.api_host[0] == '\0');
    assert(strcmp(update.api_key, "sk-test+value") == 0);

    static const char preserve_form[] =
        "api_key=&api_host=llm-demo.cn-beijing.maas.aliyuncs.com&"
        "model=qwen-audio-3.0-realtime-flash&enabled=off&"
        "service=aliyun_realtime";
    assert(conversation_config_parse_form(
               preserve_form, strlen(preserve_form), &update) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(!update.enabled && update.api_key[0] == '\0');
    conversation_config_t current = make_config("sk-preserved");
    conversation_config_t candidate;
    assert(conversation_config_apply_update(&current, &update, &candidate) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(strcmp(candidate.api_key, "sk-preserved") == 0);

    static const char duplicate_form[] =
        "service=aliyun_realtime&service=aliyun_realtime&enabled=on&"
        "model=qwen3-omni-flash-realtime&api_host=&api_key=k";
    assert(conversation_config_parse_form(
               duplicate_form, strlen(duplicate_form), &update) ==
           CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD);

    static const char missing_form[] =
        "service=aliyun_realtime&enabled=on&"
        "model=qwen3-omni-flash-realtime&api_host=";
    assert(conversation_config_parse_form(missing_form,
                                          strlen(missing_form), &update) ==
           CONVERSATION_CONFIG_RESULT_MISSING_FIELD);

    static const char unexpected_form[] =
        "service=aliyun_realtime&enabled=on&"
        "model=qwen3-omni-flash-realtime&api_host=&api_key=k&"
        "endpoint=wss%3A%2F%2Fevil.invalid";
    assert(conversation_config_parse_form(
               unexpected_form, strlen(unexpected_form), &update) ==
           CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD);

    static const char bad_encoding_form[] =
        "service=aliyun_realtime&enabled=on&"
        "model=qwen3-omni-flash-realtime&api_host=%GG&api_key=k";
    assert(conversation_config_parse_form(
               bad_encoding_form, strlen(bad_encoding_form), &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_ENCODING);

    static const char nul_encoding_form[] =
        "service=aliyun_realtime&enabled=on&"
        "model=qwen3-omni-flash-realtime&api_host=&api_key=sk%00hidden";
    assert(conversation_config_parse_form(
               nul_encoding_form, strlen(nul_encoding_form), &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_ENCODING);

    static const char wrong_service_form[] =
        "service=other&enabled=on&model=qwen3-omni-flash-realtime&"
        "api_host=&api_key=k";
    assert(conversation_config_parse_form(
               wrong_service_form, strlen(wrong_service_form), &update) ==
           CONVERSATION_CONFIG_RESULT_UNSUPPORTED_SERVICE);

    static const char wrong_model_form[] =
        "service=aliyun_realtime&enabled=on&model=qwen-uncontrolled&"
        "api_host=&api_key=k";
    assert(conversation_config_parse_form(
               wrong_model_form, strlen(wrong_model_form), &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_MODEL);

    static const char wrong_host_form[] =
        "service=aliyun_realtime&enabled=on&"
        "model=qwen3-omni-flash-realtime&api_host=example.com&api_key=k";
    assert(conversation_config_parse_form(
               wrong_host_form, strlen(wrong_host_form), &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_API_HOST);

    static const char wrong_enabled_form[] =
        "service=aliyun_realtime&enabled=yes&"
        "model=qwen3-omni-flash-realtime&api_host=&api_key=k";
    assert(conversation_config_parse_form(
               wrong_enabled_form, strlen(wrong_enabled_form), &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_FORM);

    char encoded_key_form[CONVERSATION_CONFIG_FORM_MAX_LENGTH + 1U] = {0};
    const int prefix = snprintf(
        encoded_key_form, sizeof(encoded_key_form),
        "service=aliyun_realtime&enabled=on&"
        "model=qwen3-omni-flash-realtime&api_host=&api_key=");
    assert(prefix > 0 && (size_t)prefix < sizeof(encoded_key_form));
    size_t encoded_length = (size_t)prefix;
    for (size_t index = 0U; index < CONVERSATION_API_KEY_MAX_LENGTH;
         ++index) {
        assert(encoded_length + 3U < sizeof(encoded_key_form));
        memcpy(encoded_key_form + encoded_length, "%3D", 3U);
        encoded_length += 3U;
    }
    encoded_key_form[encoded_length] = '\0';
    assert(conversation_config_parse_form(
               encoded_key_form, encoded_length, &update) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(strlen(update.api_key) == CONVERSATION_API_KEY_MAX_LENGTH);

    char oversized[CONVERSATION_CONFIG_FORM_MAX_LENGTH + 1U];
    memset(oversized, 'x', sizeof(oversized));
    memset(&update, 0x5a, sizeof(update));
    assert(conversation_config_parse_form(oversized, sizeof(oversized),
                                          &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_FORM);
    const uint8_t *cleared = (const uint8_t *)&update;
    for (size_t index = 0U; index < sizeof(update); ++index) {
        assert(cleared[index] == 0U);
    }

    conversation_config_reset(&candidate);
    conversation_config_reset(&current);
    conversation_config_clear_sensitive(&update, sizeof(update));
}

static void assert_configs_equal(const conversation_config_t *left,
                                 const conversation_config_t *right)
{
    assert(left->schema_version == right->schema_version);
    assert(left->service == right->service);
    assert(left->model == right->model);
    assert(left->enabled == right->enabled);
    assert(strcmp(left->api_key, right->api_key) == 0);
    assert(strcmp(left->api_host, right->api_host) == 0);
}

static void test_record_round_trip_and_integrity(void)
{
    conversation_config_t base = make_config("sk-record-secret");
    const conversation_config_update_t update = make_update(
        "", CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH, false,
        "llm-demo.cn-beijing.maas.aliyuncs.com");
    conversation_config_t config;
    assert(conversation_config_apply_update(&base, &update, &config) ==
           CONVERSATION_CONFIG_RESULT_OK);
    conversation_config_reset(&base);

    uint8_t encoded[CONVERSATION_CONFIG_RECORD_ENCODED_SIZE];
    assert(conversation_config_record_encode(
        UINT32_C(0x78563412), &config, encoded, sizeof(encoded)));
    assert(encoded[0] == 'R' && encoded[1] == 'L' &&
           encoded[2] == 'C' && encoded[3] == 'V');
    assert(encoded[8] == 0x12U && encoded[9] == 0x34U &&
           encoded[10] == 0x56U && encoded[11] == 0x78U);

    conversation_config_record_t record = {0};
    assert(conversation_config_record_decode(encoded, sizeof(encoded),
                                             &record));
    assert(record.generation == UINT32_C(0x78563412));
    assert_configs_equal(&record.config, &config);

    encoded[40] ^= 1U;
    assert(!conversation_config_record_decode(encoded, sizeof(encoded),
                                              &record));
    encoded[40] ^= 1U;
    assert(!conversation_config_record_decode(encoded,
                                              sizeof(encoded) - 1U,
                                              &record));
    assert(!conversation_config_record_decode(NULL, sizeof(encoded),
                                              &record));
    assert(!conversation_config_record_encode(1U, NULL, encoded,
                                              sizeof(encoded)));

    conversation_config_clear_sensitive(encoded, sizeof(encoded));
    conversation_config_reset(&record.config);
    conversation_config_reset(&config);
}

static void test_record_selection_handles_generation_wrap(void)
{
    conversation_config_record_t slot_a = {.generation = 10U};
    conversation_config_record_t slot_b = {.generation = 11U};
    assert(conversation_config_record_select_latest(&slot_a, &slot_b) ==
           CONVERSATION_CONFIG_RECORD_SLOT_B);
    assert(conversation_config_record_select_latest(&slot_a, NULL) ==
           CONVERSATION_CONFIG_RECORD_SLOT_A);
    assert(conversation_config_record_select_latest(NULL, &slot_b) ==
           CONVERSATION_CONFIG_RECORD_SLOT_B);
    assert(conversation_config_record_select_latest(NULL, NULL) ==
           CONVERSATION_CONFIG_RECORD_SLOT_NONE);

    slot_a.generation = UINT32_MAX;
    slot_b.generation = 0U;
    assert(conversation_config_record_select_latest(&slot_a, &slot_b) ==
           CONVERSATION_CONFIG_RECORD_SLOT_B);
}

int main(void)
{
    test_defaults_models_and_secret_free_status();
    test_api_host_policy_and_endpoint_builder();
    test_update_preserves_key_and_controls_model();
    test_bounds_and_clear();
    test_portal_form_parser();
    test_record_round_trip_and_integrity();
    test_record_selection_handles_generation_wrap();
    puts("conversation config tests passed");
    return 0;
}
