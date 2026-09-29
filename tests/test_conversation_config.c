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
    assert(config.voice == CONVERSATION_DEFAULT_VOICE);
    assert(config.reply_style == CONVERSATION_DEFAULT_REPLY_STYLE);
    assert(config.model == CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH);
    assert(CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME == 0);
    assert(CONVERSATION_MODEL_QWEN_AUDIO_3_0_REALTIME_FLASH == 1);
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
    assert(!conversation_model_is_supported(
        CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME));
    assert(!conversation_model_from_name("qwen3-omni-flash-realtime", &model));
    assert(!conversation_model_from_name("qwen-uncontrolled", &model));

    conversation_config_status_t status;
    conversation_config_make_status(NULL, &status);
    assert(!status.enabled && !status.configured);
    assert(status.migration == CONVERSATION_CONFIG_MIGRATION_NONE);
    assert(status.generation == 0U);
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

static void test_retired_model_cannot_be_used_or_written(void)
{
    conversation_config_t config = make_config("sk-legacy-runtime");
    config.model = CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME;
    assert(conversation_config_validate(&config) ==
           CONVERSATION_CONFIG_RESULT_INVALID_MODEL);
    uint8_t encoded[CONVERSATION_CONFIG_RECORD_ENCODED_SIZE];
    memset(encoded, 0x5a, sizeof(encoded));
    assert(!conversation_config_record_encode(1U, &config, encoded,
                                              sizeof(encoded)));
    for (size_t index = 0U; index < sizeof(encoded); ++index) {
        assert(encoded[index] == 0U);
    }
    conversation_config_status_t status;
    conversation_config_make_status(&config, &status);
    assert(!status.configured);
    assert(!memory_contains(&status, sizeof(status), "sk-legacy-runtime"));
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
    assert(!conversation_config_build_endpoint(
        "", CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME,
        endpoint, sizeof(endpoint)));
    assert(endpoint[0] == '\0');
    assert(conversation_config_build_endpoint(
        "", CONVERSATION_DEFAULT_MODEL,
        endpoint, sizeof(endpoint)));
    assert(strcmp(
               endpoint,
               "wss://dashscope.aliyuncs.com/api-ws/v1/realtime?"
               "model=qwen-audio-3.0-realtime-flash") == 0);
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
    replacement = make_update("sk-valid",
        CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME, true, "");
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
        "model=qwen-audio-3.0-realtime-flash&api_host=&"
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
        "model=qwen-audio-3.0-realtime-flash&api_host=&api_key=k";
    assert(conversation_config_parse_form(
               duplicate_form, strlen(duplicate_form), &update) ==
           CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD);

    static const char missing_form[] =
        "service=aliyun_realtime&enabled=on&"
        "model=qwen-audio-3.0-realtime-flash&api_host=";
    assert(conversation_config_parse_form(missing_form,
                                          strlen(missing_form), &update) ==
           CONVERSATION_CONFIG_RESULT_MISSING_FIELD);

    static const char unexpected_form[] =
        "service=aliyun_realtime&enabled=on&"
        "model=qwen-audio-3.0-realtime-flash&api_host=&api_key=k&"
        "endpoint=wss%3A%2F%2Fevil.invalid";
    assert(conversation_config_parse_form(
               unexpected_form, strlen(unexpected_form), &update) ==
           CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD);

    static const char bad_encoding_form[] =
        "service=aliyun_realtime&enabled=on&"
        "model=qwen-audio-3.0-realtime-flash&api_host=%GG&api_key=k";
    assert(conversation_config_parse_form(
               bad_encoding_form, strlen(bad_encoding_form), &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_ENCODING);

    static const char nul_encoding_form[] =
        "service=aliyun_realtime&enabled=on&"
        "model=qwen-audio-3.0-realtime-flash&api_host=&api_key=sk%00hidden";
    assert(conversation_config_parse_form(
               nul_encoding_form, strlen(nul_encoding_form), &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_ENCODING);

    static const char wrong_service_form[] =
        "service=other&enabled=on&model=qwen-audio-3.0-realtime-flash&"
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

    static const char retired_model_form[] =
        "service=aliyun_realtime&enabled=on&model=qwen3-omni-flash-realtime&"
        "api_host=&api_key=k";
    assert(conversation_config_parse_form(
               retired_model_form, strlen(retired_model_form), &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_MODEL);

    static const char wrong_host_form[] =
        "service=aliyun_realtime&enabled=on&"
        "model=qwen-audio-3.0-realtime-flash&api_host=example.com&api_key=k";
    assert(conversation_config_parse_form(
               wrong_host_form, strlen(wrong_host_form), &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_API_HOST);

    static const char wrong_enabled_form[] =
        "service=aliyun_realtime&enabled=yes&"
        "model=qwen-audio-3.0-realtime-flash&api_host=&api_key=k";
    assert(conversation_config_parse_form(
               wrong_enabled_form, strlen(wrong_enabled_form), &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_FORM);

    char encoded_key_form[CONVERSATION_CONFIG_FORM_MAX_LENGTH + 1U] = {0};
    const int prefix = snprintf(
        encoded_key_form, sizeof(encoded_key_form),
        "service=aliyun_realtime&enabled=on&"
        "model=qwen-audio-3.0-realtime-flash&api_host=&api_key=");
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
    assert(left->voice == right->voice);
    assert(left->reply_style == right->reply_style);
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

static void test_voice_and_reply_style_presets(void)
{
    conversation_config_t current = make_config("sk-options-preserve");
    conversation_config_t candidate;
    conversation_config_update_t update = make_update(
        "", CONVERSATION_DEFAULT_MODEL, true, "");
    for (unsigned voice = 0U; voice < CONVERSATION_VOICE_COUNT; ++voice) {
        conversation_voice_t parsed_voice = CONVERSATION_VOICE_COUNT;
        assert(conversation_voice_from_name(
            conversation_voice_name((conversation_voice_t)voice), &parsed_voice));
        assert((unsigned)parsed_voice == voice);
        for (unsigned style = 0U; style < CONVERSATION_REPLY_STYLE_COUNT; ++style) {
            conversation_reply_style_t parsed_style = CONVERSATION_REPLY_STYLE_COUNT;
            assert(conversation_reply_style_from_name(
                conversation_reply_style_name((conversation_reply_style_t)style),
                &parsed_style));
            assert((unsigned)parsed_style == style);
            update.voice = parsed_voice;
            update.reply_style = parsed_style;
            update.has_voice = true;
            update.has_reply_style = true;
            assert(conversation_config_apply_update(&current, &update, &candidate) ==
                   CONVERSATION_CONFIG_RESULT_OK);
            assert(candidate.voice == parsed_voice && candidate.reply_style == parsed_style);
            assert(strcmp(candidate.api_key, current.api_key) == 0);
            uint8_t encoded[CONVERSATION_CONFIG_RECORD_ENCODED_SIZE];
            conversation_config_record_t decoded;
            assert(conversation_config_record_encode(12U, &candidate, encoded, sizeof(encoded)));
            assert(conversation_config_record_decode(encoded, sizeof(encoded), &decoded));
            assert_configs_equal(&candidate, &decoded.config);
            conversation_config_status_t status;
            conversation_config_make_status(&candidate, &status);
            assert(status.voice == parsed_voice && status.reply_style == parsed_style);
            assert(!memory_contains(&status, sizeof(status), current.api_key));
            conversation_config_clear_sensitive(&decoded, sizeof(decoded));
            conversation_config_clear_sensitive(encoded, sizeof(encoded));
        }
    }
    current.voice = CONVERSATION_VOICE_LONGANLUFENG;
    current.reply_style = CONVERSATION_REPLY_STYLE_ENGLISH_PRACTICE;
    update.has_voice = false;
    update.has_reply_style = false;
    update.voice = CONVERSATION_VOICE_COUNT; /* omitted values are ignored */
    update.reply_style = CONVERSATION_REPLY_STYLE_COUNT;
    assert(conversation_config_apply_update(&current, &update, &candidate) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert_configs_equal(&current, &candidate);
    update.has_voice = true;
    assert(conversation_config_apply_update(&current, &update, &candidate) ==
           CONVERSATION_CONFIG_RESULT_INVALID_VOICE);
    update.voice = CONVERSATION_DEFAULT_VOICE;
    update.has_reply_style = true;
    assert(conversation_config_apply_update(&current, &update, &candidate) ==
           CONVERSATION_CONFIG_RESULT_INVALID_REPLY_STYLE);
    assert(conversation_voice_name((conversation_voice_t)-1) == NULL);
    assert(conversation_voice_name(CONVERSATION_VOICE_COUNT) == NULL);
    assert(conversation_reply_style_name((conversation_reply_style_t)-1) == NULL);
    assert(!conversation_voice_from_name("longanqian_v3.1", &update.voice));
    assert(!conversation_voice_from_name("custom-voice", &update.voice));
    assert(!conversation_reply_style_from_name("custom-prompt", &update.reply_style));

    static const char base[] = "service=aliyun_realtime&enabled=on&"
        "model=qwen-audio-3.0-realtime-flash&api_host=&api_key=";
    const struct { const char *suffix; conversation_config_result_t result; } cases[] = {
        {"&voice=longanlingxi&reply_style=english_practice", CONVERSATION_CONFIG_RESULT_OK},
        {"&voice=longanlingxin", CONVERSATION_CONFIG_RESULT_OK},
        {"&reply_style=concise", CONVERSATION_CONFIG_RESULT_OK},
        {"&voice=longanqian_v3.1", CONVERSATION_CONFIG_RESULT_INVALID_VOICE},
        {"&voice=", CONVERSATION_CONFIG_RESULT_INVALID_VOICE},
        {"&voice=longanqian&voice=longanlingxi", CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD},
        {"&reply_style=concise&reply_style=concise", CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD},
        {"&reply_style=custom", CONVERSATION_CONFIG_RESULT_INVALID_REPLY_STYLE},
        {"&reply_style=", CONVERSATION_CONFIG_RESULT_INVALID_REPLY_STYLE},
        {"&voice=%00longanqian", CONVERSATION_CONFIG_RESULT_INVALID_ENCODING},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char form[512];
        const int size = snprintf(form, sizeof(form), "%s%s", base, cases[index].suffix);
        assert(size > 0 && (size_t)size < sizeof(form));
        assert(conversation_config_parse_form(form, (size_t)size, &update) == cases[index].result);
        if (cases[index].result == CONVERSATION_CONFIG_RESULT_OK) {
            assert(conversation_config_apply_update(&current, &update, &candidate) ==
                   CONVERSATION_CONFIG_RESULT_OK);
            assert(strcmp(candidate.api_key, current.api_key) == 0);
            if (!update.has_voice) assert(candidate.voice == current.voice);
            if (!update.has_reply_style) assert(candidate.reply_style == current.reply_style);
        } else {
            const uint8_t *bytes = (const uint8_t *)&update;
            for (size_t byte = 0U; byte < sizeof(update); ++byte) assert(bytes[byte] == 0U);
        }
    }
    conversation_config_reset(&current);
    conversation_config_reset(&candidate);
    conversation_config_clear_sensitive(&update, sizeof(update));
}

static void test_preferences_route_has_no_credential_fields(void)
{
    static const char valid[] =
        "enabled=on&voice=longanlingxi&reply_style=english_practice";
    conversation_preferences_t preferences;
    assert(conversation_config_parse_preferences_form(valid, strlen(valid), &preferences) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(preferences.enabled && preferences.voice == CONVERSATION_VOICE_LONGANLINGXI);
    assert(preferences.reply_style == CONVERSATION_REPLY_STYLE_ENGLISH_PRACTICE);
    assert(!preferences.has_model && !preferences.has_api_host);
    conversation_config_t current = make_config("sk-preferences-preserved");
    snprintf(current.api_host, sizeof(current.api_host), "%s",
             "workspace.ap-southeast-1.maas.aliyuncs.com");
    conversation_config_t candidate;
    assert(conversation_config_apply_preferences(&current, &preferences, &candidate) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(candidate.schema_version == current.schema_version);
    assert(candidate.service == current.service && candidate.model == current.model);
    assert(strcmp(candidate.api_key, current.api_key) == 0);
    assert(strcmp(candidate.api_host, current.api_host) == 0);
    assert(candidate.enabled == preferences.enabled && candidate.voice == preferences.voice);
    assert(candidate.reply_style == preferences.reply_style);
    preferences.enabled = false;
    assert(conversation_config_apply_preferences(&candidate, &preferences, &candidate) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(!candidate.enabled && strcmp(candidate.api_key, current.api_key) == 0);
    assert(strcmp(candidate.api_host, current.api_host) == 0);
    preferences.voice = CONVERSATION_VOICE_COUNT;
    assert(conversation_config_apply_preferences(&current, &preferences, &candidate) ==
           CONVERSATION_CONFIG_RESULT_INVALID_VOICE);
    assert(candidate.api_key[0] == '\0' && strcmp(current.api_key, "sk-preferences-preserved") == 0);
    preferences.voice = CONVERSATION_DEFAULT_VOICE;
    preferences.reply_style = CONVERSATION_REPLY_STYLE_COUNT;
    assert(conversation_config_apply_preferences(&current, &preferences, &candidate) ==
           CONVERSATION_CONFIG_RESULT_INVALID_REPLY_STYLE);
    preferences.reply_style = CONVERSATION_DEFAULT_REPLY_STYLE;
    conversation_config_reset(&candidate);
    assert(conversation_config_apply_preferences(&candidate, &preferences, &candidate) ==
           CONVERSATION_CONFIG_RESULT_API_KEY_REQUIRED);
    assert(!candidate.enabled && candidate.api_key[0] == '\0');

    const struct { const char *suffix; conversation_config_result_t result; } extras[] = {
        {"&api_key=", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"&api_key=sk-do-not-accept", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"&api_host=", CONVERSATION_CONFIG_RESULT_MISSING_FIELD},
        {"&model=qwen-audio-3.0-realtime-flash", CONVERSATION_CONFIG_RESULT_MISSING_FIELD},
        {"&service=aliyun_realtime", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"&api%5Fkey=", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"&%61pi_host=", CONVERSATION_CONFIG_RESULT_MISSING_FIELD},
        {"&unknown=1", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"&enabled=off", CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD},
        {"&%76oice=longanqian", CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD},
        {"&reply_style=concise", CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD},
        {"&", CONVERSATION_CONFIG_RESULT_INVALID_FORM},
        {"&&other=1", CONVERSATION_CONFIG_RESULT_INVALID_FORM},
    };
    for (size_t i = 0U; i < sizeof(extras) / sizeof(extras[0]); ++i) {
        char form[CONVERSATION_PREFERENCES_FORM_MAX_LENGTH + 1U];
        const int length = snprintf(form, sizeof(form), "%s%s", valid, extras[i].suffix);
        assert(length > 0 && (size_t)length < sizeof(form));
        memset(&preferences, 0xa5, sizeof(preferences));
        assert(conversation_config_parse_preferences_form(form, (size_t)length, &preferences) ==
               extras[i].result);
        const uint8_t *bytes = (const uint8_t *)&preferences;
        for (size_t j = 0U; j < sizeof(preferences); ++j) assert(bytes[j] == 0U);
    }
    const struct { const char *form; conversation_config_result_t result; } invalid[] = {
        {"", CONVERSATION_CONFIG_RESULT_INVALID_FORM},
        {"enabled=on&voice=longanqian", CONVERSATION_CONFIG_RESULT_MISSING_FIELD},
        {"enabled=on&reply_style=concise", CONVERSATION_CONFIG_RESULT_MISSING_FIELD},
        {"voice=longanqian&reply_style=concise", CONVERSATION_CONFIG_RESULT_MISSING_FIELD},
        {"enabled=yes&voice=longanqian&reply_style=concise", CONVERSATION_CONFIG_RESULT_INVALID_FORM},
        {"enabled=off&voice=longanqian_v3.1&reply_style=concise", CONVERSATION_CONFIG_RESULT_INVALID_VOICE},
        {"enabled=off&voice=longanqian&reply_style=custom", CONVERSATION_CONFIG_RESULT_INVALID_REPLY_STYLE},
        {"enabled=on&voice=longanqian%00&reply_style=concise", CONVERSATION_CONFIG_RESULT_INVALID_ENCODING},
        {"enabled=on&voice=longanqian&reply_style=%ZZ", CONVERSATION_CONFIG_RESULT_INVALID_ENCODING},
        {"enabled=on&voice=longanqian&reply_style=%", CONVERSATION_CONFIG_RESULT_INVALID_ENCODING},
        {"enabled=on&voice=longanqian&reply_style=", CONVERSATION_CONFIG_RESULT_INVALID_REPLY_STYLE},
        {"enabled=on&voice=longanqian&reply_style", CONVERSATION_CONFIG_RESULT_INVALID_FORM},
        {"enabled=on&voice=longanqian&=concise", CONVERSATION_CONFIG_RESULT_INVALID_FORM},
    };
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(conversation_config_parse_preferences_form(invalid[i].form,
            strlen(invalid[i].form), &preferences) == invalid[i].result);
    }
    static const char encoded[] = "%65nabled=%6Fn&reply%5Fstyle=concise&voice=longanqian";
    assert(conversation_config_parse_preferences_form(encoded, strlen(encoded), &preferences) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(preferences.enabled && preferences.voice == CONVERSATION_DEFAULT_VOICE);
    assert(preferences.reply_style == CONVERSATION_DEFAULT_REPLY_STYLE);
    char oversized[CONVERSATION_PREFERENCES_FORM_MAX_LENGTH + 1U];
    memset(oversized, 'x', sizeof(oversized));
    assert(conversation_config_parse_preferences_form(oversized, sizeof(oversized), &preferences) ==
           CONVERSATION_CONFIG_RESULT_INVALID_FORM);
    assert(conversation_config_parse_preferences_form(NULL, 1U, &preferences) ==
           CONVERSATION_CONFIG_RESULT_INVALID_FORM);
    assert(conversation_config_parse_preferences_form(valid, strlen(valid), NULL) ==
           CONVERSATION_CONFIG_RESULT_INVALID_ARGUMENT);
    conversation_config_reset(&current);
    conversation_config_reset(&candidate);
}

static void test_preferences_model_and_host_are_controlled_together(void)
{
    static const char valid[] =
        "enabled=on&voice=longanlingxi&reply_style=english_practice&"
        "model=qwen-audio-3.0-realtime-flash&api_host=WORKSPACE.CN-BEIJING.MAAS.ALIYUNCS.COM";
    conversation_preferences_t preferences;
    assert(conversation_config_parse_preferences_form(valid, strlen(valid), &preferences) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(preferences.has_model && preferences.has_api_host);
    assert(preferences.model == CONVERSATION_DEFAULT_MODEL);
    assert(strcmp(preferences.api_host, "workspace.cn-beijing.maas.aliyuncs.com") == 0);
    conversation_config_t current = make_config("sk-preserved-new-preferences");
    conversation_config_t candidate;
    current.enabled = false;
    assert(conversation_config_apply_preferences(&current, &preferences, &candidate) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(candidate.enabled && candidate.model == CONVERSATION_DEFAULT_MODEL);
    assert(candidate.voice == preferences.voice && candidate.reply_style == preferences.reply_style);
    assert(strcmp(candidate.api_host, preferences.api_host) == 0);
    assert(strcmp(candidate.api_key, current.api_key) == 0);
    assert(candidate.service == current.service && candidate.schema_version == current.schema_version);

    const struct { const char *suffix; conversation_config_result_t result; } bad[] = {
        {"&api_key=sk-forbidden", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"&api%5fkey=", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"&service=aliyun_realtime", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"&unknown=x", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"&model=qwen-audio-3.0-realtime-flash", CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD},
        {"&%6Dodel=qwen-audio-3.0-realtime-flash", CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD},
        {"&api_host=", CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD},
        {"&api%5Fhost=dashscope.aliyuncs.com", CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD},
    };
    for (size_t i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char form[CONVERSATION_PREFERENCES_FORM_MAX_LENGTH + 1U];
        const int count = snprintf(form, sizeof(form), "%s%s", valid, bad[i].suffix);
        assert(count > 0 && (size_t)count < sizeof(form));
        memset(&preferences, 0xa5, sizeof(preferences));
        assert(conversation_config_parse_preferences_form(form, (size_t)count, &preferences) ==
               bad[i].result);
        const uint8_t *bytes = (const uint8_t *)&preferences;
        for (size_t j = 0U; j < sizeof(preferences); ++j) assert(bytes[j] == 0U);
    }
    const struct { const char *field; conversation_config_result_t result; } invalid[] = {
        {"model=qwen3-omni-flash-realtime&api_host=", CONVERSATION_CONFIG_RESULT_INVALID_MODEL},
        {"model=qwen-audio-3.1-realtime&api_host=", CONVERSATION_CONFIG_RESULT_INVALID_MODEL},
        {"model=&api_host=", CONVERSATION_CONFIG_RESULT_INVALID_MODEL},
        {"model=qwen-audio-3.0-realtime-flash&api_host=https%3A%2F%2Fdashscope.aliyuncs.com", CONVERSATION_CONFIG_RESULT_INVALID_API_HOST},
        {"model=qwen-audio-3.0-realtime-flash&api_host=dashscope.aliyuncs.com.evil.example", CONVERSATION_CONFIG_RESULT_INVALID_API_HOST},
        {"model=qwen-audio-3.0-realtime-flash&api_host=a.b.cn-beijing.maas.aliyuncs.com", CONVERSATION_CONFIG_RESULT_INVALID_API_HOST},
        {"model=qwen-audio-3.0-realtime-flash&api_host=dashscope.aliyuncs.com%00.evil.example", CONVERSATION_CONFIG_RESULT_INVALID_ENCODING},
        {"model=qwen-audio-3.0-realtime-flash&api_host=%ZZ", CONVERSATION_CONFIG_RESULT_INVALID_ENCODING},
        {"model=qwen-audio-3.0-realtime-flash", CONVERSATION_CONFIG_RESULT_MISSING_FIELD},
        {"api_host=", CONVERSATION_CONFIG_RESULT_MISSING_FIELD},
    };
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        char form[CONVERSATION_PREFERENCES_FORM_MAX_LENGTH + 1U];
        const int count = snprintf(form, sizeof(form),
            "enabled=off&voice=longanqian&reply_style=concise&%s", invalid[i].field);
        assert(count > 0 && (size_t)count < sizeof(form));
        assert(conversation_config_parse_preferences_form(form, (size_t)count, &preferences) ==
               invalid[i].result);
    }
    /* Percent encoding may exceed the original 256-byte form budget. */
    char long_host[CONVERSATION_API_HOST_MAX_LENGTH + 1U] = {0};
    memset(long_host, 'a', 63U);
    memcpy(long_host + 63U, ".ap-southeast-1.maas.aliyuncs.com",
           sizeof(".ap-southeast-1.maas.aliyuncs.com"));
    char encoded[CONVERSATION_PREFERENCES_FORM_MAX_LENGTH + 1U];
    const int prefix = snprintf(encoded, sizeof(encoded),
        "enabled=off&voice=longanqian&reply_style=concise&"
        "%%6dodel=qwen-audio-3.0-realtime-flash&api%%5fhost=");
    assert(prefix > 0);
    size_t length = (size_t)prefix;
    for (size_t i = 0U; long_host[i] != '\0'; ++i) {
        assert(length + 3U < sizeof(encoded));
        assert(snprintf(encoded + length, sizeof(encoded) - length,
                        "%%%02X", (unsigned char)long_host[i]) == 3);
        length += 3U;
    }
    assert(length > 256U);
    assert(conversation_config_parse_preferences_form(encoded, length, &preferences) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(strcmp(preferences.api_host, long_host) == 0);
    assert(conversation_config_apply_preferences(&current, &preferences, &current) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(strcmp(current.api_host, long_host) == 0);
    const conversation_config_t unchanged = current;
    preferences.has_model = false;
    assert(conversation_config_apply_preferences(&current, &preferences, &current) ==
           CONVERSATION_CONFIG_RESULT_MISSING_FIELD);
    assert(memcmp(&current, &unchanged, sizeof(current)) == 0);
    preferences.has_model = true;
    preferences.model = CONVERSATION_MODEL_QWEN3_OMNI_FLASH_REALTIME;
    assert(conversation_config_apply_preferences(&current, &preferences, &candidate) ==
           CONVERSATION_CONFIG_RESULT_INVALID_MODEL);
    assert(candidate.api_key[0] == '\0');
    preferences.model = CONVERSATION_DEFAULT_MODEL;
    snprintf(preferences.api_host, sizeof(preferences.api_host), "%s", "evil.example");
    assert(conversation_config_apply_preferences(&current, &preferences, &current) ==
           CONVERSATION_CONFIG_RESULT_INVALID_API_HOST);
    assert(memcmp(&current, &unchanged, sizeof(current)) == 0);
    preferences.api_host[0] = '\0';
    assert(conversation_config_apply_preferences(&current, &preferences, &current) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(current.api_host[0] == '\0');
    assert(strcmp(current.api_key, unchanged.api_key) == 0);
    conversation_config_reset(&current);
    conversation_config_reset(&candidate);
}

static void test_api_key_form_is_single_field_and_cleared_on_failure(void)
{
    conversation_api_key_update_t update;
    static const char encoded[] = "%61pi%5Fkey=sk-a%2Bb%26c%3Dd";
    assert(conversation_config_parse_api_key_form(encoded, strlen(encoded), &update) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(strcmp(update.api_key, "sk-a+b&c=d") == 0);
    assert(conversation_config_parse_api_key_form("api_key=", 8U, &update) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(update.api_key[0] == '\0');
    const struct { const char *form; conversation_config_result_t result; } invalid[] = {
        {"", CONVERSATION_CONFIG_RESULT_INVALID_FORM},
        {"api_key", CONVERSATION_CONFIG_RESULT_INVALID_FORM},
        {"=sk-x", CONVERSATION_CONFIG_RESULT_INVALID_FORM},
        {"api_key=sk-x&", CONVERSATION_CONFIG_RESULT_INVALID_FORM},
        {"api_key=sk-x&&api_key=sk-y", CONVERSATION_CONFIG_RESULT_INVALID_FORM},
        {"api_key=sk-x&api_key=", CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD},
        {"api_key=sk-x&api%5Fkey=sk-y", CONVERSATION_CONFIG_RESULT_DUPLICATE_FIELD},
        {"api_key=sk-x&enabled=on", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"api_key=sk-x&api_host=", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"api_key=sk-x&model=qwen-audio-3.0-realtime-flash", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"api_key=sk-x&service=aliyun_realtime", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"api_key=sk-x&voice=longanqian", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"api_key=sk-x&reply_style=concise", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"api_key=sk-x&unknown=x", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"unknown=x", CONVERSATION_CONFIG_RESULT_UNEXPECTED_FIELD},
        {"api%00key=sk-x", CONVERSATION_CONFIG_RESULT_INVALID_ENCODING},
        {"api_key=sk-%00x", CONVERSATION_CONFIG_RESULT_INVALID_ENCODING},
        {"api_key=sk-%ZZ", CONVERSATION_CONFIG_RESULT_INVALID_ENCODING},
        {"api_key=sk-%", CONVERSATION_CONFIG_RESULT_INVALID_ENCODING},
        {"api_key=sk-+space", CONVERSATION_CONFIG_RESULT_INVALID_API_KEY},
        {"api_key=sk-%0D%0AInjected", CONVERSATION_CONFIG_RESULT_INVALID_API_KEY},
        {"api_key=%E4%B8%AD", CONVERSATION_CONFIG_RESULT_INVALID_API_KEY},
    };
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        memset(&update, 0xa5, sizeof(update));
        assert(conversation_config_parse_api_key_form(invalid[i].form,
            strlen(invalid[i].form), &update) == invalid[i].result);
        const uint8_t *bytes = (const uint8_t *)&update;
        for (size_t j = 0U; j < sizeof(update); ++j) assert(bytes[j] == 0U);
    }
    char max_encoded[CONVERSATION_API_KEY_FORM_MAX_LENGTH + 1U] = "api_key=";
    size_t length = 8U;
    for (size_t i = 0U; i < CONVERSATION_API_KEY_MAX_LENGTH; ++i) {
        memcpy(max_encoded + length, "%41", 3U);
        length += 3U;
    }
    assert(conversation_config_parse_api_key_form(max_encoded, length, &update) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(strlen(update.api_key) == CONVERSATION_API_KEY_MAX_LENGTH);
    max_encoded[length++] = 'A';
    assert(conversation_config_parse_api_key_form(max_encoded, length, &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_API_KEY);
    assert(update.api_key[0] == '\0');
    memset(max_encoded, 'x', sizeof(max_encoded));
    assert(conversation_config_parse_api_key_form(max_encoded, sizeof(max_encoded), &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_FORM);
    assert(conversation_config_parse_api_key_form(NULL, 1U, &update) ==
           CONVERSATION_CONFIG_RESULT_INVALID_FORM);
    assert(conversation_config_parse_api_key_form(encoded, strlen(encoded), NULL) ==
           CONVERSATION_CONFIG_RESULT_INVALID_ARGUMENT);
    conversation_config_clear_sensitive(&update, sizeof(update));
}

static void test_credentials_form_preserves_independent_preferences(void)
{
    static const char credentials[] =
        "service=aliyun_realtime&model=qwen-audio-3.0-realtime-flash&"
        "api_key=sk-new-credentials&api_host=other.cn-beijing.maas.aliyuncs.com";
    conversation_config_update_t update;
    assert(conversation_config_parse_form(credentials, strlen(credentials), &update) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(update.preserve_enabled && !update.has_voice && !update.has_reply_style);
    conversation_config_t current = make_config("sk-old-credentials");
    current.voice = CONVERSATION_VOICE_LONGANLINGXI;
    current.reply_style = CONVERSATION_REPLY_STYLE_ENGLISH_PRACTICE;
    conversation_config_t candidate;
    for (unsigned enabled = 0U; enabled < 2U; ++enabled) {
        current.enabled = enabled != 0U;
        assert(conversation_config_apply_update(&current, &update, &candidate) ==
               CONVERSATION_CONFIG_RESULT_OK);
        assert(candidate.enabled == current.enabled);
        assert(candidate.voice == current.voice && candidate.reply_style == current.reply_style);
        assert(strcmp(candidate.api_key, "sk-new-credentials") == 0);
        assert(strcmp(candidate.api_host, "other.cn-beijing.maas.aliyuncs.com") == 0);
    }
    conversation_config_reset(&current);
    assert(conversation_config_apply_update(&current, &update, &candidate) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(!candidate.enabled && candidate.voice == CONVERSATION_DEFAULT_VOICE);
    assert(candidate.reply_style == CONVERSATION_DEFAULT_REPLY_STYLE);
    char legacy[sizeof(credentials) + 16U];
    const int size = snprintf(legacy, sizeof(legacy), "%s&enabled=on", credentials);
    assert(size > 0 && (size_t)size < sizeof(legacy));
    assert(conversation_config_parse_form(legacy, (size_t)size, &update) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(!update.preserve_enabled && update.enabled);
    assert(conversation_config_apply_update(&current, &update, &candidate) ==
           CONVERSATION_CONFIG_RESULT_OK);
    assert(candidate.enabled);
    conversation_config_reset(&current);
    conversation_config_reset(&candidate);
    conversation_config_clear_sensitive(&update, sizeof(update));
}

int main(void)
{
    test_defaults_models_and_secret_free_status();
    test_retired_model_cannot_be_used_or_written();
    test_api_host_policy_and_endpoint_builder();
    test_update_preserves_key_and_controls_model();
    test_bounds_and_clear();
    test_portal_form_parser();
    test_record_round_trip_and_integrity();
    test_record_selection_handles_generation_wrap();
    test_voice_and_reply_style_presets();
    test_preferences_route_has_no_credential_fields();
    test_preferences_model_and_host_are_controlled_together();
    test_api_key_form_is_single_field_and_cleared_on_failure();
    test_credentials_form_preserves_independent_preferences();
    puts("conversation config tests passed");
    return 0;
}
