if("${RUN_CLANG_TIDY_EXECUTABLE}" STREQUAL "" AND "${CLANG_TIDY_EXECUTABLE}" STREQUAL "")
    message(FATAL_ERROR "Neither run-clang-tidy nor clang-tidy was found.")
endif()

if(NOT EXISTS "${BUILD_DIR}/compile_commands.json")
    message(FATAL_ERROR "Missing ${BUILD_DIR}/compile_commands.json. Reconfigure the build directory first.")
endif()

string(REPLACE "__QCAI2_LIST_SEPARATOR__" ";" SOURCE_FILES "${SOURCE_FILES}")

set(qcai2_clang_tidy_build_dir "${BUILD_DIR}/clang-tidy")
file(MAKE_DIRECTORY "${qcai2_clang_tidy_build_dir}")
file(READ "${BUILD_DIR}/compile_commands.json" qcai2_compile_commands)
foreach(qcai2_unsupported_flag IN ITEMS
        " -mno-direct-extern-access"
        " -fconcepts-diagnostics-depth=8"
        " -fdeps-format=p1689r5"
        " -fmodules-ts")
    string(REPLACE "${qcai2_unsupported_flag}" "" qcai2_compile_commands "${qcai2_compile_commands}")
endforeach()
string(REGEX REPLACE " -fmodule-mapper=[^ \\\"]+" "" qcai2_compile_commands "${qcai2_compile_commands}")
file(WRITE "${qcai2_clang_tidy_build_dir}/compile_commands.json" "${qcai2_compile_commands}")

if("${RUN_CLANG_TIDY_EXECUTABLE}" STREQUAL "")
    foreach(qcai2_source IN LISTS SOURCE_FILES)
        execute_process(
            COMMAND "${CLANG_TIDY_EXECUTABLE}"
                -p "${qcai2_clang_tidy_build_dir}"
                "-header-filter=${HEADER_FILTER}"
                "${qcai2_source}"
            WORKING_DIRECTORY "${SOURCE_DIR}"
            COMMAND_ECHO STDOUT
            RESULT_VARIABLE qcai2_result
        )
        if(NOT qcai2_result EQUAL 0)
            message(FATAL_ERROR "clang-tidy failed for ${qcai2_source} with exit code ${qcai2_result}.")
        endif()
    endforeach()
    return()
endif()

execute_process(
    COMMAND "${RUN_CLANG_TIDY_EXECUTABLE}"
        -p "${qcai2_clang_tidy_build_dir}"
        "-header-filter=${HEADER_FILTER}"
        ${SOURCE_FILES}
    WORKING_DIRECTORY "${SOURCE_DIR}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE qcai2_result
)

if(NOT qcai2_result EQUAL 0)
    message(FATAL_ERROR "run-clang-tidy failed with exit code ${qcai2_result}.")
endif()
