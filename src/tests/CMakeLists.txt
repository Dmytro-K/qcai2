function(qcai2_register_test test_name target_name)
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target_name}>
                $<TARGET_FILE_DIR:${target_name}>
            COMMAND_EXPAND_LISTS
        )
    endif()
    qcai2_apply_test_precompiled_headers(${target_name})
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

add_executable(tst_qcai2_debugger_session_service
    tst_debugger_session_service.cpp
    ../src/debugger/debugger_session_service.cpp
    ../src/util/output_capture_support.cpp
)

qcai2_enable_linker_map(tst_qcai2_debugger_session_service)

target_include_directories(tst_qcai2_debugger_session_service PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_debugger_session_service PRIVATE
    Qt::Core
    Qt::Test
    QtCreator::Core
    QtCreator::Debugger
)
qcai2_register_test(qcai2_debugger_session_service tst_qcai2_debugger_session_service)

add_executable(tst_qcai2_debugger_tools
    tst_debugger_tools.cpp
    ../src/debugger/debugger_session_service.cpp
    ../src/tools/debugger_tools.cpp
    ../src/tools/tool_registry.cpp
    ../src/util/logger.cpp
    ../src/util/output_capture_support.cpp
)

qcai2_enable_linker_map(tst_qcai2_debugger_tools)

target_include_directories(tst_qcai2_debugger_tools PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_debugger_tools PRIVATE
    Qt::Core
    Qt::Test
    QtCreator::Core
    QtCreator::Debugger
)
qcai2_register_test(qcai2_debugger_tools tst_qcai2_debugger_tools)

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

add_executable(tst_qcai2_actions_log_model
    tst_actions_log_model.cpp
    ../src/ui/actions_log_links.cpp
    ../src/ui/actions_log_model.cpp
)

qcai2_enable_linker_map(tst_qcai2_actions_log_model)

target_include_directories(tst_qcai2_actions_log_model PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_actions_log_model PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_actions_log_model tst_qcai2_actions_log_model)

add_executable(tst_qcai2_actions_log_delegate
    tst_actions_log_delegate.cpp
    ../src/ui/actions_log_links.cpp
    ../src/ui/actions_log_model.cpp
    ../src/ui/actions_log_delegate.cpp
)

qcai2_enable_linker_map(tst_qcai2_actions_log_delegate)

target_include_directories(tst_qcai2_actions_log_delegate PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_actions_log_delegate PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Widgets
    Qt::Test
)
qcai2_register_test(qcai2_actions_log_delegate tst_qcai2_actions_log_delegate)

add_executable(tst_qcai2_actions_log_links
    tst_actions_log_links.cpp
    ../src/ui/actions_log_links.cpp
)

qcai2_enable_linker_map(tst_qcai2_actions_log_links)

target_include_directories(tst_qcai2_actions_log_links PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_actions_log_links PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_actions_log_links tst_qcai2_actions_log_links)

add_executable(tst_qcai2_request_queue
    tst_request_queue.cpp
    ../src/ui/request_queue.cpp
    ../src/models/agent_messages.cpp
)

qcai2_enable_linker_map(tst_qcai2_request_queue)

target_include_directories(tst_qcai2_request_queue PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_request_queue PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_request_queue tst_qcai2_request_queue)

add_executable(tst_qcai2_request_duration_formatter
    tst_request_duration_formatter.cpp
    ../src/ui/request_duration_formatter.cpp
)

qcai2_enable_linker_map(tst_qcai2_request_duration_formatter)

target_include_directories(tst_qcai2_request_duration_formatter PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_request_duration_formatter PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_request_duration_formatter tst_qcai2_request_duration_formatter)

add_executable(tst_qcai2_file_tools
    tst_file_tools.cpp
    ../src/tools/file_tools.cpp
    ../src/util/diff.cpp
    ../src/util/process_runner.cpp
    ../src/util/logger.cpp
)

qcai2_enable_linker_map(tst_qcai2_file_tools)

target_include_directories(tst_qcai2_file_tools PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_file_tools PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_file_tools tst_qcai2_file_tools)

add_executable(tst_qcai2_auto_hiding_list_widget
    tst_auto_hiding_list_widget.cpp
    ../src/ui/auto_hiding_list_widget.cpp
)

qcai2_enable_linker_map(tst_qcai2_auto_hiding_list_widget)

target_include_directories(tst_qcai2_auto_hiding_list_widget PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_auto_hiding_list_widget PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Widgets
    Qt::Test
)
qcai2_register_test(qcai2_auto_hiding_list_widget tst_qcai2_auto_hiding_list_widget)

add_executable(tst_qcai2_decision_request_widget
    tst_decision_request_widget.cpp
    ../src/ui/decision_request_widget.cpp
    ../src/models/agent_messages.cpp
)

qcai2_enable_linker_map(tst_qcai2_decision_request_widget)

target_include_directories(tst_qcai2_decision_request_widget PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_decision_request_widget PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Widgets
    Qt::Test
)
qcai2_register_test(qcai2_decision_request_widget tst_qcai2_decision_request_widget)

add_executable(tst_qcai2_settings_spin_box_widgets
    tst_settings_spin_box_widgets.cpp
    ../src/settings/settings_spin_box.cpp
    ../src/settings/settings_spin_box.ui
    ../src/settings/settings_double_spin_box.cpp
    ../src/settings/settings_double_spin_box.ui
)

qcai2_enable_linker_map(tst_qcai2_settings_spin_box_widgets)

target_include_directories(tst_qcai2_settings_spin_box_widgets PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_settings_spin_box_widgets PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Widgets
    Qt::Test
)
qcai2_register_test(qcai2_settings_spin_box_widgets tst_qcai2_settings_spin_box_widgets)

add_executable(tst_qcai2_debugger_ui_integration
    tst_debugger_ui_integration.cpp
    ../src/debugger/debugger_session_service.cpp
    ../src/ui/debugger_status_widget.cpp
    ../src/util/output_capture_support.cpp
)

qcai2_enable_linker_map(tst_qcai2_debugger_ui_integration)

target_include_directories(tst_qcai2_debugger_ui_integration PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_debugger_ui_integration PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Widgets
    Qt::Test
    QtCreator::Core
    QtCreator::Debugger
)
qcai2_register_test(qcai2_debugger_ui_integration tst_qcai2_debugger_ui_integration)

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
    ../src/completion/completion_request_settings.cpp
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
    Qt::Gui
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

add_executable(tst_qcai2_completion_detailed_log
    tst_completion_detailed_log.cpp
    ../src/util/completion_detailed_log.cpp
    ../src/util/system_diagnostics_log.cpp
    ../src/context/chat_context.cpp
    ../src/models/agent_messages.cpp
    ../src/progress/agent_progress.cpp
    ../src/providers/provider_usage.cpp
    ../src/util/json.cpp
)

qcai2_enable_linker_map(tst_qcai2_completion_detailed_log)

target_include_directories(tst_qcai2_completion_detailed_log PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_completion_detailed_log PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_completion_detailed_log tst_qcai2_completion_detailed_log)

add_executable(tst_qcai2_completion_request_settings
    tst_completion_request_settings.cpp
    ../src/completion/completion_request_settings.cpp
)

qcai2_enable_linker_map(tst_qcai2_completion_request_settings)

target_include_directories(tst_qcai2_completion_request_settings PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/lib/qtmcp/include"
)
target_link_libraries(tst_qcai2_completion_request_settings PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_completion_request_settings tst_qcai2_completion_request_settings)

add_executable(tst_qcai2_completion_trigger_utils
    tst_completion_trigger_utils.cpp
    ../src/completion/completion_trigger_utils.cpp
)

qcai2_enable_linker_map(tst_qcai2_completion_trigger_utils)

target_include_directories(tst_qcai2_completion_trigger_utils PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_completion_trigger_utils PRIVATE
    Qt::Core
    Qt::Test
)
qcai2_register_test(qcai2_completion_trigger_utils tst_qcai2_completion_trigger_utils)

add_executable(tst_qcai2_completion_response_utils
    tst_completion_response_utils.cpp
    ../src/completion/completion_trigger_utils.cpp
    ../src/completion/completion_response_utils.cpp
)

qcai2_enable_linker_map(tst_qcai2_completion_response_utils)
qcai2_apply_global_parameters(tst_qcai2_completion_response_utils)

target_include_directories(tst_qcai2_completion_response_utils PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/lib/qompi/include"
)
target_link_libraries(tst_qcai2_completion_response_utils PRIVATE
    Qt::Core
    Qt::Test
    qompi_core
)
qcai2_register_test(qcai2_completion_response_utils tst_qcai2_completion_response_utils)

add_executable(tst_qcai2_qompi_completion_session
    tst_qompi_completion_session.cpp
)

qcai2_enable_linker_map(tst_qcai2_qompi_completion_session)

target_link_libraries(tst_qcai2_qompi_completion_session PRIVATE
    Qt::Core
    Qt::Test
    qompi_core
)
qcai2_register_test(qcai2_qompi_completion_session tst_qcai2_qompi_completion_session)

add_executable(tst_qcai2_qompi_completion_policy
    tst_qompi_completion_policy.cpp
)

qcai2_enable_linker_map(tst_qcai2_qompi_completion_policy)

target_link_libraries(tst_qcai2_qompi_completion_policy PRIVATE
    Qt::Core
    Qt::Test
    qompi_core
)
qcai2_register_test(qcai2_qompi_completion_policy tst_qcai2_qompi_completion_policy)

add_executable(tst_qcai2_qompi_completion_profiles
    tst_qompi_completion_profiles.cpp
)

qcai2_enable_linker_map(tst_qcai2_qompi_completion_profiles)

target_link_libraries(tst_qcai2_qompi_completion_profiles PRIVATE
    Qt::Core
    Qt::Test
    qompi_core
)
qcai2_register_test(qcai2_qompi_completion_profiles tst_qcai2_qompi_completion_profiles)

add_executable(tst_qcai2_qompi_qt_completion_session
    tst_qompi_qt_completion_session.cpp
)

qcai2_enable_linker_map(tst_qcai2_qompi_qt_completion_session)

target_link_libraries(tst_qcai2_qompi_qt_completion_session PRIVATE
    Qt::Core
    Qt::Test
    qompi_qt
)
qcai2_register_test(qcai2_qompi_qt_completion_session tst_qcai2_qompi_qt_completion_session)

add_executable(tst_qcai2_qompi_provider_backend_adapter
    tst_qompi_provider_backend_adapter.cpp
    ../src/completion/qompi_provider_backend_adapter.cpp
    ../src/progress/agent_progress.cpp
)

qcai2_enable_linker_map(tst_qcai2_qompi_provider_backend_adapter)

target_include_directories(tst_qcai2_qompi_provider_backend_adapter PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/lib/qompi/include"
)
target_link_libraries(tst_qcai2_qompi_provider_backend_adapter PRIVATE
    Qt::Core
    Qt::Test
    qompi_core
)
qcai2_register_test(qcai2_qompi_provider_backend_adapter
                    tst_qcai2_qompi_provider_backend_adapter)

add_executable(tst_qcai2_project_root_resolver
    tst_project_root_resolver.cpp
    ../src/util/project_root_resolver.cpp
)

qcai2_enable_linker_map(tst_qcai2_project_root_resolver)

target_include_directories(tst_qcai2_project_root_resolver PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_project_root_resolver PRIVATE
    Qt::Core
    Qt::Test
    QtCreator::ExtensionSystem
    QtCreator::ProjectExplorer
    QtCreator::Utils
)
qcai2_register_test(qcai2_project_root_resolver tst_qcai2_project_root_resolver)

if(FEATURE_CLANGD_ENABLE)
add_executable(tst_qcai2_completion_clang_context
    tst_completion_clang_context.cpp
    ../src/clangd/clangd_service.cpp
    ../src/completion/completion_clang_context.cpp
)

qcai2_enable_linker_map(tst_qcai2_completion_clang_context)
qcai2_apply_global_parameters(tst_qcai2_completion_clang_context)

target_include_directories(tst_qcai2_completion_clang_context PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_completion_clang_context PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Test
    QtCreator::ClangCodeModel
    QtCreator::Utils
)
qcai2_register_test(qcai2_completion_clang_context tst_qcai2_completion_clang_context)
endif()

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
