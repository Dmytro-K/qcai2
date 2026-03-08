cmake_minimum_required(VERSION 3.28)

# Remove when sharing with others.
list(APPEND CMAKE_PREFIX_PATH "/usr")

project(qcai2)

include(GNUInstallDirs)

set(QCAI2_FORMAT_CHANGED_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/format-changed-files.cmake")
set(QCAI2_BUILD_DOCS_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build-docs.cmake")

if (WIN32)
    if (DEFINED ENV{LOCALAPPDATA} AND NOT "$ENV{LOCALAPPDATA}" STREQUAL "")
        set(QCAI2_DEFAULT_INSTALL_PREFIX "$ENV{LOCALAPPDATA}/QtProject/qtcreator")
    else ()
        set(QCAI2_DEFAULT_INSTALL_PREFIX "$ENV{HOME}/AppData/Local/QtProject/qtcreator")
    endif ()
elseif (APPLE)
    set(QCAI2_DEFAULT_INSTALL_PREFIX "$ENV{HOME}/Library/Application Support/QtProject/Qt Creator")
else ()
    if (DEFINED ENV{XDG_DATA_HOME} AND NOT "$ENV{XDG_DATA_HOME}" STREQUAL "")
        set(QCAI2_DEFAULT_INSTALL_PREFIX "$ENV{XDG_DATA_HOME}/data/QtProject/qtcreator")
    else ()
        set(QCAI2_DEFAULT_INSTALL_PREFIX "$ENV{HOME}/.local/share/data/QtProject/qtcreator")
    endif ()
endif ()

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${QCAI2_DEFAULT_INSTALL_PREFIX}" CACHE PATH
        "Install path prefix, prepended onto install directories." FORCE)
endif ()

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
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wconversion -pedantic -Werror")
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
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -Wl,-O2,--sort-common,--as-needed,-z,relro,-z,now,--copy-dt-needed-entries,-flto")
    #target_link_libraries(${PROJECT_NAME} stdc++fs)
    add_definitions(-D__GNU_GCC__)
elseif("${COMPILER_ID}" STREQUAL "Intel")
    # using Intel C++
elseif("${COMPILER_ID}" STREQUAL "MSVC")
    # using Visual Studio C++
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
endif("${COMPILER_ID}" STREQUAL "Clang")

function(qcai2_enable_linker_map target)
    if (MSVC)
        target_link_options(${target} PRIVATE
            "/MAP:${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(${target} PRIVATE
            "LINKER:-Map,${CMAKE_CURRENT_BINARY_DIR}/${target}.map")
    endif ()
endfunction()

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

  add_subdirectory(src/tests)
endif()

add_qtc_plugin(qcai2
  PLUGIN_PATH
    plugins/${IDE_VERSION}
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
    qcai2.json.in
    src/qcai2constants.h
    src/qcai2tr.h
    src/resources/aiagentplugin.qrc
    src/src/AiAgentPlugin.h
    src/src/AiAgentPlugin.cpp
    src/src/AgentController.h
    src/src/AgentController.cpp
    src/src/AgentDockWidget.h
    src/src/AgentDockWidget.cpp
    src/src/context/EditorContext.h
    src/src/context/EditorContext.cpp
    src/src/tools/ITool.h
    src/src/tools/ToolRegistry.h
    src/src/tools/ToolRegistry.cpp
    src/src/tools/FileTools.h
    src/src/tools/FileTools.cpp
    src/src/tools/SearchTools.h
    src/src/tools/SearchTools.cpp
    src/src/tools/BuildTools.h
    src/src/tools/BuildTools.cpp
    src/src/tools/GitTools.h
    src/src/tools/GitTools.cpp
    src/src/tools/IdeTools.h
    src/src/tools/IdeTools.cpp
    src/src/providers/IAIProvider.h
    src/src/providers/OpenAICompatibleProvider.h
    src/src/providers/OpenAICompatibleProvider.cpp
    src/src/providers/LocalHttpProvider.h
    src/src/providers/LocalHttpProvider.cpp
    src/src/providers/OllamaProvider.h
    src/src/providers/OllamaProvider.cpp
    src/src/providers/CopilotProvider.h
    src/src/providers/CopilotProvider.cpp
    src/src/models/AgentMessages.h
    src/src/models/AgentMessages.cpp
    src/src/models/ToolCall.h
    src/src/models/ToolCall.cpp
    src/src/safety/SafetyPolicy.h
    src/src/safety/SafetyPolicy.cpp
    src/src/util/Json.h
    src/src/util/Json.cpp
    src/src/util/ProcessRunner.h
    src/src/util/ProcessRunner.cpp
    src/src/util/Diff.h
    src/src/util/Diff.cpp
    src/src/util/Logger.h
    src/src/util/Logger.cpp
    src/src/util/CrashHandler.h
    src/src/util/CrashHandler.cpp
    src/src/settings/Settings.h
    src/src/settings/Settings.cpp
    src/src/settings/SettingsPage.h
    src/src/settings/SettingsPage.cpp
    src/src/completion/AiCompletionProvider.h
    src/src/completion/AiCompletionProvider.cpp
    src/src/completion/AiCompletionProcessor.h
    src/src/completion/AiCompletionProcessor.cpp
    src/src/completion/GhostTextManager.h
    src/src/completion/GhostTextManager.cpp
    src/src/diff/InlineDiffManager.h
    src/src/diff/InlineDiffManager.cpp
)

qcai2_enable_linker_map(qcai2)

set(QCAI2_PLUGIN_INSTALL_DIR "plugins/${IDE_VERSION}")
set(QCAI2_SIDECAR_INSTALL_DIR "${QCAI2_PLUGIN_INSTALL_DIR}/qcai2/sidecar")

# Sidecar path for development builds
target_compile_definitions(qcai2 PRIVATE
    QCAI2_SIDECAR_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/sidecar/copilot-sidecar.js"
    QCAI2_QTCREATOR_IDE_VERSION="${IDE_VERSION}"
    QCAI2_INSTALLED_SIDECAR_RELATIVE_PATH="qcai2/sidecar/copilot-sidecar.js"
)

# Install sidecar files alongside the plugin
install(FILES
    src/sidecar/copilot-sidecar.js
    src/sidecar/package.json
    src/sidecar/package-lock.json
    DESTINATION ${QCAI2_SIDECAR_INSTALL_DIR}
)

set(QCAI2_INSTALL_SIDECAR_DEPS_SCRIPT
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/install-sidecar-deps.cmake")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/install-sidecar-deps.cmake"
    "${QCAI2_INSTALL_SIDECAR_DEPS_SCRIPT}"
    @ONLY
)
install(SCRIPT "${QCAI2_INSTALL_SIDECAR_DEPS_SCRIPT}")

find_program(QCAI2_CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-18 clang-format-17)
find_program(QCAI2_GIT_EXECUTABLE NAMES git)

file(RELATIVE_PATH QCAI2_BINARY_DIR_RELATIVE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
set(QCAI2_FORMAT_EXCLUDED_PREFIXES
    "${QCAI2_BINARY_DIR_RELATIVE};build/;cmake-build-debug/;cmake-build-release/;cmake-build-relwithdebinfo/;cmake-build-minsizerel/")

add_custom_target(format-changed-files
    COMMAND ${CMAKE_COMMAND}
        -DCLANG_FORMAT_EXECUTABLE=${QCAI2_CLANG_FORMAT_EXECUTABLE}
        -DGIT_EXECUTABLE=${QCAI2_GIT_EXECUTABLE}
        -DQCAI2_FORMAT_WORKTREE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        "-DQCAI2_FORMAT_EXCLUDED_PREFIXES=${QCAI2_FORMAT_EXCLUDED_PREFIXES}"
        -P "${QCAI2_FORMAT_CHANGED_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Formatting changed project files with clang-format"
)

find_package(Doxygen QUIET)
find_package(Python3 COMPONENTS Interpreter QUIET)
find_program(QCAI2_SPHINX_EXECUTABLE NAMES sphinx-build)

set(QCAI2_DOCS_TEMPLATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/docs")
set(QCAI2_DOCS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs")
set(QCAI2_DOCS_SPHINX_SOURCE_DIR "${QCAI2_DOCS_BUILD_DIR}/sphinx")
set(QCAI2_DOCS_SPHINX_HTML_DIR "${QCAI2_DOCS_BUILD_DIR}/html")
set(QCAI2_DOCS_DOXYGEN_OUTPUT_DIR "${QCAI2_DOCS_BUILD_DIR}/doxygen")
set(QCAI2_DOCS_DOXYGEN_XML_DIR "${QCAI2_DOCS_DOXYGEN_OUTPUT_DIR}/xml")

file(MAKE_DIRECTORY "${QCAI2_DOCS_SPHINX_SOURCE_DIR}")

set(QCAI2_DOXYGEN_INPUT_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/qcai2constants.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/qcai2tr.h"
)
set(QCAI2_DOXYGEN_INPUTS "")
foreach(qcai2_doxygen_input IN LISTS QCAI2_DOXYGEN_INPUT_PATHS)
    string(APPEND QCAI2_DOXYGEN_INPUTS " \"${qcai2_doxygen_input}\"")
endforeach()
string(STRIP "${QCAI2_DOXYGEN_INPUTS}" QCAI2_DOXYGEN_INPUTS)

cmake_path(CONVERT "${QCAI2_DOCS_DOXYGEN_XML_DIR}" TO_CMAKE_PATH_LIST
    QCAI2_DOCS_DOXYGEN_XML_DIR_PY)
cmake_path(CONVERT "${CMAKE_CURRENT_SOURCE_DIR}" TO_CMAKE_PATH_LIST
    QCAI2_PROJECT_SOURCE_DIR_PY)

configure_file(
    "${QCAI2_DOCS_TEMPLATE_DIR}/Doxyfile.in"
    "${QCAI2_DOCS_BUILD_DIR}/Doxyfile"
    @ONLY
)
configure_file(
    "${QCAI2_DOCS_TEMPLATE_DIR}/conf.py.in"
    "${QCAI2_DOCS_SPHINX_SOURCE_DIR}/conf.py"
    @ONLY
)
configure_file(
    "${QCAI2_DOCS_TEMPLATE_DIR}/index.rst"
    "${QCAI2_DOCS_SPHINX_SOURCE_DIR}/index.rst"
    COPYONLY
)
configure_file(
    "${QCAI2_DOCS_TEMPLATE_DIR}/overview.rst"
    "${QCAI2_DOCS_SPHINX_SOURCE_DIR}/overview.rst"
    COPYONLY
)

add_custom_target(doc
    COMMAND ${CMAKE_COMMAND}
        -DDOXYGEN_EXECUTABLE=${DOXYGEN_EXECUTABLE}
        -DSPHINX_EXECUTABLE=${QCAI2_SPHINX_EXECUTABLE}
        -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
        -DDOXYFILE_PATH=${QCAI2_DOCS_BUILD_DIR}/Doxyfile
        -DSPHINX_SOURCE_DIR=${QCAI2_DOCS_SPHINX_SOURCE_DIR}
        -DSPHINX_OUTPUT_DIR=${QCAI2_DOCS_SPHINX_HTML_DIR}
        -P "${QCAI2_BUILD_DOCS_SCRIPT}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Generating documentation with Doxygen, Sphinx, Breathe, and Exhale"
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
    COMMAND ${QtCreatorExecutable} -pluginpath $<TARGET_FILE_DIR:qcai2>
    DEPENDS qcai2
  )
  set_target_properties(RunQtCreator PROPERTIES FOLDER "qtc_runnable")
endif()

set(SRC_IDE
    .clang-format
    .github/workflows/README.md
    .github/workflows/build_cmake.yml
    ai/ai-context.md
    CMakeLists.txt
    LICENSE
    README.md
    .gitignore
    cmake/cmake_install.cmake
    cmake/format-changed-files.cmake
    cmake/build-docs.cmake
    src/docs/Doxyfile.in
    src/docs/conf.py.in
    src/docs/index.rst
    src/docs/overview.rst
    cmake/install-sidecar-deps.cmake
    qcai2.json.in
    src/qcai2_uk_UA.ts
    src/sidecar/copilot-sidecar.js
    src/sidecar/package.json
    src/tests/CMakeLists.txt
)

add_custom_target(target_for_ide SOURCES ${SRC_IDE})
