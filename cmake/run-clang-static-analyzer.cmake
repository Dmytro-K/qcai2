if("${SCAN_BUILD_EXECUTABLE}" STREQUAL "")
    message(FATAL_ERROR "scan-build was not found.")
endif()

if("${CLANG_EXECUTABLE}" STREQUAL "")
    message(FATAL_ERROR "clang was not found.")
endif()

string(REPLACE "__QCAI2_LIST_SEPARATOR__" ";" ANALYZER_CMAKE_ARGS "${ANALYZER_CMAKE_ARGS}")
string(REPLACE "__QCAI2_LIST_SEPARATOR__" ";" ANALYZER_SCAN_BUILD_ARGS "${ANALYZER_SCAN_BUILD_ARGS}")

set(qcai2_initial_cache_file "${ANALYZER_BUILD_DIR}/analyzer-initial-cache.cmake")
set(qcai2_initial_cache_content "")
foreach(qcai2_arg IN LISTS ANALYZER_CMAKE_ARGS)
    string(REPLACE "__QCAI2_VALUE_SEPARATOR__" ";" qcai2_restored_arg "${qcai2_arg}")
    if(NOT qcai2_restored_arg MATCHES "^-D([^:]+):([^=]+)=(.*)$")
        message(FATAL_ERROR "Unsupported analyzer CMake argument format: ${qcai2_restored_arg}")
    endif()

    set(qcai2_cache_name "${CMAKE_MATCH_1}")
    set(qcai2_cache_type "${CMAKE_MATCH_2}")
    set(qcai2_cache_value "${CMAKE_MATCH_3}")
    string(REPLACE "\\" "\\\\" qcai2_cache_value "${qcai2_cache_value}")
    string(REPLACE "\"" "\\\"" qcai2_cache_value "${qcai2_cache_value}")
    string(APPEND qcai2_initial_cache_content
        "set(${qcai2_cache_name} \"${qcai2_cache_value}\" CACHE ${qcai2_cache_type} \"\" FORCE)\n")
endforeach()
string(REPLACE "__QCAI2_LIST_SEPARATOR__" ";" ANALYZER_BUILD_TARGETS "${ANALYZER_BUILD_TARGETS}")

file(REMOVE_RECURSE "${ANALYZER_BUILD_DIR}" "${ANALYZER_REPORT_DIR}")
file(MAKE_DIRECTORY "${ANALYZER_BUILD_DIR}")
file(MAKE_DIRECTORY "${ANALYZER_REPORT_DIR}")
file(WRITE "${qcai2_initial_cache_file}" "${qcai2_initial_cache_content}")

set(qcai2_configure_command
    "${SCAN_BUILD_EXECUTABLE}"
    --keep-empty
    -o "${ANALYZER_REPORT_DIR}"
    --use-analyzer "${CLANG_EXECUTABLE}"
)
foreach(qcai2_arg IN LISTS ANALYZER_SCAN_BUILD_ARGS)
    list(APPEND qcai2_configure_command "${qcai2_arg}")
endforeach()
list(APPEND qcai2_configure_command
    "${CMAKE_COMMAND}"
    -C "${qcai2_initial_cache_file}"
    -S "${SOURCE_DIR}"
    -B "${ANALYZER_BUILD_DIR}"
    -G "${ANALYZER_GENERATOR}"
)

if(NOT "${ANALYZER_GENERATOR_PLATFORM}" STREQUAL "")
    list(APPEND qcai2_configure_command -A "${ANALYZER_GENERATOR_PLATFORM}")
endif()

if(NOT "${ANALYZER_GENERATOR_TOOLSET}" STREQUAL "")
    list(APPEND qcai2_configure_command -T "${ANALYZER_GENERATOR_TOOLSET}")
endif()

execute_process(
    COMMAND ${qcai2_configure_command}
    WORKING_DIRECTORY "${SOURCE_DIR}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE qcai2_result
)

if(NOT qcai2_result EQUAL 0)
    message(FATAL_ERROR "scan-build configure step failed with exit code ${qcai2_result}.")
endif()

set(qcai2_build_command
    "${SCAN_BUILD_EXECUTABLE}"
    --keep-empty
    -o "${ANALYZER_REPORT_DIR}"
    --use-analyzer "${CLANG_EXECUTABLE}"
)
foreach(qcai2_arg IN LISTS ANALYZER_SCAN_BUILD_ARGS)
    list(APPEND qcai2_build_command "${qcai2_arg}")
endforeach()
list(APPEND qcai2_build_command
    "${CMAKE_COMMAND}"
    --build "${ANALYZER_BUILD_DIR}"
    --parallel
)

foreach(qcai2_target IN LISTS ANALYZER_BUILD_TARGETS)
    list(APPEND qcai2_build_command --target "${qcai2_target}")
endforeach()

execute_process(
    COMMAND ${qcai2_build_command}
    WORKING_DIRECTORY "${SOURCE_DIR}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE qcai2_result
)

if(NOT qcai2_result EQUAL 0)
    message(FATAL_ERROR "scan-build build step failed with exit code ${qcai2_result}.")
endif()

message(STATUS "Clang Static Analyzer report directory: ${ANALYZER_REPORT_DIR}")
