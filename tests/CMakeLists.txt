add_executable(tst_qcai2_json
    tst_json.cpp
    ../src/util/Json.cpp
)

qcai2_enable_linker_map(tst_qcai2_json)

target_include_directories(tst_qcai2_json PRIVATE
    "${CMAKE_SOURCE_DIR}"
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
    "${CMAKE_SOURCE_DIR}"
)
target_link_libraries(tst_qcai2_toolcall PRIVATE
    Qt::Core
    Qt::Test
)
add_test(NAME qcai2_toolcall COMMAND tst_qcai2_toolcall)
