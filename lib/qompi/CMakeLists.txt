cmake_minimum_required(VERSION 3.28)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(qompi LANGUAGES CXX)

    set(CMAKE_AUTOMOC ON)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(qompi_core STATIC
    include/qompi/callback_completion_backend.h
    include/qompi/completion_backend.h
    include/qompi/completion_engine.h
    include/qompi/completion_operation.h
    include/qompi/completion_policy.h
    include/qompi/completion_profiles.h
    include/qompi/completion_session.h
    include/qompi/completion_sink.h
    include/qompi/completion_types.h
    src/core/callback_completion_backend.cpp
    src/core/completion_engine.cpp
    src/core/completion_operation.cpp
    src/core/completion_policy.cpp
    src/core/completion_profiles.cpp
    src/core/completion_session.cpp
)

add_library(qompi::core ALIAS qompi_core)

target_compile_features(qompi_core PUBLIC cxx_std_23)
set_target_properties(qompi_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(qompi_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

add_library(qompi_qt STATIC
    include/qompi/qt_completion_session.h
    src/detail/callback_dispatcher.h
    src/qt/qt_callback_dispatcher.cpp
    src/qt/qt_completion_session.cpp
)

add_library(qompi::qt ALIAS qompi_qt)

target_compile_features(qompi_qt PUBLIC cxx_std_23)
set_target_properties(qompi_qt PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(qompi_qt
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(qompi_qt
    PUBLIC
        qompi_core
        Qt6::Core
)

add_library(qompi INTERFACE)
add_library(qompi::qompi ALIAS qompi)

target_include_directories(qompi
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(qompi INTERFACE qompi_qt)

set(SRC_IDE
    LICENSE
    README.md
    CHANGELOG.md
    VERSION.txt
)

add_custom_target(qompi_target_for_ide SOURCES ${SRC_IDE})
