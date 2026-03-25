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

function(run_npm_command result_var stdout_var stderr_var)
    execute_process(
        COMMAND "${NPM_EXECUTABLE}" ${ARGN}
        WORKING_DIRECTORY "${sidecar_dir}"
        COMMAND_ECHO STDOUT
        RESULT_VARIABLE npm_result
        OUTPUT_VARIABLE npm_stdout
        ERROR_VARIABLE npm_stderr
    )
    set(${result_var} "${npm_result}" PARENT_SCOPE)
    set(${stdout_var} "${npm_stdout}" PARENT_SCOPE)
    set(${stderr_var} "${npm_stderr}" PARENT_SCOPE)
endfunction()

set(npm_install_command install --no-audit --no-fund)
set(npm_install_command_name "npm install")

if(EXISTS "${sidecar_dir}/package-lock.json")
    message(STATUS
        "Removing installed qcai2 sidecar package-lock.json before dependency installation in ${sidecar_dir}")
    file(REMOVE "${sidecar_dir}/package-lock.json")
endif()

message(STATUS
    "Installing qcai2 sidecar dependencies in ${sidecar_dir} using ${npm_install_command_name}")
run_npm_command(npm_result npm_stdout npm_stderr ${npm_install_command})

if(NOT npm_result EQUAL 0)
    message(FATAL_ERROR
        "Sidecar dependency installation failed in ${sidecar_dir} with exit code ${npm_result}\n"
        "stdout:\n${npm_stdout}\n"
        "stderr:\n${npm_stderr}")
endif()
