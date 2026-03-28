set(QCAI2_COMPLETION_PREFIX_CONTEXT_CHARS_MAX 1200 CACHE STRING
    "Maximum number of prefix-context characters kept in completion prompts.")
set(QCAI2_COMPLETION_SUFFIX_CONTEXT_CHARS_MAX 400 CACHE STRING
    "Maximum number of suffix-context characters kept in completion prompts.")
set(QCAI2_COMPLETION_FUNCTION_SNIPPET_LINES_MAX 24 CACHE STRING
    "Maximum number of lines allowed in a function snippet for completion context.")
set(QCAI2_COMPLETION_FUNCTION_SNIPPET_CHARS_MAX 1200 CACHE STRING
    "Maximum number of rendered characters allowed in a function snippet for completion context.")
set(QCAI2_COMPLETION_CLASS_SNIPPET_LINES_MAX 500 CACHE STRING
    "Maximum number of lines allowed in a class snippet for completion context.")
set(QCAI2_COMPLETION_CLASS_SNIPPET_CHARS_MAX 20000 CACHE STRING
    "Maximum number of rendered characters allowed in a class snippet for completion context.")
set(QCAI2_GHOST_TEXT_DIAGNOSTICS_EXCERPT_CHARS_MAX 80 CACHE STRING
    "Maximum number of characters shown in one ghost-text diagnostics excerpt.")

function(qcai2_validate_global_parameter name)
    if(NOT DEFINED ${name} OR NOT "${${name}}" MATCHES "^[0-9]+$")
        message(FATAL_ERROR "Global parameter ${name} must be a non-negative integer.")
    endif()
endfunction()

foreach(qcai2_global_parameter IN ITEMS
        QCAI2_COMPLETION_PREFIX_CONTEXT_CHARS_MAX
        QCAI2_COMPLETION_SUFFIX_CONTEXT_CHARS_MAX
        QCAI2_COMPLETION_FUNCTION_SNIPPET_LINES_MAX
        QCAI2_COMPLETION_FUNCTION_SNIPPET_CHARS_MAX
        QCAI2_COMPLETION_CLASS_SNIPPET_LINES_MAX
        QCAI2_COMPLETION_CLASS_SNIPPET_CHARS_MAX
        QCAI2_GHOST_TEXT_DIAGNOSTICS_EXCERPT_CHARS_MAX)
    qcai2_validate_global_parameter(${qcai2_global_parameter})
endforeach()

function(qcai2_apply_global_parameters target_name)
    target_compile_definitions(${target_name} PRIVATE
        QCAI2_COMPLETION_PREFIX_CONTEXT_CHARS_MAX=${QCAI2_COMPLETION_PREFIX_CONTEXT_CHARS_MAX}
        QCAI2_COMPLETION_SUFFIX_CONTEXT_CHARS_MAX=${QCAI2_COMPLETION_SUFFIX_CONTEXT_CHARS_MAX}
        QCAI2_COMPLETION_FUNCTION_SNIPPET_LINES_MAX=${QCAI2_COMPLETION_FUNCTION_SNIPPET_LINES_MAX}
        QCAI2_COMPLETION_FUNCTION_SNIPPET_CHARS_MAX=${QCAI2_COMPLETION_FUNCTION_SNIPPET_CHARS_MAX}
        QCAI2_COMPLETION_CLASS_SNIPPET_LINES_MAX=${QCAI2_COMPLETION_CLASS_SNIPPET_LINES_MAX}
        QCAI2_COMPLETION_CLASS_SNIPPET_CHARS_MAX=${QCAI2_COMPLETION_CLASS_SNIPPET_CHARS_MAX}
        QCAI2_GHOST_TEXT_DIAGNOSTICS_EXCERPT_CHARS_MAX=${QCAI2_GHOST_TEXT_DIAGNOSTICS_EXCERPT_CHARS_MAX}
    )
endfunction()
