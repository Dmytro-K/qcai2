if(NOT DEFINED QCAI2_FORMAT_WORKTREE_DIR OR QCAI2_FORMAT_WORKTREE_DIR STREQUAL "")
    set(QCAI2_FORMAT_WORKTREE_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()

if(NOT DEFINED CLANG_FORMAT_EXECUTABLE OR CLANG_FORMAT_EXECUTABLE STREQUAL "")
    find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-18 clang-format-17)
endif()
if(NOT CLANG_FORMAT_EXECUTABLE)
    message(FATAL_ERROR "clang-format executable not found")
endif()

if(NOT DEFINED GIT_EXECUTABLE OR GIT_EXECUTABLE STREQUAL "")
    find_program(GIT_EXECUTABLE NAMES git)
endif()
if(NOT GIT_EXECUTABLE)
    message(FATAL_ERROR "git executable not found")
endif()

set(QCAI2_FORMAT_SUPPORTED_EXTENSIONS
    .c
    .cc
    .cpp
    .cxx
    .h
    .hh
    .hpp
    .hxx
    .ipp
    .inl
    .m
    .mm
)

if(NOT DEFINED QCAI2_FORMAT_EXCLUDED_PREFIXES)
    set(QCAI2_FORMAT_EXCLUDED_PREFIXES
        build/
        cmake-build-debug/
        cmake-build-release/
        cmake-build-relwithdebinfo/
        cmake-build-minsizerel/
    )
endif()

function(qcai2_collect_changed_files out_var)
    set(all_paths "")
    foreach(command_args
            "diff;--name-only;--diff-filter=ACMR;--"
            "diff;--cached;--name-only;--diff-filter=ACMR;--"
            "ls-files;--others;--exclude-standard;--")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${QCAI2_FORMAT_WORKTREE_DIR}" ${command_args}
            OUTPUT_VARIABLE git_output
            ERROR_VARIABLE git_error
            RESULT_VARIABLE git_result
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT git_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to query changed files with git (${command_args}): ${git_error}")
        endif()
        if(NOT git_output STREQUAL "")
            string(REPLACE "\n" ";" output_list "${git_output}")
            list(APPEND all_paths ${output_list})
        endif()
    endforeach()

    list(REMOVE_DUPLICATES all_paths)
    set(filtered_paths "")
    foreach(relative_path IN LISTS all_paths)
        if(relative_path STREQUAL "")
            continue()
        endif()

        set(skip_path FALSE)
        foreach(prefix IN LISTS QCAI2_FORMAT_EXCLUDED_PREFIXES)
            if(prefix STREQUAL "")
                continue()
            endif()
            string(REGEX REPLACE "/+$" "" normalized_prefix "${prefix}")
            if(normalized_prefix STREQUAL "")
                continue()
            endif()
            string(FIND "${relative_path}" "${normalized_prefix}/" prefix_match)
            if(relative_path STREQUAL normalized_prefix OR prefix_match EQUAL 0)
                set(skip_path TRUE)
                break()
            endif()
        endforeach()
        if(skip_path)
            continue()
        endif()

        get_filename_component(extension "${relative_path}" LAST_EXT)
        string(TOLOWER "${extension}" extension)
        if(NOT extension IN_LIST QCAI2_FORMAT_SUPPORTED_EXTENSIONS)
            continue()
        endif()

        set(absolute_path "${QCAI2_FORMAT_WORKTREE_DIR}/${relative_path}")
        if(EXISTS "${absolute_path}")
            list(APPEND filtered_paths "${absolute_path}")
        endif()
    endforeach()

    set(${out_var} "${filtered_paths}" PARENT_SCOPE)
endfunction()

qcai2_collect_changed_files(files_to_format)

if(NOT files_to_format)
    message(STATUS "No changed files eligible for clang-format in ${QCAI2_FORMAT_WORKTREE_DIR}")
    return()
endif()

list(JOIN files_to_format "\n  " formatted_file_list)
message(STATUS "Formatting changed files:\n  ${formatted_file_list}")
execute_process(
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i --style=file --fallback-style=none ${files_to_format}
    WORKING_DIRECTORY "${QCAI2_FORMAT_WORKTREE_DIR}"
    RESULT_VARIABLE clang_format_result
    COMMAND_ECHO STDOUT
)

if(NOT clang_format_result EQUAL 0)
    message(FATAL_ERROR "clang-format failed with exit code ${clang_format_result}")
endif()
