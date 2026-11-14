cmake_minimum_required(VERSION 3.20)
# Rejections are verified by expected diagnostics, not just by nonzero exit.
# Missing executables, timeouts, crashes, and unexpected success are failures.
foreach(required IN ITEMS COMPILER SOURCE EXPECT)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "EXPORT_REJECTION_MISSING_ARGUMENT: ${required}")
  endif()
endforeach()
execute_process(
  COMMAND "${COMPILER}" run "${SOURCE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  TIMEOUT 10)
if(NOT "${result}" MATCHES "^[0-9]+$" OR result EQUAL 0 OR result GREATER 125)
  message(FATAL_ERROR "EXPORT_REJECTION_INVALID_EXIT: expected compiler rejection (1..125), received '${result}'\n${stdout}\n${stderr}")
endif()
set(output "${stdout}\n${stderr}")
string(FIND "${output}" "${EXPECT}" expected_position)
if(expected_position EQUAL -1)
  message(FATAL_ERROR "EXPORT_REJECTION_WRONG_DIAGNOSTIC: expected ${EXPECT}, exit ${result}\n${output}")
endif()
message(STATUS "EXPORT_REJECTION_VERIFIED: ${EXPECT}")
