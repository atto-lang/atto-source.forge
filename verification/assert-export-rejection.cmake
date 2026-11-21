cmake_minimum_required(VERSION 3.20)
# A compiler rejection must be a single expected error, not an incidental
# nonzero status, crash, or substring buried in unrelated diagnostic output.
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
if(NOT "${result}" STREQUAL "1")
  message(FATAL_ERROR "EXPORT_REJECTION_INVALID_EXIT: expected exactly 1, received '${result}'\n${stdout}\n${stderr}")
endif()
if(NOT "${stdout}" STREQUAL "")
  message(FATAL_ERROR "EXPORT_REJECTION_STDOUT: unexpected stdout\n${stdout}")
endif()
if(NOT "${stderr}" STREQUAL "oresforge: ${EXPECT}\n")
  message(FATAL_ERROR "EXPORT_REJECTION_WRONG_DIAGNOSTIC: expected exactly '${EXPECT}'\n${stderr}")
endif()
message(STATUS "EXPORT_REJECTION_VERIFIED: ${EXPECT}")
