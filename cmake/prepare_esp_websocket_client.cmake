if(NOT DEFINED RLCD_WEBSOCKET_SOURCE_DIR OR
   "${RLCD_WEBSOCKET_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "RLCD_WEBSOCKET_SOURCE_DIR is required")
endif()
if(NOT DEFINED RLCD_WEBSOCKET_OUTPUT_DIR OR
   "${RLCD_WEBSOCKET_OUTPUT_DIR}" STREQUAL "")
    message(FATAL_ERROR "RLCD_WEBSOCKET_OUTPUT_DIR is required")
endif()

file(TO_CMAKE_PATH "${RLCD_WEBSOCKET_SOURCE_DIR}" websocket_source_dir)
file(TO_CMAKE_PATH "${RLCD_WEBSOCKET_OUTPUT_DIR}" websocket_output_dir)
get_filename_component(websocket_source_dir "${websocket_source_dir}" ABSOLUTE)
get_filename_component(websocket_output_dir "${websocket_output_dir}" ABSOLUTE)
if(websocket_source_dir STREQUAL websocket_output_dir)
    message(FATAL_ERROR
        "The protected WebSocket component must be generated outside its upstream checkout")
endif()

set(websocket_required_files
    CMakeLists.txt
    Kconfig
    LICENSE
    idf_component.yml
    esp_websocket_client.c
    include/esp_websocket_client.h)
foreach(relative_path IN LISTS websocket_required_files)
    if(NOT EXISTS "${websocket_source_dir}/${relative_path}")
        message(FATAL_ERROR
            "Incomplete ESP WebSocket Client source: missing ${relative_path}")
    endif()
endforeach()

file(READ "${websocket_source_dir}/idf_component.yml"
     websocket_component_manifest)
string(REGEX MATCHALL
    "version:[ \t]*\"[0-9]+\\.[0-9]+\\.[0-9]+\""
    websocket_component_versions "${websocket_component_manifest}")
list(LENGTH websocket_component_versions websocket_component_version_count)
if(NOT websocket_component_version_count EQUAL 1 OR
   NOT websocket_component_versions STREQUAL "version: \"1.8.0\"")
    message(FATAL_ERROR
        "Expected the reviewed ESP WebSocket Client 1.8.0 source; refusing "
        "an unreviewed version: ${websocket_component_versions}")
endif()

set(reviewed_websocket_source_sha256
    "18041ec37d3456bc1ecb3275a78c3ae64679f57f049b5b05af169ef6b1857c77")
file(SHA256 "${websocket_source_dir}/esp_websocket_client.c"
     websocket_source_sha256)
if(NOT websocket_source_sha256 STREQUAL reviewed_websocket_source_sha256)
    message(FATAL_ERROR
        "ESP WebSocket Client 1.8.0 source differs from the reviewed pinned "
        "revision; refusing an unreviewed build: ${websocket_source_sha256}")
endif()

set(redirect_define_enabled
    "#define WS_TRANSPORT_REDIRECT_HEADER_SUPPORT    1")
set(redirect_define_disabled
    "#define WS_TRANSPORT_REDIRECT_HEADER_SUPPORT    0")
file(READ "${websocket_source_dir}/esp_websocket_client.c"
     websocket_source)
string(REGEX MATCHALL
    "#define[ \t]+WS_TRANSPORT_REDIRECT_HEADER_SUPPORT[ \t]+[0-9]+"
    redirect_definitions "${websocket_source}")
list(LENGTH redirect_definitions redirect_definition_count)
if(NOT redirect_definition_count EQUAL 1)
    message(FATAL_ERROR
        "Unexpected ESP WebSocket Client redirect policy definition count: "
        "${redirect_definition_count}")
endif()
list(GET redirect_definitions 0 redirect_definition)
if(NOT redirect_definition STREQUAL redirect_define_enabled)
    message(FATAL_ERROR
        "Pinned ESP WebSocket Client redirect policy changed; refusing an "
        "unreviewed build: ${redirect_definition}")
endif()

set(redirect_branch_markers
    "#if WS_TRANSPORT_REDIRECT_HEADER_SUPPORT"
    "else if (WS_HTTP_REDIRECT(result))"
    "esp_transport_ws_get_redir_uri(client->transport)"
    "esp_websocket_client_set_uri(client, client->config->uri)"
    "ESP_LOGI(TAG, \"Redirecting to %s\", client->config->uri)")
foreach(marker IN LISTS redirect_branch_markers)
    string(FIND "${websocket_source}" "${marker}" marker_offset)
    if(marker_offset EQUAL -1)
        message(FATAL_ERROR
            "Pinned ESP WebSocket Client redirect branch changed; missing: ${marker}")
    endif()
endforeach()

set(redirect_follow_branch [=[#if WS_TRANSPORT_REDIRECT_HEADER_SUPPORT
            else if (WS_HTTP_REDIRECT(result)) {
                const char *redir = esp_transport_ws_get_redir_uri(client->transport);
                if (redir) {
                    // Redirecting to a new URI
                    free(client->config->uri);

                    client->config->uri = strdup(redir);
                    client->config->port = 0;

                    esp_websocket_client_set_uri(client, client->config->uri);

                    if (client->config->port == 0) {
                        client->config->port = esp_transport_get_default_port(client->transport);
                    }

                    // Rerun the connection with the redir uri.
                    client->state = WEBSOCKET_STATE_INIT;
                    ESP_LOGI(TAG, "Redirecting to %s", client->config->uri);
                    break;
                }
            }
#endif]=])
set(redirect_reject_branch [=[#if !WS_TRANSPORT_REDIRECT_HEADER_SUPPORT
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

string(REPLACE "${redirect_define_enabled}" "${redirect_define_disabled}"
       protected_websocket_source "${websocket_source}")
if(protected_websocket_source STREQUAL websocket_source)
    message(FATAL_ERROR "Failed to disable ESP WebSocket Client redirects")
endif()
set(macro_only_websocket_source "${protected_websocket_source}")
string(REPLACE "${redirect_follow_branch}" "${redirect_reject_branch}"
       protected_websocket_source "${protected_websocket_source}")
if(protected_websocket_source STREQUAL macro_only_websocket_source)
    message(FATAL_ERROR
        "Failed to replace ESP WebSocket Client redirect handling")
endif()
set(restored_websocket_source "${protected_websocket_source}")
string(REPLACE "${redirect_reject_branch}" "${redirect_follow_branch}"
       restored_websocket_source "${restored_websocket_source}")
string(REPLACE "${redirect_define_disabled}" "${redirect_define_enabled}"
       restored_websocket_source "${restored_websocket_source}")
if(NOT restored_websocket_source STREQUAL websocket_source)
    message(FATAL_ERROR
        "Redirect policy preparation changed beyond the reviewed patch")
endif()

file(MAKE_DIRECTORY "${websocket_output_dir}")
foreach(relative_path CMakeLists.txt Kconfig LICENSE idf_component.yml)
    configure_file(
        "${websocket_source_dir}/${relative_path}"
        "${websocket_output_dir}/${relative_path}"
        COPYONLY)
endforeach()
file(COPY "${websocket_source_dir}/include"
     DESTINATION "${websocket_output_dir}")
file(WRITE "${websocket_output_dir}/esp_websocket_client.c"
     "${protected_websocket_source}")

file(READ "${websocket_output_dir}/esp_websocket_client.c"
     generated_websocket_source)
if(NOT generated_websocket_source STREQUAL protected_websocket_source)
    message(FATAL_ERROR "Generated ESP WebSocket Client source is incomplete")
endif()
message(STATUS
    "ESP WebSocket Client redirects disabled in generated build component")
