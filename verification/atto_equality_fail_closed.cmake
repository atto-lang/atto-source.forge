cmake_minimum_required(VERSION 3.20)
if(NOT EXISTS "${FORGE}")
  message(FATAL_ERROR "missing real Forge compiler")
endif()
foreach(case_name IN ITEMS eq neq bang_eq)
  set(input "${ROOT}/verification/fixtures/erl-otp-operator-aot/${case_name}.atto")
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "missing reviewed Atto source: ${case_name}")
  endif()
  execute_process(
    COMMAND "${FORGE}" emit-asm x86_64 "${input}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE diagnostic
    TIMEOUT 10)
  if(exit_code EQUAL 0 OR NOT output STREQUAL "")
    message(FATAL_ERROR "${case_name}: unsupported typed equality emitted native output")
  endif()
  string(FIND "${diagnostic}" "ATTO_EQ_BOOL_AOT_UNSUPPORTED" found)
  string(FIND "${diagnostic}" "at byte " offset)
  if(found EQUAL -1 OR offset EQUAL -1)
    message(FATAL_ERROR "${case_name}: wrong rejection diagnostic: ${diagnostic}")
  endif()
  message(STATUS "PASS ${case_name}: typed Boolean equality rejected before AOT emission")
endforeach()
