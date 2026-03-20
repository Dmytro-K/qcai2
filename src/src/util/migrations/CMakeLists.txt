function(qcai2_collect_migration_files out_var)
    set(qcai2_migration_files
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/migration_0_0_4_001.h
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/migration_0_0_4_001.cpp
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/migration_0_0_5_002.h
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/migration_0_0_5_002.cpp
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/migration_0_0_5_003.h
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/migration_0_0_5_003.cpp
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/migration_0_0_7_001.h
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/migration_0_0_7_001.cpp
    )

    set(${out_var} "${qcai2_migration_files}" PARENT_SCOPE)
endfunction()
