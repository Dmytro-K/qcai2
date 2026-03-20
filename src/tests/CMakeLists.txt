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
add_test(NAME qcai2_json COMMAND tst_qcai2_json)

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
add_test(NAME qcai2_toolcall COMMAND tst_qcai2_toolcall)

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
add_test(NAME qcai2_agent_messages COMMAND tst_qcai2_agent_messages)

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
add_test(NAME qcai2_provider_usage COMMAND tst_qcai2_provider_usage)

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
add_test(NAME qcai2_agent_progress COMMAND tst_qcai2_agent_progress)

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
add_test(NAME qcai2_chat_context COMMAND tst_qcai2_chat_context)

add_executable(tst_qcai2_slash_commands
    tst_slash_commands.cpp
    ../src/commands/slash_command_registry.cpp
    ../src/commands/hello_command.cpp
    ../src/goal/file_reference_goal_handler.cpp
    ../src/goal/slash_command_goal_handler.cpp
)

qcai2_enable_linker_map(tst_qcai2_slash_commands)

target_include_directories(tst_qcai2_slash_commands PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_slash_commands PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Test
)
add_test(NAME qcai2_slash_commands COMMAND tst_qcai2_slash_commands)

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
add_test(NAME qcai2_output_capture COMMAND tst_qcai2_output_capture)

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
add_test(NAME qcai2_safety_policy COMMAND tst_qcai2_safety_policy)

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
add_test(NAME qcai2_mcp_config COMMAND tst_qcai2_mcp_config)

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
add_test(NAME qcai2_tool_registry COMMAND tst_qcai2_tool_registry)

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
add_test(NAME qcai2_request_detailed_log COMMAND tst_qcai2_request_detailed_log)

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
add_test(NAME qcai2_migration COMMAND tst_qcai2_migration)
