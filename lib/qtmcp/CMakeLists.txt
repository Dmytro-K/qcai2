cmake_minimum_required(VERSION 3.28)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(qtmcp LANGUAGES CXX)

    set(CMAKE_AUTOMOC ON)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network Gui NetworkAuth)

add_library(qtmcp
    include/qtmcp/Client.h
    include/qtmcp/HttpTransport.h
    include/qtmcp/ServerDefinition.h
    include/qtmcp/StdioTransport.h
    include/qtmcp/Transport.h
    src/Client.cpp
    src/HttpTransport.cpp
    src/ServerDefinition.cpp
    src/StdioTransport.cpp
    src/Transport.cpp
)

add_library(qtmcp::qtmcp ALIAS qtmcp)

target_compile_features(qtmcp PUBLIC cxx_std_23)
set_target_properties(qtmcp PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(qtmcp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(qtmcp
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::NetworkAuth
    PRIVATE
        Qt6::Gui
)
target_compile_definitions(qtmcp PUBLIC QTMCP_HAS_NETWORKAUTH=1)


set(SRC_IDE
    LICENSE
    README.md
    CHANGELOG.md
    VERSION.txt
)

add_custom_target(qtmcp_target_for_ide SOURCES ${SRC_IDE})
