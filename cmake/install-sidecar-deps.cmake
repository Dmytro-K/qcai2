set(sidecar_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/plugins/19.0.0/qcai2/sidecar")

if(NOT EXISTS "${sidecar_dir}/package.json")
    message(FATAL_ERROR "qcai2 sidecar package.json not found in ${sidecar_dir}")
endif()

if(WIN32)
    find_program(NPM_EXECUTABLE NAMES npm.cmd npm)
else()
    find_program(NPM_EXECUTABLE NAMES npm)
endif()

if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "npm executable not found; required to install qcai2 sidecar dependencies in ${sidecar_dir}")
endif()

set(npm_command install --no-audit --no-fund)
set(npm_command_name "npm install")
if(EXISTS "${sidecar_dir}/package-lock.json")
    set(npm_command ci --no-audit --no-fund)
    set(npm_command_name "npm ci")
endif()

message(STATUS "Installing qcai2 sidecar dependencies in ${sidecar_dir} using ${npm_command_name}")
execute_process(
    COMMAND "${NPM_EXECUTABLE}" ${npm_command}
    WORKING_DIRECTORY "${sidecar_dir}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE npm_result
    OUTPUT_VARIABLE npm_stdout
    ERROR_VARIABLE npm_stderr
)

if(NOT npm_result EQUAL 0)
    message(FATAL_ERROR
        "${npm_command_name} failed in ${sidecar_dir} with exit code ${npm_result}\n"
        "stdout:\n${npm_stdout}\n"
        "stderr:\n${npm_stderr}")
endif()
