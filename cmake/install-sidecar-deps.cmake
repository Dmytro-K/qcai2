set(sidecar_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/plugins/18.0.2/qcai2/sidecar")

if (NOT EXISTS "${sidecar_dir}/package.json")
    message(FATAL_ERROR "qcai2 sidecar package.json not found in ${sidecar_dir}")
endif()

if (WIN32)
    find_program(NPM_EXECUTABLE NAMES npm.cmd npm)
else()
    find_program(NPM_EXECUTABLE NAMES npm)
endif()

if (NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "npm executable not found; required to install qcai2 sidecar dependencies in ${sidecar_dir}")
endif()

message(STATUS "Installing qcai2 sidecar dependencies in ${sidecar_dir}")
execute_process(
    COMMAND "${NPM_EXECUTABLE}" install --no-audit --no-fund
    WORKING_DIRECTORY "${sidecar_dir}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE npm_result
    OUTPUT_VARIABLE npm_stdout
    ERROR_VARIABLE npm_stderr
)

if (NOT npm_result EQUAL 0)
    message(FATAL_ERROR
        "npm install failed in ${sidecar_dir} with exit code ${npm_result}\n"
        "stdout:\n${npm_stdout}\n"
        "stderr:\n${npm_stderr}")
endif()
