cmake_minimum_required(VERSION 3.20)
foreach(required COMPILER VALID OVERFLOW UNSUPPORTED)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "Missing ${required}")
  endif()
endforeach()

function(assert_scalar_check source expected_exit error_fragment)
  execute_process(
    COMMAND "${COMPILER}" check "${source}"
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE actual_stdout
    ERROR_VARIABLE actual_stderr
    TIMEOUT 10)
  if(NOT "${actual_exit}" STREQUAL "${expected_exit}")
    message(FATAL_ERROR "check ${source}: exit ${actual_exit}, expected ${expected_exit}; stderr=${actual_stderr}")
  endif()
  if(NOT "${actual_stdout}" STREQUAL "")
    message(FATAL_ERROR "check ${source}: emitted unexpected stdout: ${actual_stdout}")
  endif()
  if("${error_fragment}" STREQUAL "")
    if(NOT "${actual_stderr}" STREQUAL "")
      message(FATAL_ERROR "check ${source}: emitted unexpected stderr: ${actual_stderr}")
    endif()
  elseif(NOT "${actual_stderr}" MATCHES "oresforge: .*${error_fragment}")
    message(FATAL_ERROR "check ${source}: incorrect diagnostic: ${actual_stderr}")
  endif()
endfunction()

assert_scalar_check("${VALID}" 0 "")
assert_scalar_check("${OVERFLOW}" 1 "i64 add overflow")
assert_scalar_check("${UNSUPPORTED}" 1 "FORGE_CHECKED_DIVISION_BACKEND_UNSUPPORTED")
assert_scalar_check("${VALID}.missing" 1 "cannot open source")
message(STATUS "FORGE_SCALAR_CHECK_CLI_PASS positive=1 negative=3")
