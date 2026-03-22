function(qcai2_register_test test_name target_name)
    add_test(NAME ${test_name} COMMAND ${target_name})
    set_property(GLOBAL APPEND PROPERTY QCAI2_TEST_TARGETS ${target_name})
endfunction()

if(QCAI2_ENABLE_COVERAGE)
    add_compile_options(-O0 -g --coverage)
    add_link_options(--coverage)
endif()

add_executable(tst_qcai2_json
    tst_json.cpp
    ../src/util/json.cpp
)

qcai2_enable_linker_map(tst_qcai2_json)

target_include_directories(tst_qcai2_json PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_json PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_json tst_qcai2_json)

add_executable(tst_qcai2_toolcall
    tst_toolcall.cpp
    ../src/models/tool_call.cpp
)

qcai2_enable_linker_map(tst_qcai2_toolcall)

target_include_directories(tst_qcai2_toolcall PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_toolcall PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_toolcall tst_qcai2_toolcall)

add_executable(tst_qcai2_agent_messages
    tst_agent_messages.cpp
    ../src/models/agent_messages.cpp
)

qcai2_enable_linker_map(tst_qcai2_agent_messages)

target_include_directories(tst_qcai2_agent_messages PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_agent_messages PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_agent_messages tst_qcai2_agent_messages)

add_executable(tst_qcai2_provider_usage
    tst_provider_usage.cpp
    ../src/providers/provider_usage.cpp
    ../src/util/json.cpp
)

qcai2_enable_linker_map(tst_qcai2_provider_usage)

target_include_directories(tst_qcai2_provider_usage PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_provider_usage PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_provider_usage tst_qcai2_provider_usage)

add_executable(tst_qcai2_agent_progress
    tst_agent_progress.cpp
    ../src/progress/agent_progress.cpp
    ../src/progress/provider_progress_adapter.cpp
    ../src/progress/agent_status.cpp
    ../src/progress/agent_status_formatter.cpp
    ../src/progress/agent_status_renderer.cpp
    ../src/progress/agent_progress_tracker.cpp
)

qcai2_enable_linker_map(tst_qcai2_agent_progress)

target_include_directories(tst_qcai2_agent_progress PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_agent_progress PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_agent_progress tst_qcai2_agent_progress)

add_executable(tst_qcai2_agent_run_state_machine
    tst_agent_run_state_machine.cpp
    ../src/agent_run_state_machine.cpp
)

qcai2_enable_linker_map(tst_qcai2_agent_run_state_machine)

target_include_directories(tst_qcai2_agent_run_state_machine PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_agent_run_state_machine PRIVATE
    Qt::Core
    Qt::StateMachine
    Qt::Test
)
qcai2_register_test(qcai2_agent_run_state_machine tst_qcai2_agent_run_state_machine)

add_executable(tst_qcai2_agent_controller_soft_steer
    tst_agent_controller_soft_steer.cpp
    ../src/agent_controller.cpp
    ../src/agent_run_state_machine.cpp
    ../src/models/agent_messages.cpp
    ../src/providers/provider_usage.cpp
    ../src/progress/agent_progress.cpp
    ../src/progress/provider_progress_adapter.cpp
    ../src/progress/agent_status.cpp
    ../src/progress/agent_status_formatter.cpp
    ../src/progress/agent_status_renderer.cpp
    ../src/progress/agent_progress_tracker.cpp
    ../src/tools/tool_registry.cpp
    ../src/util/logger.cpp
    ../src/util/json.cpp
)

qcai2_enable_linker_map(tst_qcai2_agent_controller_soft_steer)

target_include_directories(tst_qcai2_agent_controller_soft_steer PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/lib/qtmcp/include"
)
target_link_libraries(tst_qcai2_agent_controller_soft_steer PRIVATE
    Qt::Core
    Qt::StateMachine
    Qt::Test
)
qcai2_register_test(qcai2_agent_controller_soft_steer tst_qcai2_agent_controller_soft_steer)

add_executable(tst_qcai2_chat_context
    tst_chat_context.cpp
    ../src/context/chat_context.h
    ../src/context/chat_context.cpp
    ../src/context/chat_context_store.h
    ../src/context/chat_context_store.cpp
    ../src/context/chat_context_manager.h
    ../src/context/chat_context_manager.cpp
    ../src/models/agent_messages.cpp
    ../src/providers/provider_usage.cpp
    ../src/util/json.cpp
)

qcai2_enable_linker_map(tst_qcai2_chat_context)

target_include_directories(tst_qcai2_chat_context PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/lib/qtmcp/include"
)
target_link_libraries(tst_qcai2_chat_context PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_chat_context tst_qcai2_chat_context)

add_executable(tst_qcai2_slash_commands
    tst_slash_commands.cpp
    ../src/commands/slash_command_registry.cpp
    ../src/commands/hello_command.cpp
    ../src/vector_search/vector_search_service.cpp
    ../src/goal/file_reference_goal_handler.cpp
    ../src/goal/slash_command_goal_handler.cpp
)

if(FEATURE_QDRANT_ENABLE)
target_sources(tst_qcai2_slash_commands PRIVATE
    ../src/commands/search_command.cpp
    ../src/vector_search/qdrant/qdrant_vector_search_backend.cpp
)
target_compile_definitions(tst_qcai2_slash_commands PRIVATE
    QCAI2_FEATURE_QDRANT_ENABLE=1
)
target_link_libraries(tst_qcai2_slash_commands PRIVATE
    qcai2_qdrant_http
)
else()
target_compile_definitions(tst_qcai2_slash_commands PRIVATE
    QCAI2_FEATURE_QDRANT_ENABLE=0
)
endif()

qcai2_enable_linker_map(tst_qcai2_slash_commands)

target_include_directories(tst_qcai2_slash_commands PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/lib/qtmcp/include"
)
target_link_libraries(tst_qcai2_slash_commands PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::Test
)
qcai2_register_test(qcai2_slash_commands tst_qcai2_slash_commands)

add_executable(tst_qcai2_output_capture
    tst_output_capture.cpp
    ../src/util/output_capture_support.cpp
)

qcai2_enable_linker_map(tst_qcai2_output_capture)

target_include_directories(tst_qcai2_output_capture PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_output_capture PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_output_capture tst_qcai2_output_capture)

add_executable(tst_qcai2_safety_policy
    tst_safety_policy.cpp
    ../src/safety/safety_policy.cpp
    ../src/util/logger.cpp
)

qcai2_enable_linker_map(tst_qcai2_safety_policy)

target_include_directories(tst_qcai2_safety_policy PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_safety_policy PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_safety_policy tst_qcai2_safety_policy)

add_executable(tst_qcai2_mcp_config
    tst_mcp_config.cpp
    ../../lib/qtmcp/src/server_definition.cpp
)

qcai2_enable_linker_map(tst_qcai2_mcp_config)

target_include_directories(tst_qcai2_mcp_config PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/lib/qtmcp/include"
)
target_link_libraries(tst_qcai2_mcp_config PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_mcp_config tst_qcai2_mcp_config)

add_executable(tst_qcai2_tool_registry
    tst_tool_registry.cpp
    ../src/tools/tool_registry.cpp
    ../src/util/logger.cpp
)

qcai2_enable_linker_map(tst_qcai2_tool_registry)

target_include_directories(tst_qcai2_tool_registry PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_tool_registry PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_tool_registry tst_qcai2_tool_registry)

add_executable(tst_qcai2_web_tools
    tst_web_tools.cpp
    ../src/util/web_tools.cpp
)

qcai2_enable_linker_map(tst_qcai2_web_tools)

target_include_directories(tst_qcai2_web_tools PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_web_tools PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::Test
)
qcai2_register_test(qcai2_web_tools tst_qcai2_web_tools)

if(FEATURE_CLANGD_ENABLE)
add_executable(tst_qcai2_clangd_tools
    tst_clangd_tools.cpp
    ../src/tools/clangd_tool_metadata.cpp
)

qcai2_enable_linker_map(tst_qcai2_clangd_tools)

target_include_directories(tst_qcai2_clangd_tools PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_clangd_tools PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_clangd_tools tst_qcai2_clangd_tools)
endif()

add_executable(tst_qcai2_vector_search
    tst_vector_search.cpp
    ../src/vector_search/vector_search_service.cpp
)

if(FEATURE_QDRANT_ENABLE)
target_sources(tst_qcai2_vector_search PRIVATE
    ../src/vector_search/qdrant/qdrant_vector_search_backend.cpp
)
target_compile_definitions(tst_qcai2_vector_search PRIVATE
    QCAI2_FEATURE_QDRANT_ENABLE=1
)
target_link_libraries(tst_qcai2_vector_search PRIVATE
    qcai2_qdrant_http
)
else()
target_compile_definitions(tst_qcai2_vector_search PRIVATE
    QCAI2_FEATURE_QDRANT_ENABLE=0
)
endif()

qcai2_enable_linker_map(tst_qcai2_vector_search)

target_include_directories(tst_qcai2_vector_search PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/lib/qtmcp/include"
)
target_link_libraries(tst_qcai2_vector_search PRIVATE
    Qt::Core
    Qt::Network
    Qt::Test
)
qcai2_register_test(qcai2_vector_search tst_qcai2_vector_search)

add_executable(tst_qcai2_vector_search_tools
    tst_vector_search_tools.cpp
    ../src/tools/vector_search_tools.cpp
    ../src/vector_search/text_embedder.cpp
    ../src/vector_search/vector_search_service.cpp
)

if(FEATURE_QDRANT_ENABLE)
target_sources(tst_qcai2_vector_search_tools PRIVATE
    ../src/vector_search/qdrant/qdrant_vector_search_backend.cpp
)
target_compile_definitions(tst_qcai2_vector_search_tools PRIVATE
    QCAI2_FEATURE_QDRANT_ENABLE=1
)
target_link_libraries(tst_qcai2_vector_search_tools PRIVATE
    qcai2_qdrant_http
)
else()
target_compile_definitions(tst_qcai2_vector_search_tools PRIVATE
    QCAI2_FEATURE_QDRANT_ENABLE=0
)
endif()

qcai2_enable_linker_map(tst_qcai2_vector_search_tools)

target_include_directories(tst_qcai2_vector_search_tools PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/lib/qtmcp/include"
)
target_link_libraries(tst_qcai2_vector_search_tools PRIVATE
    Qt::Core
    Qt::Network
    Qt::Test
)
qcai2_register_test(qcai2_vector_search_tools tst_qcai2_vector_search_tools)

add_executable(tst_qcai2_request_detailed_log
    tst_request_detailed_log.cpp
    ../src/util/request_detailed_log.cpp
    ../src/context/chat_context.cpp
    ../src/models/agent_messages.cpp
    ../src/providers/provider_usage.cpp
    ../src/util/json.cpp
)

qcai2_enable_linker_map(tst_qcai2_request_detailed_log)

target_include_directories(tst_qcai2_request_detailed_log PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_request_detailed_log PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_request_detailed_log tst_qcai2_request_detailed_log)

add_executable(tst_qcai2_prompt_instructions
    tst_prompt_instructions.cpp
    ../src/util/prompt_instructions.cpp
)

qcai2_enable_linker_map(tst_qcai2_prompt_instructions)

target_include_directories(tst_qcai2_prompt_instructions PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_prompt_instructions PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_prompt_instructions tst_qcai2_prompt_instructions)

add_executable(tst_qcai2_migration
    tst_migration.cpp
    ../src/util/migration.cpp
    ${QCAI2_MIGRATION_FILES}
)

qcai2_enable_linker_map(tst_qcai2_migration)

target_include_directories(tst_qcai2_migration PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_migration PRIVATE
    Qt::Core
    Qt::Test
    qcai2_libarchive
    qcai2_version_info
)
qcai2_register_test(qcai2_migration tst_qcai2_migration)

get_property(QCAI2_TEST_TARGETS GLOBAL PROPERTY QCAI2_TEST_TARGETS)
if(QCAI2_TEST_TARGETS)
    list(REMOVE_DUPLICATES QCAI2_TEST_TARGETS)
    add_custom_target(qcai2_tests DEPENDS ${QCAI2_TEST_TARGETS})
endif()
