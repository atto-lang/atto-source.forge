cmake_minimum_required(VERSION 3.20)
# Require authentic successful execution and a precise scalar guest result.
# A failed compiler that mentions 42 or noisy stdout is not positive evidence.
foreach(required IN ITEMS COMPILER SOURCE EXPECT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "EXPORT_ACCEPTANCE_MISSING_ARGUMENT: ${required}")
  endif()
endforeach()
execute_process(
  COMMAND "${COMPILER}" run "${SOURCE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  TIMEOUT 10)
if(NOT "${result}" STREQUAL "0")
  message(FATAL_ERROR "EXPORT_ACCEPTANCE_INVALID_EXIT: expected 0, received '${result}'\n${stdout}\n${stderr}")
endif()
if(NOT "${stderr}" STREQUAL "")
  message(FATAL_ERROR "EXPORT_ACCEPTANCE_STDERR: unexpected stderr\n${stderr}")
endif()
if(NOT "${stdout}" STREQUAL "${EXPECT}\n")
  message(FATAL_ERROR "EXPORT_ACCEPTANCE_WRONG_RESULT: expected exactly '${EXPECT}', received\n${stdout}")
endif()
message(STATUS "EXPORT_ACCEPTANCE_VERIFIED: ${EXPECT}")
