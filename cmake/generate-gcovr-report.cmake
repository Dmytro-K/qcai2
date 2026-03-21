cmake_minimum_required(VERSION 3.28)

foreach(qcai2_required_var IN ITEMS
        QCAI2_GCOVR_EXECUTABLE
        QCAI2_GCOV_EXECUTABLE
        QCAI2_CTEST_EXECUTABLE
        QCAI2_COVERAGE_SOURCE_DIR
        QCAI2_COVERAGE_BUILD_DIR
        QCAI2_COVERAGE_OUTPUT_DIR)
    if(NOT DEFINED ${qcai2_required_var} OR "${${qcai2_required_var}}" STREQUAL "")
        message(FATAL_ERROR "Missing required variable: ${qcai2_required_var}")
    endif()
endforeach()

file(REMOVE_RECURSE "${QCAI2_COVERAGE_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${QCAI2_COVERAGE_OUTPUT_DIR}")

file(GLOB_RECURSE qcai2_old_coverage_data
    LIST_DIRECTORIES FALSE
    "${QCAI2_COVERAGE_BUILD_DIR}/*.gcda"
    "${QCAI2_COVERAGE_BUILD_DIR}/*.gcov"
)
if(qcai2_old_coverage_data)
    file(REMOVE ${qcai2_old_coverage_data})
endif()

execute_process(
    COMMAND "${QCAI2_CTEST_EXECUTABLE}" --test-dir "${QCAI2_COVERAGE_BUILD_DIR}" --output-on-failure
    WORKING_DIRECTORY "${QCAI2_COVERAGE_BUILD_DIR}"
    RESULT_VARIABLE qcai2_ctest_result
)
if(NOT qcai2_ctest_result EQUAL 0)
    message(FATAL_ERROR "Coverage run aborted because ctest failed with exit code ${qcai2_ctest_result}.")
endif()

set(QCAI2_GCOVR_COMMON_ARGS
    --root "${QCAI2_COVERAGE_SOURCE_DIR}"
    --object-directory "${QCAI2_COVERAGE_BUILD_DIR}"
    --gcov-executable "${QCAI2_GCOV_EXECUTABLE}"
    --exclude "${QCAI2_COVERAGE_SOURCE_DIR}/src/tests/.*"
    --gcov-ignore-errors=no_working_dir_found
)

execute_process(
    COMMAND "${QCAI2_GCOVR_EXECUTABLE}"
        ${QCAI2_GCOVR_COMMON_ARGS}
        --txt
        --output "${QCAI2_COVERAGE_OUTPUT_DIR}/coverage.txt"
    WORKING_DIRECTORY "${QCAI2_COVERAGE_BUILD_DIR}"
    RESULT_VARIABLE qcai2_gcovr_txt_result
)
if(NOT qcai2_gcovr_txt_result EQUAL 0)
    message(FATAL_ERROR "gcovr text report generation failed with exit code ${qcai2_gcovr_txt_result}.")
endif()

execute_process(
    COMMAND "${QCAI2_GCOVR_EXECUTABLE}"
        ${QCAI2_GCOVR_COMMON_ARGS}
        --xml-pretty
        --output "${QCAI2_COVERAGE_OUTPUT_DIR}/coverage.xml"
    WORKING_DIRECTORY "${QCAI2_COVERAGE_BUILD_DIR}"
    RESULT_VARIABLE qcai2_gcovr_xml_result
)
if(NOT qcai2_gcovr_xml_result EQUAL 0)
    message(FATAL_ERROR "gcovr XML report generation failed with exit code ${qcai2_gcovr_xml_result}.")
endif()

execute_process(
    COMMAND "${QCAI2_GCOVR_EXECUTABLE}"
        ${QCAI2_GCOVR_COMMON_ARGS}
        --html-details
        --output "${QCAI2_COVERAGE_OUTPUT_DIR}/coverage.html"
    WORKING_DIRECTORY "${QCAI2_COVERAGE_BUILD_DIR}"
    RESULT_VARIABLE qcai2_gcovr_html_result
)
if(NOT qcai2_gcovr_html_result EQUAL 0)
    message(FATAL_ERROR "gcovr HTML report generation failed with exit code ${qcai2_gcovr_html_result}.")
endif()

message(STATUS "gcovr coverage reports written to ${QCAI2_COVERAGE_OUTPUT_DIR}")
