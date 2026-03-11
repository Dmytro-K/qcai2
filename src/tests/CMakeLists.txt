add_executable(tst_qcai2_json
    tst_json.cpp
    ../src/util/Json.cpp
)

qcai2_enable_linker_map(tst_qcai2_json)

target_include_directories(tst_qcai2_json PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_json PRIVATE
    Qt::Core
    Qt::Test
)
add_test(NAME qcai2_json COMMAND tst_qcai2_json)

add_executable(tst_qcai2_toolcall
    tst_toolcall.cpp
    ../src/models/ToolCall.cpp
)

qcai2_enable_linker_map(tst_qcai2_toolcall)

target_include_directories(tst_qcai2_toolcall PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(tst_qcai2_toolcall PRIVATE
    Qt::Core
    Qt::Test
)
add_test(NAME qcai2_toolcall COMMAND tst_qcai2_toolcall)

add_executable(tst_qcai2_migration
    tst_migration.cpp
    ../src/util/Migration.cpp
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
add_test(NAME qcai2_migration COMMAND tst_qcai2_migration)
