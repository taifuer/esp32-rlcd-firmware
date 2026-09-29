foreach(required_variable
        RLCD_PROJECT_DIR RLCD_UPSTREAM_WEBSOCKET_DIR RLCD_TEST_TMP)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

set(policy_script
    "${RLCD_PROJECT_DIR}/cmake/prepare_esp_websocket_client.cmake")
set(upstream_source
    "${RLCD_UPSTREAM_WEBSOCKET_DIR}/esp_websocket_client.c")
if(NOT EXISTS "${policy_script}" OR NOT EXISTS "${upstream_source}")
    message(FATAL_ERROR "WebSocket redirect policy test inputs are missing")
endif()

function(run_policy source_dir output_dir result_var output_var)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DRLCD_WEBSOCKET_SOURCE_DIR=${source_dir}"
            "-DRLCD_WEBSOCKET_OUTPUT_DIR=${output_dir}"
            -P "${policy_script}"
        RESULT_VARIABLE policy_result
        OUTPUT_VARIABLE policy_stdout
        ERROR_VARIABLE policy_stderr)
    set(${result_var} "${policy_result}" PARENT_SCOPE)
    set(${output_var} "${policy_stdout}${policy_stderr}" PARENT_SCOPE)
endfunction()

file(SHA256 "${upstream_source}" upstream_sha256_before)
set(protected_dir "${RLCD_TEST_TMP}/protected/esp_websocket_client")
run_policy("${RLCD_UPSTREAM_WEBSOCKET_DIR}" "${protected_dir}"
           policy_result policy_output)
if(NOT policy_result EQUAL 0)
    message(FATAL_ERROR "Valid redirect policy preparation failed: ${policy_output}")
endif()
file(SHA256 "${upstream_source}" upstream_sha256_after)
if(NOT upstream_sha256_before STREQUAL upstream_sha256_after)
    message(FATAL_ERROR "Redirect policy preparation modified the upstream checkout")
endif()

file(READ "${upstream_source}" original_source)
file(READ "${protected_dir}/esp_websocket_client.c" protected_source)
set(enabled_define "#define WS_TRANSPORT_REDIRECT_HEADER_SUPPORT    1")
set(disabled_define "#define WS_TRANSPORT_REDIRECT_HEADER_SUPPORT    0")
string(FIND "${protected_source}" "${disabled_define}" disabled_offset)
string(FIND "${protected_source}" "${enabled_define}" enabled_offset)
if(disabled_offset EQUAL -1 OR NOT enabled_offset EQUAL -1)
    message(FATAL_ERROR "Generated component did not disable redirects")
endif()
set(expected_rejection_branch [=[#if !WS_TRANSPORT_REDIRECT_HEADER_SUPPORT
            else if (WS_HTTP_REDIRECT(result)) {
                client->error_handle.esp_ws_handshake_status_code = result;
                esp_websocket_client_error(client,
                                           "WebSocket redirect refused, status=%d",
                                           result);
                esp_websocket_client_abort_connection(client,
                                                      WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT);
                break;
            }
#endif]=])
string(FIND "${protected_source}" "${expected_rejection_branch}"
       rejection_branch_offset)
if(rejection_branch_offset EQUAL -1)
    message(FATAL_ERROR
        "Generated component does not fail closed on a 3xx handshake")
endif()
string(FIND "${protected_source}"
       "#define WS_HTTP_REDIRECT(code) ((code >= 300) && (code < 400))"
       redirect_classifier_offset)
if(redirect_classifier_offset EQUAL -1)
    message(FATAL_ERROR
        "Generated component does not isolate rejection to 3xx handshakes")
endif()
set(forbidden_redirect_markers
    "esp_transport_ws_get_redir_uri(client->transport)"
    "Redirecting to %s"
    "esp_websocket_client_set_uri(client, client->config->uri)")
foreach(marker IN LISTS forbidden_redirect_markers)
    string(FIND "${protected_source}" "${marker}" marker_offset)
    if(NOT marker_offset EQUAL -1)
        message(FATAL_ERROR
            "Generated component still follows redirects: ${marker}")
    endif()
endforeach()
string(REGEX MATCHALL
    "WebSocket redirect refused, status=%d"
    redirect_rejection_markers "${protected_source}")
list(LENGTH redirect_rejection_markers redirect_rejection_marker_count)
if(NOT redirect_rejection_marker_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one fail-closed 3xx redirect path, found: "
        "${redirect_rejection_marker_count}")
endif()
foreach(required_output
        CMakeLists.txt Kconfig LICENSE idf_component.yml
        include/esp_websocket_client.h)
    if(NOT EXISTS "${protected_dir}/${required_output}")
        message(FATAL_ERROR "Generated component is missing ${required_output}")
    endif()
endforeach()

function(expect_policy_failure
         case_name relative_path search_text replacement_text)
    set(case_source_dir "${RLCD_TEST_TMP}/${case_name}/upstream")
    set(case_output_dir "${RLCD_TEST_TMP}/${case_name}/output")
    file(MAKE_DIRECTORY "${case_source_dir}")
    file(COPY "${RLCD_UPSTREAM_WEBSOCKET_DIR}/"
         DESTINATION "${case_source_dir}")
    set(case_source "${case_source_dir}/${relative_path}")
    file(READ "${case_source}" case_contents)
    string(FIND "${case_contents}" "${search_text}" search_offset)
    if(search_offset EQUAL -1)
        message(FATAL_ERROR
            "Test case ${case_name} cannot find its mutation target")
    endif()
    string(REPLACE "${search_text}" "${replacement_text}"
           case_contents "${case_contents}")
    file(WRITE "${case_source}" "${case_contents}")
    run_policy("${case_source_dir}" "${case_output_dir}"
               rejected_result rejected_output)
    if(rejected_result EQUAL 0)
        message(FATAL_ERROR
            "Redirect policy accepted unsafe case ${case_name}: ${rejected_output}")
    endif()
endfunction()

expect_policy_failure(
    changed_define
    esp_websocket_client.c
    "${enabled_define}"
    "#define WS_TRANSPORT_REDIRECT_HEADER_SUPPORT    2")
expect_policy_failure(
    missing_define
    esp_websocket_client.c
    "${enabled_define}"
    "/* redirect policy definition removed */")
expect_policy_failure(
    changed_redirect_branch
    esp_websocket_client.c
    "esp_transport_ws_get_redir_uri(client->transport)"
    "esp_transport_ws_get_redir_uri_changed(client->transport)")
expect_policy_failure(
    changed_component_version
    idf_component.yml
    "version: \"1.8.0\""
    "version: \"1.8.1\"")

message(STATUS "ESP WebSocket redirect policy tests passed")
