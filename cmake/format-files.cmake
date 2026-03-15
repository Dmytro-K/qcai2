if(NOT DEFINED QCAI2_FORMAT_WORKTREE_DIR OR QCAI2_FORMAT_WORKTREE_DIR STREQUAL "")
    set(QCAI2_FORMAT_WORKTREE_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()

if(NOT DEFINED QCAI2_FORMAT_ALL OR QCAI2_FORMAT_ALL STREQUAL "")
    set(QCAI2_FORMAT_ALL FALSE)
endif()

if(NOT DEFINED QCAI2_FORMAT_DRY_RUN OR QCAI2_FORMAT_DRY_RUN STREQUAL "")
    set(QCAI2_FORMAT_DRY_RUN TRUE)
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

function(qcai2_filter_format_paths input_paths out_var)
    set(filtered_paths "")
    foreach(relative_path IN LISTS input_paths)
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

    list(REMOVE_DUPLICATES filtered_paths)
    set(${out_var} "${filtered_paths}" PARENT_SCOPE)
endfunction()

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

    qcai2_filter_format_paths("${all_paths}" filtered_paths)
    set(${out_var} "${filtered_paths}" PARENT_SCOPE)
endfunction()

function(qcai2_collect_all_files out_var)
    file(GLOB_RECURSE all_paths
        RELATIVE "${QCAI2_FORMAT_WORKTREE_DIR}"
        "${QCAI2_FORMAT_WORKTREE_DIR}/*")
    qcai2_filter_format_paths("${all_paths}" filtered_paths)
    set(${out_var} "${filtered_paths}" PARENT_SCOPE)
endfunction()

function(qcai2_format_file_to_path input_path output_path)
    execute_process(
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --style=file --fallback-style=none "${input_path}"
        WORKING_DIRECTORY "${QCAI2_FORMAT_WORKTREE_DIR}"
        OUTPUT_VARIABLE formatted_content
        ERROR_VARIABLE clang_format_error
        RESULT_VARIABLE clang_format_result
    )
    if(NOT clang_format_result EQUAL 0)
        message(FATAL_ERROR
            "clang-format failed for ${input_path} with exit code ${clang_format_result}: ${clang_format_error}")
    endif()
    file(WRITE "${output_path}" "${formatted_content}")
endfunction()

if(QCAI2_FORMAT_ALL)
    set(format_scope_label "all files")
    qcai2_collect_all_files(files_to_format)
else()
    set(format_scope_label "changed files")
    qcai2_collect_changed_files(files_to_format)
endif()

if(NOT files_to_format)
    message(STATUS "No ${format_scope_label} eligible for clang-format in ${QCAI2_FORMAT_WORKTREE_DIR}")
    return()
endif()

list(JOIN files_to_format "\n  " formatted_file_list)
if(QCAI2_FORMAT_DRY_RUN)
    message(STATUS "Dry-run checking ${format_scope_label}:\n  ${formatted_file_list}")

    string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef qcai2_format_temp_suffix)
    set(qcai2_format_temp_dir
        "${QCAI2_FORMAT_WORKTREE_DIR}/.qcai2-format-tmp-${qcai2_format_temp_suffix}")
    get_filename_component(qcai2_format_temp_dir_name "${qcai2_format_temp_dir}" NAME)
    file(MAKE_DIRECTORY "${qcai2_format_temp_dir}")

    set(qcai2_diff_output "")
    foreach(file_path IN LISTS files_to_format)
        file(RELATIVE_PATH relative_path "${QCAI2_FORMAT_WORKTREE_DIR}" "${file_path}")
        set(formatted_path "${qcai2_format_temp_dir}/${relative_path}")
        get_filename_component(formatted_dir "${formatted_path}" DIRECTORY)
        file(MAKE_DIRECTORY "${formatted_dir}")

        qcai2_format_file_to_path("${file_path}" "${formatted_path}")

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" diff --no-index --no-ext-diff
                    --src-prefix=a/ --dst-prefix=b/
                    -- "${relative_path}" "${qcai2_format_temp_dir_name}/${relative_path}"
            WORKING_DIRECTORY "${QCAI2_FORMAT_WORKTREE_DIR}"
            OUTPUT_VARIABLE file_diff
            ERROR_VARIABLE diff_error
            RESULT_VARIABLE diff_result
        )
        if(NOT diff_result EQUAL 0 AND NOT diff_result EQUAL 1)
            file(REMOVE_RECURSE "${qcai2_format_temp_dir}")
            message(FATAL_ERROR
                "Failed to diff formatted file ${relative_path}: ${diff_error}")
        endif()

        if(NOT file_diff STREQUAL "")
            string(REPLACE "${qcai2_format_temp_dir_name}/" "" file_diff "${file_diff}")
            string(APPEND qcai2_diff_output "${file_diff}\n")
        endif()
    endforeach()

    if(NOT qcai2_diff_output STREQUAL "")
        set(qcai2_diff_report "${qcai2_format_temp_dir}/clang-format.diff")
        file(WRITE "${qcai2_diff_report}" "${qcai2_diff_output}")
        execute_process(COMMAND "${CMAKE_COMMAND}" -E cat "${qcai2_diff_report}")
        file(REMOVE_RECURSE "${qcai2_format_temp_dir}")
        message(FATAL_ERROR
            "Formatting changes are required. Re-run with QCAI2_FORMAT_DRY_RUN=FALSE or use the *-apply target.")
    endif()

    file(REMOVE_RECURSE "${qcai2_format_temp_dir}")
    message(STATUS "No formatting changes required for ${format_scope_label}")
else()
    message(STATUS "Formatting ${format_scope_label}:\n  ${formatted_file_list}")
    execute_process(
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i --style=file --fallback-style=none ${files_to_format}
        WORKING_DIRECTORY "${QCAI2_FORMAT_WORKTREE_DIR}"
        RESULT_VARIABLE clang_format_result
        COMMAND_ECHO STDOUT
    )

    if(NOT clang_format_result EQUAL 0)
        message(FATAL_ERROR "clang-format failed with exit code ${clang_format_result}")
    endif()
endif()
