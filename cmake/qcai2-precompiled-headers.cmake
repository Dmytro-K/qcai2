#[[
    Centralizes precompiled-header support for repository-owned C++ targets.
]]

option(QCAI2_ENABLE_PRECOMPILED_HEADERS
    "Enable precompiled headers for qcai2-owned C++ targets."
    ON)
option(QCAI2_ENABLE_TEST_PRECOMPILED_HEADERS
    "Enable precompiled headers for qcai2 test executables."
    OFF)

get_filename_component(QCAI2_PRECOMPILED_HEADERS_ROOT_DIR
    "${CMAKE_CURRENT_LIST_DIR}/.."
    ABSOLUTE)

set(QCAI2_CORE_PRECOMPILED_HEADERS_FILE
    "${QCAI2_PRECOMPILED_HEADERS_ROOT_DIR}/src/src/precompiled_headers.h")
set(QCAI2_TEST_PRECOMPILED_HEADERS_FILE
    "${QCAI2_PRECOMPILED_HEADERS_ROOT_DIR}/src/tests/precompiled_headers.h")
set(QCAI2_NETWORK_PRECOMPILED_HEADERS_FILE
    "${QCAI2_PRECOMPILED_HEADERS_ROOT_DIR}/lib/qtmcp/src/precompiled_headers.h")
set(QCAI2_QOMPI_CORE_PRECOMPILED_HEADERS_FILE
    "${QCAI2_PRECOMPILED_HEADERS_ROOT_DIR}/lib/qompi/src/precompiled_headers_core.h")
set(QCAI2_QOMPI_QT_PRECOMPILED_HEADERS_FILE
    "${QCAI2_PRECOMPILED_HEADERS_ROOT_DIR}/lib/qompi/src/precompiled_headers_qt.h")

function(qcai2_apply_precompiled_headers target_name header_path)
    if(NOT QCAI2_ENABLE_PRECOMPILED_HEADERS)
        return()
    endif()

    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "Cannot apply precompiled headers: target `${target_name}` does not exist.")
    endif()

    if(NOT EXISTS "${header_path}")
        message(FATAL_ERROR
            "Cannot apply precompiled headers to `${target_name}`: `${header_path}` does not exist.")
    endif()

    target_precompile_headers(${target_name} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${header_path}>"
    )
endfunction()

function(qcai2_apply_core_precompiled_headers target_name)
    qcai2_apply_precompiled_headers(${target_name} "${QCAI2_CORE_PRECOMPILED_HEADERS_FILE}")
endfunction()

function(qcai2_apply_test_precompiled_headers target_name)
    if(NOT QCAI2_ENABLE_TEST_PRECOMPILED_HEADERS)
        return()
    endif()

    qcai2_apply_precompiled_headers(${target_name} "${QCAI2_TEST_PRECOMPILED_HEADERS_FILE}")
endfunction()

function(qcai2_apply_network_precompiled_headers target_name)
    qcai2_apply_precompiled_headers(${target_name} "${QCAI2_NETWORK_PRECOMPILED_HEADERS_FILE}")
endfunction()

function(qcai2_apply_qompi_core_precompiled_headers target_name)
    qcai2_apply_precompiled_headers(${target_name} "${QCAI2_QOMPI_CORE_PRECOMPILED_HEADERS_FILE}")
endfunction()

function(qcai2_apply_qompi_qt_precompiled_headers target_name)
    qcai2_apply_precompiled_headers(${target_name} "${QCAI2_QOMPI_QT_PRECOMPILED_HEADERS_FILE}")
endfunction()
