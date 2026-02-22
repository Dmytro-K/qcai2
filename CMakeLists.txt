cmake_minimum_required(VERSION 3.28)

# Remove when sharing with others.
list(APPEND CMAKE_PREFIX_PATH "/usr")

project(Qcai2)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (WIN32)
    set(ENV{HOME} "$ENV{USERPROFILE}")
endif (WIN32)

if (WIN32)
    set(ADDITIONAL_FLAGS WIN32)
else (WIN32)
    set(ADDITIONAL_FLAGS)
endif (WIN32)

set(COMPILER_LIST ${CMAKE_C_COMPILER_ID} ${CMAKE_CXX_COMPILER_ID})
list(GET COMPILER_LIST 0 COMPILER_ID)

if("${COMPILER_ID}" STREQUAL "Clang")
    # using Clang
elseif("${COMPILER_ID}" STREQUAL "GNU")
    # using GCC
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wconversion")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -g0 -O3 -march=native -mtune=native -ftree-vectorize -funroll-loops -flto -flto-partition=none -ffat-lto-objects")
    # set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -ftree-parallelize-loops=32")
    # set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fopenmp")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -mavx")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -mprefer-vector-width=512")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -ffast-math")
    # set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fprofile-generate")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fprofile-use")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -pg")
    # set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -ggdb3")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb3")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Map=${PROJECT_NAME}.map")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,-O2,--sort-common,--as-needed,-z,relro,-z,now,--copy-dt-needed-entries,-flto")
    #target_link_libraries(${PROJECT_NAME} stdc++fs)
    add_definitions(-D__GNU_GCC__)
elseif("${COMPILER_ID}" STREQUAL "Intel")
    # using Intel C++
elseif("${COMPILER_ID}" STREQUAL "MSVC")
    # using Visual Studio C++
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MAP:${PROJECT_NAME}.map")
endif("${COMPILER_ID}" STREQUAL "Clang")

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "${CMAKE_C_FLAGS_DEBUG_INIT}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "${CMAKE_C_FLAGS_RELEASE_INIT}")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL}")
set(CMAKE_CXX_FLAGS_MINSIZEREL_INIT "${CMAKE_C_FLAGS_MINSIZEREL_INIT}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "${CMAKE_C_FLAGS_RELWITHDEBINFO_INIT}")

find_package(QtCreator REQUIRED COMPONENTS Core TextEditor ProjectExplorer)
find_package(Qt6 COMPONENTS Widgets Network REQUIRED)

# Add a CMake option that enables building your plugin with tests.
# You don't want your released plugin binaries to contain tests,
# so make that default to 'NO'.
# Enable tests by passing -DWITH_TESTS=ON to CMake.
option(WITH_TESTS "Builds with tests" NO)

if(WITH_TESTS)
  # Look for QtTest
  find_package(Qt6 REQUIRED COMPONENTS Test)

  # Tell CMake functions like add_qtc_plugin about the QtTest component.
  set(IMPLICIT_DEPENDS Qt::Test)

  # Enable ctest for auto tests.
  enable_testing()
endif()

add_qtc_plugin(Qcai2
  PLUGIN_DEPENDS
    QtCreator::Core
    QtCreator::TextEditor
    QtCreator::ProjectExplorer
  DEPENDS
    Qt::Widgets
    Qt::Network
    QtCreator::ExtensionSystem
    QtCreator::Utils
  SOURCES
    qcai2constants.h
    qcai2tr.h
    resources/aiagentplugin.qrc
    src/AiAgentPlugin.h
    src/AiAgentPlugin.cpp
    src/AgentController.h
    src/AgentController.cpp
    src/AgentDockWidget.h
    src/AgentDockWidget.cpp
    src/context/EditorContext.h
    src/context/EditorContext.cpp
    src/tools/ITool.h
    src/tools/ToolRegistry.h
    src/tools/ToolRegistry.cpp
    src/tools/FileTools.h
    src/tools/FileTools.cpp
    src/tools/SearchTools.h
    src/tools/SearchTools.cpp
    src/tools/BuildTools.h
    src/tools/BuildTools.cpp
    src/tools/GitTools.h
    src/tools/GitTools.cpp
    src/tools/IdeTools.h
    src/tools/IdeTools.cpp
    src/providers/IAIProvider.h
    src/providers/OpenAICompatibleProvider.h
    src/providers/OpenAICompatibleProvider.cpp
    src/providers/LocalHttpProvider.h
    src/providers/LocalHttpProvider.cpp
    src/providers/OllamaProvider.h
    src/providers/OllamaProvider.cpp
    src/providers/CopilotProvider.h
    src/providers/CopilotProvider.cpp
    src/models/AgentMessages.h
    src/models/AgentMessages.cpp
    src/models/ToolCall.h
    src/models/ToolCall.cpp
    src/safety/SafetyPolicy.h
    src/safety/SafetyPolicy.cpp
    src/util/Json.h
    src/util/Json.cpp
    src/util/ProcessRunner.h
    src/util/ProcessRunner.cpp
    src/util/Diff.h
    src/util/Diff.cpp
    src/util/Logger.h
    src/util/Logger.cpp
    src/util/CrashHandler.h
    src/util/CrashHandler.cpp
    src/settings/Settings.h
    src/settings/Settings.cpp
    src/settings/SettingsPage.h
    src/settings/SettingsPage.cpp
    src/completion/AiCompletionProvider.h
    src/completion/AiCompletionProvider.cpp
    src/completion/AiCompletionProcessor.h
    src/completion/AiCompletionProcessor.cpp
)

# Sidecar path for development builds
target_compile_definitions(Qcai2 PRIVATE
    QCAI2_SIDECAR_PATH="${CMAKE_CURRENT_SOURCE_DIR}/sidecar/copilot-sidecar.js"
)

# Install sidecar files alongside the plugin
install(FILES
    sidecar/copilot-sidecar.js
    sidecar/package.json
    DESTINATION share/qtcreator/qcai2/sidecar
)
# Install node_modules if they exist (after npm install)
install(DIRECTORY sidecar/node_modules/
    DESTINATION share/qtcreator/qcai2/sidecar/node_modules
    OPTIONAL
)

# Enable the Run button in Qt Creator
get_target_property(QtCreatorCorePath QtCreator::Core LOCATION)
find_program(QtCreatorExecutable
  NAMES
    qtcreator "Qt Creator"
  PATHS
    "${QtCreatorCorePath}/../../../bin"
    "${QtCreatorCorePath}/../../../MacOS"
  NO_DEFAULT_PATH
)
if (QtCreatorExecutable)
  add_custom_target(RunQtCreator
    COMMAND ${QtCreatorExecutable} -pluginpath $<TARGET_FILE_DIR:Qcai2>
    DEPENDS Qcai2
  )
  set_target_properties(RunQtCreator PROPERTIES FOLDER "qtc_runnable")
endif()


set(SRC_IDE .clang-format ai-prompt.txt ai-todo-errors.txt ai-context.md README.md)

add_custom_target(target_for_ide SOURCES ${SRC_IDE})
