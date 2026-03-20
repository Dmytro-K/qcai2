cmake_minimum_required(VERSION 3.28)

# Remove when sharing with others.
list(APPEND CMAKE_PREFIX_PATH "/usr")

project(qcai2)

include(GNUInstallDirs)
include(ExternalProject)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION.txt" QCAI2_VERSION_FILE_LINES
    REGEX "^[ \t]*[^# \t].*$")
list(GET QCAI2_VERSION_FILE_LINES 0 QCAI2_VERSION_FULL_RAW)
string(STRIP "${QCAI2_VERSION_FULL_RAW}" QCAI2_VERSION_FULL)

if(NOT QCAI2_VERSION_FULL MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-[0-9]+)$")
    message(FATAL_ERROR
        "VERSION.txt must contain a revision like 0.0.4-002, got: ${QCAI2_VERSION_FULL}")
endif()

set(QCAI2_VERSION_COMPAT "${CMAKE_MATCH_1}")
set(QCAI2_BUILD_SUFFIX "${CMAKE_MATCH_2}")

set(QCAI2_FORMAT_FILES_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/format-files.cmake")
set(QCAI2_BUILD_DOCS_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build-docs.cmake")

if(WIN32)
    if(DEFINED ENV{LOCALAPPDATA} AND NOT "$ENV{LOCALAPPDATA}" STREQUAL "")
        set(QCAI2_DEFAULT_INSTALL_PREFIX "$ENV{LOCALAPPDATA}/QtProject/qtcreator")
    else()
        set(QCAI2_DEFAULT_INSTALL_PREFIX "$ENV{HOME}/AppData/Local/QtProject/qtcreator")
    endif()
elseif(APPLE)
    set(QCAI2_DEFAULT_INSTALL_PREFIX "$ENV{HOME}/Library/Application Support/QtProject/Qt Creator")
else()
    if(DEFINED ENV{XDG_DATA_HOME} AND NOT "$ENV{XDG_DATA_HOME}" STREQUAL "")
        set(QCAI2_DEFAULT_INSTALL_PREFIX "$ENV{XDG_DATA_HOME}/data/QtProject/qtcreator")
    else()
        set(QCAI2_DEFAULT_INSTALL_PREFIX "$ENV{HOME}/.local/share/data/QtProject/qtcreator")
    endif()
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${QCAI2_DEFAULT_INSTALL_PREFIX}" CACHE PATH
        "Install path prefix, prepended onto install directories." FORCE)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    set(ENV{HOME} "$ENV{USERPROFILE}")
endif(WIN32)

if(WIN32)
    set(ADDITIONAL_FLAGS WIN32)
else(WIN32)
    set(ADDITIONAL_FLAGS)
endif(WIN32)

set(COMPILER_LIST ${CMAKE_C_COMPILER_ID} ${CMAKE_CXX_COMPILER_ID})
list(GET COMPILER_LIST 0 COMPILER_ID)

if("${COMPILER_ID}" STREQUAL "Clang")
    # using Clang
elseif("${COMPILER_ID}" STREQUAL "GNU")
    # using GCC
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wconversion -pedantic -Werror")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -g0 -O3 -march=native -mtune=native -ftree-vectorize -funroll-loops -flto -flto-partition=none -ffat-lto-objects")
    # set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -ftree-parallelize-loops=32")
    # set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fopenmp")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -mavx")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -mprefer-vector-width=512")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -ffast-math")
    # set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fprofile-generate")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fprofile-use")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -pg")
    # set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -ggdb3")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb3")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,-O2,--sort-common,--as-needed,-z,relro,-z,now,--copy-dt-needed-entries,-flto")
    #target_link_libraries(${PROJECT_NAME} stdc++fs)
    add_definitions(-D__GNU_GCC__)
elseif("${COMPILER_ID}" STREQUAL "Intel")
    # using Intel C++
elseif("${COMPILER_ID}" STREQUAL "MSVC")
    # using Visual Studio C++
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
endif("${COMPILER_ID}" STREQUAL "Clang")

function(qcai2_enable_linker_map target)
    if(MSVC)
        target_link_options(${target} PRIVATE
            "/MAP:${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(${target} PRIVATE
            "LINKER:-Map,${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
    endif()
endfunction()

set(QCAI2_EXTERNAL_LIST_SEPARATOR "|")
set(QCAI2_SCRIPT_LIST_SEPARATOR "__QCAI2_LIST_SEPARATOR__")
set(QCAI2_SCRIPT_VALUE_SEPARATOR "__QCAI2_VALUE_SEPARATOR__")

function(qcai2_append_defined_cmake_arg out_var cmake_var cmake_type)
    set(qcai2_args "${${out_var}}")
    if(DEFINED ${cmake_var} AND NOT "${${cmake_var}}" STREQUAL "")
        string(REPLACE ";" "${QCAI2_EXTERNAL_LIST_SEPARATOR}" qcai2_var_value "${${cmake_var}}")
        list(APPEND qcai2_args "-D${cmake_var}:${cmake_type}=${qcai2_var_value}")
    endif()
    set(${out_var} "${qcai2_args}" PARENT_SCOPE)
endfunction()

function(qcai2_collect_external_cmake_args out_var)
    get_cmake_property(qcai2_all_vars VARIABLES)
    set(qcai2_cmake_args)

    foreach(qcai2_var IN LISTS qcai2_all_vars)
        if(qcai2_var MATCHES "^CMAKE_[A-Z0-9_]*FLAGS($|_.*)")
            qcai2_append_defined_cmake_arg(qcai2_cmake_args "${qcai2_var}" STRING)
        endif()
    endforeach()

    foreach(qcai2_var IN ITEMS
            CMAKE_BUILD_TYPE
            CMAKE_MAKE_PROGRAM
            CMAKE_TOOLCHAIN_FILE
            VCPKG_TARGET_TRIPLET
            VCPKG_HOST_TRIPLET
            CMAKE_SYSROOT
            CMAKE_FIND_ROOT_PATH
            CMAKE_SYSTEM_NAME
            CMAKE_SYSTEM_VERSION
            CMAKE_SYSTEM_PROCESSOR
            CMAKE_C_COMPILER
            CMAKE_CXX_COMPILER
            CMAKE_C_COMPILER_TARGET
            CMAKE_CXX_COMPILER_TARGET
            CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN
            CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN
            CMAKE_AR
            CMAKE_RANLIB
            CMAKE_LINKER
            CMAKE_NM
            CMAKE_OBJDUMP
            CMAKE_OBJCOPY
            CMAKE_STRIP
            CMAKE_MT
            CMAKE_RC_COMPILER
            CMAKE_OSX_ARCHITECTURES
            CMAKE_OSX_DEPLOYMENT_TARGET
            CMAKE_OSX_SYSROOT
            CMAKE_APPLE_SILICON_PROCESSOR
            CMAKE_MSVC_RUNTIME_LIBRARY)
        if(qcai2_var STREQUAL "CMAKE_MAKE_PROGRAM" OR qcai2_var STREQUAL "CMAKE_TOOLCHAIN_FILE" OR
            qcai2_var STREQUAL "CMAKE_C_COMPILER" OR qcai2_var STREQUAL "CMAKE_CXX_COMPILER" OR
            qcai2_var STREQUAL "CMAKE_AR" OR qcai2_var STREQUAL "CMAKE_RANLIB" OR
            qcai2_var STREQUAL "CMAKE_LINKER" OR qcai2_var STREQUAL "CMAKE_NM" OR
            qcai2_var STREQUAL "CMAKE_OBJDUMP" OR qcai2_var STREQUAL "CMAKE_OBJCOPY" OR
            qcai2_var STREQUAL "CMAKE_STRIP" OR qcai2_var STREQUAL "CMAKE_MT" OR
            qcai2_var STREQUAL "CMAKE_RC_COMPILER")
            qcai2_append_defined_cmake_arg(qcai2_cmake_args "${qcai2_var}" FILEPATH)
        else()
            qcai2_append_defined_cmake_arg(qcai2_cmake_args "${qcai2_var}" STRING)
        endif()
    endforeach()

    list(REMOVE_DUPLICATES qcai2_cmake_args)
    list(SORT qcai2_cmake_args)
    set(${out_var} "${qcai2_cmake_args}" PARENT_SCOPE)
endfunction()

function(qcai2_collect_external_generator_args out_var)
    set(qcai2_generator_args)

    foreach(qcai2_var IN ITEMS CMAKE_GENERATOR CMAKE_GENERATOR_PLATFORM CMAKE_GENERATOR_TOOLSET CMAKE_GENERATOR_INSTANCE)
        if(DEFINED ${qcai2_var} AND NOT "${${qcai2_var}}" STREQUAL "")
            list(APPEND qcai2_generator_args ${qcai2_var} "${${qcai2_var}}")
        endif()
    endforeach()

    set(${out_var} "${qcai2_generator_args}" PARENT_SCOPE)
endfunction()

function(qcai2_collect_clang_analyzer_cmake_args out_var)
    set(qcai2_cmake_args
        "-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON"
    )

    if(DEFINED WITH_TESTS AND NOT "${WITH_TESTS}" STREQUAL "")
        list(APPEND qcai2_cmake_args "-DWITH_TESTS:BOOL=${WITH_TESTS}")
    endif()

    foreach(qcai2_var IN ITEMS
            CMAKE_BUILD_TYPE
            CMAKE_PREFIX_PATH
            CMAKE_FIND_ROOT_PATH
            CMAKE_SYSTEM_NAME
            CMAKE_SYSTEM_VERSION
            CMAKE_SYSTEM_PROCESSOR
            CMAKE_OSX_ARCHITECTURES
            CMAKE_OSX_DEPLOYMENT_TARGET
            CMAKE_OSX_SYSROOT
            CMAKE_APPLE_SILICON_PROCESSOR
            CMAKE_MSVC_RUNTIME_LIBRARY
            VCPKG_TARGET_TRIPLET
            VCPKG_HOST_TRIPLET)
        if(DEFINED ${qcai2_var} AND NOT "${${qcai2_var}}" STREQUAL "")
            string(REPLACE ";" "${QCAI2_SCRIPT_VALUE_SEPARATOR}" qcai2_var_value "${${qcai2_var}}")
            list(APPEND qcai2_cmake_args "-D${qcai2_var}:STRING=${qcai2_var_value}")
        endif()
    endforeach()

    foreach(qcai2_var IN ITEMS
            QtCreator_DIR
            Qt6_DIR
            CMAKE_TOOLCHAIN_FILE
            CMAKE_SYSROOT)
        if(DEFINED ${qcai2_var} AND NOT "${${qcai2_var}}" STREQUAL "")
            string(REPLACE ";" "${QCAI2_SCRIPT_VALUE_SEPARATOR}" qcai2_var_value "${${qcai2_var}}")
            list(APPEND qcai2_cmake_args "-D${qcai2_var}:PATH=${qcai2_var_value}")
        endif()
    endforeach()

    set(${out_var} "${qcai2_cmake_args}" PARENT_SCOPE)
endfunction()

function(qcai2_collect_target_cpp_sources out_var target)
    get_target_property(qcai2_target_sources ${target} SOURCES)
    get_target_property(qcai2_target_source_dir ${target} SOURCE_DIR)
    set(qcai2_cpp_sources)

    foreach(qcai2_source IN LISTS qcai2_target_sources)
        if(qcai2_source MATCHES "^\\$<")
            continue()
        endif()

        if(IS_ABSOLUTE "${qcai2_source}")
            set(qcai2_source_path "${qcai2_source}")
        else()
            set(qcai2_source_path "${qcai2_target_source_dir}/${qcai2_source}")
        endif()

        if(qcai2_source_path MATCHES "\\.(c|cc|cpp|cxx|mm|m)$")
            list(APPEND qcai2_cpp_sources "${qcai2_source_path}")
        endif()
    endforeach()

    set(${out_var} "${qcai2_cpp_sources}" PARENT_SCOPE)
endfunction()

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "${CMAKE_C_FLAGS_DEBUG_INIT}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "${CMAKE_C_FLAGS_RELEASE_INIT}")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL}")
set(CMAKE_CXX_FLAGS_MINSIZEREL_INIT "${CMAKE_C_FLAGS_MINSIZEREL_INIT}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "${CMAKE_C_FLAGS_RELWITHDEBINFO_INIT}")

find_package(QtCreator REQUIRED COMPONENTS Core TextEditor ProjectExplorer)
find_package(Qt6 COMPONENTS Widgets Network Sql REQUIRED)

add_subdirectory(src/src/util/migrations)

option(QCAI2_USE_SYSTEM_LIBARCHIVE "Prefer system libarchive when available" ON)
set(QCAI2_LIBARCHIVE_VERSION "3.8.6")
find_package(LibArchive QUIET)
find_package(LibLZMA QUIET)

add_library(qcai2_libarchive INTERFACE)
add_library(qcai2_version_info INTERFACE)

target_compile_definitions(qcai2_version_info INTERFACE
    QCAI2_CURRENT_VERSION="${QCAI2_VERSION_COMPAT}"
    QCAI2_CURRENT_BUILD_SUFFIX="${QCAI2_BUILD_SUFFIX}"
)

qcai2_collect_migration_files(QCAI2_MIGRATION_FILES)
qcai2_collect_external_cmake_args(QCAI2_EXTERNAL_CMAKE_ARGS)
qcai2_collect_external_generator_args(QCAI2_EXTERNAL_GENERATOR_ARGS)

set(QCAI2_LIBARCHIVE_C_WARNING_FLAGS "")
set(QCAI2_LIBARCHIVE_CXX_WARNING_FLAGS "")
set(QCAI2_LIBARCHIVE_WARNING_CMAKE_ARGS "")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(QCAI2_LIBARCHIVE_C_WARNING_FLAGS "${CMAKE_C_FLAGS} -Wno-error")
    list(APPEND QCAI2_LIBARCHIVE_WARNING_CMAKE_ARGS
        "-DCMAKE_C_FLAGS:STRING=${QCAI2_LIBARCHIVE_C_WARNING_FLAGS}")
    set(QCAI2_LIBARCHIVE_CXX_WARNING_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error")
    list(APPEND QCAI2_LIBARCHIVE_WARNING_CMAKE_ARGS
        "-DCMAKE_CXX_FLAGS:STRING=${QCAI2_LIBARCHIVE_CXX_WARNING_FLAGS}")
endif()

if(QCAI2_USE_SYSTEM_LIBARCHIVE AND LibArchive_FOUND)
    target_link_libraries(qcai2_libarchive INTERFACE LibArchive::LibArchive)
else()
    if(NOT LibLZMA_FOUND)
        message(FATAL_ERROR "External libarchive build requires LibLZMA for .tar.xz migration backups.")
    endif()

    set(QCAI2_LIBARCHIVE_INSTALL_DIR "${CMAKE_BINARY_DIR}/third_party/libarchive")
    set(QCAI2_LIBARCHIVE_PREFIX_DIR "${CMAKE_BINARY_DIR}/third_party/libarchive-prefix")
    set(QCAI2_LIBARCHIVE_STATIC_LIB
        "${QCAI2_LIBARCHIVE_INSTALL_DIR}/${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}archive${CMAKE_STATIC_LIBRARY_SUFFIX}")

    ExternalProject_Add(qcai2_libarchive_ep
        PREFIX "${QCAI2_LIBARCHIVE_PREFIX_DIR}"
        INSTALL_DIR "${QCAI2_LIBARCHIVE_INSTALL_DIR}"
        GIT_REPOSITORY https://github.com/libarchive/libarchive.git
        GIT_TAG "v${QCAI2_LIBARCHIVE_VERSION}"
        GIT_SHALLOW TRUE
        UPDATE_DISCONNECTED TRUE
        BUILD_BYPRODUCTS "${QCAI2_LIBARCHIVE_STATIC_LIB}"
        LIST_SEPARATOR "${QCAI2_EXTERNAL_LIST_SEPARATOR}"
        ${QCAI2_EXTERNAL_GENERATOR_ARGS}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=OFF
            -DBUILD_TESTING=OFF
            -DENABLE_TEST=OFF
            -DENABLE_OPENSSL=OFF
            -DENABLE_LIBXML2=OFF
            -DENABLE_EXPAT=OFF
            -DENABLE_MBEDTLS=OFF
            -DENABLE_NETTLE=OFF
            -DENABLE_CNG=OFF
            -DENABLE_WIN32_XMLLITE=OFF
            -DENABLE_ZLIB=OFF
            -DENABLE_BZip2=OFF
            -DENABLE_ZSTD=OFF
            -DENABLE_LZ4=OFF
            -DENABLE_LZO=OFF
            -DENABLE_LIBB2=OFF
            -DENABLE_ICONV=OFF
            -DENABLE_ACL=OFF
            -DENABLE_XATTR=OFF
            -DENABLE_LZMA=ON
            -DENABLE_CPIO=OFF
            -DENABLE_CAT=OFF
            -DENABLE_TAR=OFF
            -DENABLE_UNZIP=OFF
            -DENABLE_INSTALL=ON
            -DENABLE_WERROR=OFF
            ${QCAI2_EXTERNAL_CMAKE_ARGS}
            ${QCAI2_LIBARCHIVE_WARNING_CMAKE_ARGS}
    )

    add_library(qcai2_libarchive_imported STATIC IMPORTED GLOBAL)
    set_target_properties(qcai2_libarchive_imported PROPERTIES
        IMPORTED_LOCATION "${QCAI2_LIBARCHIVE_STATIC_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${QCAI2_LIBARCHIVE_INSTALL_DIR}/include"
    )
    add_dependencies(qcai2_libarchive_imported qcai2_libarchive_ep)
    if(WIN32)
        target_compile_definitions(qcai2_libarchive_imported INTERFACE LIBARCHIVE_STATIC)
    endif()
    target_link_libraries(qcai2_libarchive INTERFACE qcai2_libarchive_imported LibLZMA::LibLZMA)
endif()

# Add a CMake option that enables building your plugin with tests.
# You don't want your released plugin binaries to contain tests,
# so make that default to 'NO'.
# Enable tests by passing -DWITH_TESTS=ON to CMake.
option(WITH_TESTS "Builds with tests" NO)

if(WITH_TESTS)
  # Look for QtTest
  find_package(Qt6 REQUIRED COMPONENTS Test)

  # Tell CMake functions like add_qtc_plugin about the QtTest component.
  set(IMPLICIT_DEPENDS Qt::Test)

  # Enable ctest for auto tests.
  enable_testing()

  add_subdirectory(src/tests)
endif()

add_subdirectory(lib/qtmcp)

add_qtc_plugin(qcai2
  PLUGIN_PATH
    plugins/${IDE_VERSION}
  PLUGIN_DEPENDS
    QtCreator::Core
    QtCreator::TextEditor
    QtCreator::ProjectExplorer
  DEPENDS
    Qt::Widgets
    Qt::Network
    QtCreator::ExtensionSystem
    QtCreator::Utils
  SOURCES
    qcai2.json.in
    src/qcai2constants.h
    src/qcai2tr.h
    src/resources/aiagentplugin.qrc
    src/src/ai_agent_plugin.h
    src/src/ai_agent_plugin.cpp
    src/src/agent_controller.h
    src/src/agent_controller.cpp
    src/src/ui/agent_dock_widget.h
    src/src/ui/agent_dock_widget.cpp
    src/src/linked_files/agent_dock_linked_files_controller.h
    src/src/linked_files/agent_dock_linked_files_controller.cpp
    src/src/session/agent_dock_session_controller.h
    src/src/session/agent_dock_session_controller.cpp
    src/src/ui/agent_dock_widget.ui
    src/src/commands/slash_command_registry.h
    src/src/commands/slash_command_registry.cpp
    src/src/commands/hello_command.cpp
    src/src/commands/compact_command.cpp
    src/src/commands/explain_command.cpp
    src/src/commands/refactor_command.cpp
    src/src/commands/test_command.cpp
    src/src/goal/goal_special_handler.h
    src/src/goal/goal_text_edit.h
    src/src/goal/goal_text_edit.cpp
    src/src/goal/file_reference_goal_handler.h
    src/src/goal/file_reference_goal_handler.cpp
    src/src/goal/linked_files_list_widget.h
    src/src/goal/linked_files_list_widget.cpp
    src/src/goal/slash_command_goal_handler.h
    src/src/goal/slash_command_goal_handler.cpp
    src/src/context/editor_context.h
    src/src/context/editor_context.cpp
    src/src/context/chat_context.h
    src/src/context/chat_context.cpp
    src/src/context/chat_context_store.h
    src/src/context/chat_context_store.cpp
    src/src/context/chat_context_manager.h
    src/src/context/chat_context_manager.cpp
    src/src/progress/agent_progress.h
    src/src/progress/agent_progress.cpp
    src/src/progress/provider_progress_adapter.h
    src/src/progress/provider_progress_adapter.cpp
    src/src/progress/agent_status.h
    src/src/progress/agent_status.cpp
    src/src/progress/agent_status_formatter.h
    src/src/progress/agent_status_formatter.cpp
    src/src/progress/agent_status_renderer.h
    src/src/progress/agent_status_renderer.cpp
    src/src/progress/agent_progress_tracker.h
    src/src/progress/agent_progress_tracker.cpp
    src/src/mcp/mcp_tool_manager.h
    src/src/mcp/mcp_tool_manager.cpp
    src/src/tools/i_tool.h
    src/src/tools/tool_registry.h
    src/src/tools/tool_registry.cpp
    src/src/tools/file_tools.h
    src/src/tools/file_tools.cpp
    src/src/tools/create_file_tool.h
    src/src/tools/create_file_tool.cpp
    src/src/tools/search_tools.h
    src/src/tools/search_tools.cpp
    src/src/tools/build_tools.h
    src/src/tools/build_tools.cpp
    src/src/tools/git_tools.h
    src/src/tools/git_tools.cpp
    src/src/tools/ide_tools.h
    src/src/tools/ide_tools.cpp
    src/src/tools/list_directory_tool.h
    src/src/tools/list_directory_tool.cpp
    src/src/tools/run_command_tool.h
    src/src/tools/run_command_tool.cpp
    src/src/tools/find_symbol_tool.h
    src/src/tools/find_symbol_tool.cpp
    src/src/providers/iai_provider.h
    src/src/providers/provider_usage.h
    src/src/providers/provider_usage.cpp
    src/src/providers/open_ai_compatible_provider.h
    src/src/providers/open_ai_compatible_provider.cpp
    src/src/providers/anthropic_provider.h
    src/src/providers/anthropic_provider.cpp
    src/src/providers/local_http_provider.h
    src/src/providers/local_http_provider.cpp
    src/src/providers/ollama_provider.h
    src/src/providers/ollama_provider.cpp
    src/src/providers/copilot_provider.h
    src/src/providers/copilot_provider.cpp
    src/src/models/agent_messages.h
    src/src/models/agent_messages.cpp
    src/src/models/tool_call.h
    src/src/models/tool_call.cpp
    src/src/safety/safety_policy.h
    src/src/safety/safety_policy.cpp
    src/src/util/json.h
    src/src/util/json.cpp
    src/src/util/output_capture_support.h
    src/src/util/output_capture_support.cpp
    src/src/util/ide_output_capture.h
    src/src/util/ide_output_capture.cpp
    src/src/util/migration.h
    src/src/util/migration.cpp
    ${QCAI2_MIGRATION_FILES}
    src/src/util/process_runner.h
    src/src/util/process_runner.cpp
    src/src/util/diff.h
    src/src/util/diff.cpp
    src/src/util/request_detailed_log.h
    src/src/util/request_detailed_log.cpp
    src/src/util/logger.h
    src/src/util/logger.cpp
    src/src/util/crash_handler.h
    src/src/util/crash_handler.cpp
    src/src/settings/settings.h
    src/src/settings/settings.cpp
    src/src/settings/settings_page.h
    src/src/settings/settings_page.cpp
    src/src/settings/mcp_servers_widget.h
    src/src/settings/mcp_servers_widget.cpp
    src/src/settings/settings_widget.ui
    src/src/completion/ai_completion_provider.h
    src/src/completion/ai_completion_provider.cpp
    src/src/completion/ai_completion_processor.h
    src/src/completion/ai_completion_processor.cpp
    src/src/completion/ghost_text_manager.h
    src/src/completion/ghost_text_manager.cpp
    src/src/diff/inline_diff_manager.h
    src/src/diff/inline_diff_manager.cpp
)

qcai2_enable_linker_map(qcai2)
target_link_libraries(qcai2 PRIVATE qcai2_libarchive qcai2_version_info qtmcp::qtmcp)

set(QCAI2_PLUGIN_INSTALL_DIR "plugins/${IDE_VERSION}")
set(QCAI2_SIDECAR_INSTALL_DIR "${QCAI2_PLUGIN_INSTALL_DIR}/qcai2/sidecar")

# Sidecar path for development builds
target_compile_definitions(qcai2 PRIVATE
    QCAI2_SIDECAR_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/sidecar/copilot-sidecar.js"
    QCAI2_QTCREATOR_IDE_VERSION="${IDE_VERSION}"
    QCAI2_INSTALLED_SIDECAR_RELATIVE_PATH="qcai2/sidecar/copilot-sidecar.js"
)

# Install sidecar files alongside the plugin
install(FILES
    src/sidecar/copilot-sidecar.js
    src/sidecar/package.json
    DESTINATION ${QCAI2_SIDECAR_INSTALL_DIR}
)

set(QCAI2_INSTALL_SIDECAR_DEPS_SCRIPT
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/install-sidecar-deps.cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/install-sidecar-deps.cmake"
    "${QCAI2_INSTALL_SIDECAR_DEPS_SCRIPT}"
    @ONLY
)
install(SCRIPT "${QCAI2_INSTALL_SIDECAR_DEPS_SCRIPT}")

add_custom_target(uninstall
    COMMAND xargs rm -vf < install_manifest.txt
    COMMENT "Removing installed files (based on install_manifest.txt)"
)

find_program(QCAI2_CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-18 clang-format-17)
find_program(QCAI2_GIT_EXECUTABLE NAMES git)
find_program(QCAI2_RUN_CLANG_TIDY_EXECUTABLE NAMES run-clang-tidy run-clang-tidy-18 run-clang-tidy-17)
find_program(QCAI2_CLANG_TIDY_EXECUTABLE NAMES clang-tidy clang-tidy-18 clang-tidy-17)
find_program(QCAI2_SCAN_BUILD_EXECUTABLE NAMES scan-build scan-build-18 scan-build-17)
find_program(QCAI2_CLANG_EXECUTABLE NAMES clang clang-18 clang-17)

file(RELATIVE_PATH QCAI2_BINARY_DIR_RELATIVE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
set(QCAI2_FORMAT_EXCLUDED_PREFIXES
    "${QCAI2_BINARY_DIR_RELATIVE};build/;cmake-build-debug/;cmake-build-release/;cmake-build-relwithdebinfo/;cmake-build-minsizerel/")
string(REPLACE ";" "\\;" QCAI2_FORMAT_EXCLUDED_PREFIXES_ARG "${QCAI2_FORMAT_EXCLUDED_PREFIXES}")

add_custom_target(format-changed-files
    COMMAND ${CMAKE_COMMAND}
        -DCLANG_FORMAT_EXECUTABLE=${QCAI2_CLANG_FORMAT_EXECUTABLE}
        -DGIT_EXECUTABLE=${QCAI2_GIT_EXECUTABLE}
        -DQCAI2_FORMAT_WORKTREE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DQCAI2_FORMAT_ALL=FALSE
        -DQCAI2_FORMAT_DRY_RUN=TRUE
        "-DQCAI2_FORMAT_EXCLUDED_PREFIXES=${QCAI2_FORMAT_EXCLUDED_PREFIXES_ARG}"
        -P "${QCAI2_FORMAT_FILES_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Checking formatting of changed project files with clang-format"
)

add_custom_target(format-all-files
    COMMAND ${CMAKE_COMMAND}
        -DCLANG_FORMAT_EXECUTABLE=${QCAI2_CLANG_FORMAT_EXECUTABLE}
        -DGIT_EXECUTABLE=${QCAI2_GIT_EXECUTABLE}
        -DQCAI2_FORMAT_WORKTREE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DQCAI2_FORMAT_ALL=TRUE
        -DQCAI2_FORMAT_DRY_RUN=TRUE
        "-DQCAI2_FORMAT_EXCLUDED_PREFIXES=${QCAI2_FORMAT_EXCLUDED_PREFIXES_ARG}"
        -P "${QCAI2_FORMAT_FILES_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Checking formatting of all project files with clang-format"
)

add_custom_target(format-changed-files-apply
    COMMAND ${CMAKE_COMMAND}
        -DCLANG_FORMAT_EXECUTABLE=${QCAI2_CLANG_FORMAT_EXECUTABLE}
        -DGIT_EXECUTABLE=${QCAI2_GIT_EXECUTABLE}
        -DQCAI2_FORMAT_WORKTREE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DQCAI2_FORMAT_ALL=FALSE
        -DQCAI2_FORMAT_DRY_RUN=FALSE
        "-DQCAI2_FORMAT_EXCLUDED_PREFIXES=${QCAI2_FORMAT_EXCLUDED_PREFIXES_ARG}"
        -P "${QCAI2_FORMAT_FILES_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Formatting changed project files with clang-format"
)

add_custom_target(format-all-files-apply
    COMMAND ${CMAKE_COMMAND}
        -DCLANG_FORMAT_EXECUTABLE=${QCAI2_CLANG_FORMAT_EXECUTABLE}
        -DGIT_EXECUTABLE=${QCAI2_GIT_EXECUTABLE}
        -DQCAI2_FORMAT_WORKTREE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DQCAI2_FORMAT_ALL=TRUE
        -DQCAI2_FORMAT_DRY_RUN=FALSE
        "-DQCAI2_FORMAT_EXCLUDED_PREFIXES=${QCAI2_FORMAT_EXCLUDED_PREFIXES_ARG}"
        -P "${QCAI2_FORMAT_FILES_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Formatting all project files with clang-format"
)

find_package(Doxygen QUIET)
find_package(Python3 COMPONENTS Interpreter QUIET)
find_program(QCAI2_SPHINX_EXECUTABLE NAMES sphinx-build)
find_program(QCAI2_LATEXMK_EXECUTABLE NAMES latexmk)

set(QCAI2_DOCS_TEMPLATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/docs")
set(QCAI2_DOCS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs")
set(QCAI2_DOCS_SPHINX_SOURCE_DIR "${QCAI2_DOCS_BUILD_DIR}/sphinx")
set(QCAI2_DOCS_SPHINX_HTML_DIR "${QCAI2_DOCS_BUILD_DIR}/html")
set(QCAI2_DOCS_SPHINX_LATEX_DIR "${QCAI2_DOCS_BUILD_DIR}/latex")
set(QCAI2_DOCS_DOXYGEN_OUTPUT_DIR "${QCAI2_DOCS_BUILD_DIR}/doxygen")
set(QCAI2_DOCS_DOXYGEN_XML_DIR "${QCAI2_DOCS_DOXYGEN_OUTPUT_DIR}/xml")

file(MAKE_DIRECTORY "${QCAI2_DOCS_SPHINX_SOURCE_DIR}")

set(QCAI2_DOXYGEN_INPUT_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/qcai2constants.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/qcai2tr.h"
)
set(QCAI2_DOXYGEN_INPUTS "")
foreach(qcai2_doxygen_input IN LISTS QCAI2_DOXYGEN_INPUT_PATHS)
    string(APPEND QCAI2_DOXYGEN_INPUTS " \"${qcai2_doxygen_input}\"")
endforeach()
string(STRIP "${QCAI2_DOXYGEN_INPUTS}" QCAI2_DOXYGEN_INPUTS)

cmake_path(CONVERT "${QCAI2_DOCS_DOXYGEN_XML_DIR}" TO_CMAKE_PATH_LIST
    QCAI2_DOCS_DOXYGEN_XML_DIR_PY)
cmake_path(CONVERT "${CMAKE_CURRENT_SOURCE_DIR}" TO_CMAKE_PATH_LIST
    QCAI2_PROJECT_SOURCE_DIR_PY)

configure_file(
    "${QCAI2_DOCS_TEMPLATE_DIR}/Doxyfile.in"
    "${QCAI2_DOCS_BUILD_DIR}/Doxyfile"
    @ONLY
)
configure_file(
    "${QCAI2_DOCS_TEMPLATE_DIR}/conf.py.in"
    "${QCAI2_DOCS_SPHINX_SOURCE_DIR}/conf.py"
    @ONLY
)
configure_file(
    "${QCAI2_DOCS_TEMPLATE_DIR}/index.rst"
    "${QCAI2_DOCS_SPHINX_SOURCE_DIR}/index.rst"
    COPYONLY
)
configure_file(
    "${QCAI2_DOCS_TEMPLATE_DIR}/overview.rst"
    "${QCAI2_DOCS_SPHINX_SOURCE_DIR}/overview.rst"
    COPYONLY
)

add_custom_target(doc
    COMMAND ${CMAKE_COMMAND}
        -DDOXYGEN_EXECUTABLE=${DOXYGEN_EXECUTABLE}
        -DSPHINX_EXECUTABLE=${QCAI2_SPHINX_EXECUTABLE}
        -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
        -DDOXYFILE_PATH=${QCAI2_DOCS_BUILD_DIR}/Doxyfile
        -DSPHINX_SOURCE_DIR=${QCAI2_DOCS_SPHINX_SOURCE_DIR}
        -DSPHINX_HTML_DIR=${QCAI2_DOCS_SPHINX_HTML_DIR}
        -DSPHINX_LATEX_DIR=${QCAI2_DOCS_SPHINX_LATEX_DIR}
        -DLATEXMK_EXECUTABLE=${QCAI2_LATEXMK_EXECUTABLE}
        -DDOCS_LATEX_FILE=qcai2.tex
        -DDOCS_PDF_FILE=qcai2.pdf
        -P "${QCAI2_BUILD_DOCS_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Generating HTML and PDF documentation with Doxygen and Sphinx"
)

set(QCAI2_CLANG_TIDY_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-clang-tidy.cmake")
set(QCAI2_CLANG_STATIC_ANALYZER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-clang-static-analyzer.cmake")
set(QCAI2_CLANG_STATIC_ANALYZER_ARGS_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/clang-static-analyzer-args.cmake")

if(NOT EXISTS "${QCAI2_CLANG_STATIC_ANALYZER_ARGS_FILE}")
    message(FATAL_ERROR
        "Missing clang static analyzer args file: ${QCAI2_CLANG_STATIC_ANALYZER_ARGS_FILE}")
endif()
include("${QCAI2_CLANG_STATIC_ANALYZER_ARGS_FILE}")

if(NOT DEFINED QCAI2_CLANG_STATIC_ANALYZER_SCAN_BUILD_ARGS)
    set(QCAI2_CLANG_STATIC_ANALYZER_SCAN_BUILD_ARGS)
endif()

qcai2_collect_target_cpp_sources(QCAI2_CLANG_TIDY_SOURCES qcai2)
set(QCAI2_CLANG_ANALYZER_BUILD_TARGETS qcai2)
foreach(qcai2_test_target IN ITEMS tst_qcai2_json tst_qcai2_toolcall tst_qcai2_agent_messages tst_qcai2_provider_usage tst_qcai2_agent_progress tst_qcai2_chat_context tst_qcai2_slash_commands tst_qcai2_safety_policy tst_qcai2_mcp_config tst_qcai2_tool_registry tst_qcai2_migration)
    if(TARGET ${qcai2_test_target})
        qcai2_collect_target_cpp_sources(qcai2_test_sources ${qcai2_test_target})
        list(APPEND QCAI2_CLANG_TIDY_SOURCES ${qcai2_test_sources})
        list(APPEND QCAI2_CLANG_ANALYZER_BUILD_TARGETS ${qcai2_test_target})
    endif()
endforeach()
list(REMOVE_DUPLICATES QCAI2_CLANG_TIDY_SOURCES)
list(REMOVE_DUPLICATES QCAI2_CLANG_ANALYZER_BUILD_TARGETS)
qcai2_collect_clang_analyzer_cmake_args(QCAI2_CLANG_ANALYZER_CMAKE_ARGS)
string(REPLACE ";" "${QCAI2_SCRIPT_LIST_SEPARATOR}"
    QCAI2_CLANG_TIDY_SOURCES_ARG "${QCAI2_CLANG_TIDY_SOURCES}")
string(REPLACE ";" "${QCAI2_SCRIPT_LIST_SEPARATOR}"
    QCAI2_CLANG_ANALYZER_BUILD_TARGETS_ARG "${QCAI2_CLANG_ANALYZER_BUILD_TARGETS}")
string(REPLACE ";" "${QCAI2_SCRIPT_LIST_SEPARATOR}"
    QCAI2_CLANG_ANALYZER_CMAKE_ARGS_ARG "${QCAI2_CLANG_ANALYZER_CMAKE_ARGS}")
string(REPLACE ";" "${QCAI2_SCRIPT_LIST_SEPARATOR}"
    QCAI2_CLANG_STATIC_ANALYZER_SCAN_BUILD_ARGS_ARG
    "${QCAI2_CLANG_STATIC_ANALYZER_SCAN_BUILD_ARGS}")

add_custom_target(clang-tidy
    DEPENDS ${QCAI2_CLANG_ANALYZER_BUILD_TARGETS}
    COMMAND ${CMAKE_COMMAND}
        "-DRUN_CLANG_TIDY_EXECUTABLE=${QCAI2_RUN_CLANG_TIDY_EXECUTABLE}"
        "-DCLANG_TIDY_EXECUTABLE=${QCAI2_CLANG_TIDY_EXECUTABLE}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DHEADER_FILTER=^${CMAKE_CURRENT_SOURCE_DIR}/src/"
        "-DSOURCE_FILES=${QCAI2_CLANG_TIDY_SOURCES_ARG}"
        -P "${QCAI2_CLANG_TIDY_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Running clang-tidy for qcai2 sources"
)

add_custom_target(clang-static-analyzer
    COMMAND ${CMAKE_COMMAND}
        "-DSCAN_BUILD_EXECUTABLE=${QCAI2_SCAN_BUILD_EXECUTABLE}"
        "-DCLANG_EXECUTABLE=${QCAI2_CLANG_EXECUTABLE}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DANALYZER_BUILD_DIR=${CMAKE_BINARY_DIR}/clang-static-analyzer-build"
        "-DANALYZER_REPORT_DIR=${CMAKE_BINARY_DIR}/clang-static-analyzer-report"
        "-DANALYZER_GENERATOR=${CMAKE_GENERATOR}"
        "-DANALYZER_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DANALYZER_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
        "-DANALYZER_BUILD_TARGETS=${QCAI2_CLANG_ANALYZER_BUILD_TARGETS_ARG}"
        "-DANALYZER_CMAKE_ARGS=${QCAI2_CLANG_ANALYZER_CMAKE_ARGS_ARG}"
        "-DANALYZER_SCAN_BUILD_ARGS=${QCAI2_CLANG_STATIC_ANALYZER_SCAN_BUILD_ARGS_ARG}"
        -P "${QCAI2_CLANG_STATIC_ANALYZER_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Running Clang Static Analyzer for qcai2"
)

add_custom_target(clang-analyze
    DEPENDS clang-tidy clang-static-analyzer
)

# Enable the Run button in Qt Creator
get_target_property(QtCreatorCorePath QtCreator::Core LOCATION)
find_program(QtCreatorExecutable
  NAMES
    qtcreator "Qt Creator"
  PATHS
    "${QtCreatorCorePath}/../../../bin"
    "${QtCreatorCorePath}/../../../MacOS"
  NO_DEFAULT_PATH
)
if(QtCreatorExecutable)
  add_custom_target(RunQtCreator
    COMMAND ${QtCreatorExecutable} -pluginpath $<TARGET_FILE_DIR:qcai2>
    DEPENDS qcai2
  )
  set_target_properties(RunQtCreator PROPERTIES FOLDER "qtc_runnable")
endif()

set(SRC_IDE
    .clang-format
    .github/workflows/README.md
    .github/workflows/build_cmake.yml
    ai/ai-context.md
    LICENSE
    README.md
    CHANGELOG.md
    VERSION.txt
    docs/CODING_STYLE.md
    .gitignore
    .clang-tidy
    cmake/format-files.cmake
    cmake/build-docs.cmake
    cmake/run-clang-tidy.cmake
    cmake/run-clang-static-analyzer.cmake
    scripts/install-format-hook.sh
    src/docs/Doxyfile.in
    src/docs/conf.py.in
    src/docs/index.rst
    src/docs/overview.rst
    cmake/install-sidecar-deps.cmake
    qcai2.json.in
    src/qcai2_uk_UA.ts
    src/sidecar/copilot-sidecar.js
    src/sidecar/package.json
    src/tests/CMakeLists.txt
    cmake/clang-static-analyzer-args.cmake
)

add_custom_target(target_for_ide SOURCES ${SRC_IDE})

set(SRC_TESTS
    src/tests/tst_json.cpp
    src/tests/tst_toolcall.cpp
    src/tests/tst_slash_commands.cpp
    src/tests/tst_migration.cpp
)

add_custom_target(tests SOURCES ${SRC_TESTS})
