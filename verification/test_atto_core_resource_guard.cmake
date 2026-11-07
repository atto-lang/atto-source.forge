# Host-only CMake security gate. Does NOT constitute compiled Atto guest proof.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/AttoCoreResourceGuard.cmake")
file(READ "${CMAKE_CURRENT_LIST_DIR}/../contracts/atto-core-v1/identity.atto"
     ATTO_CORE_IDENTITY_SOURCE)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../contracts/atto-core-v1/numeric.atto"
     ATTO_CORE_NUMERIC_SOURCE)
atto_guard_core_raw_literal(ATTO_CORE_IDENTITY_SOURCE "__ATTO_CORE__")
atto_guard_core_raw_literal(ATTO_CORE_NUMERIC_SOURCE "__ATTO_NUM__")
foreach(delimiter IN ITEMS "__ATTO_CORE__" "__ATTO_NUM__")
  set(reject_file "${CMAKE_CURRENT_BINARY_DIR}/atto-guard-reject-${delimiter}.cmake")
  # Inject a raw string close token in a hypothetical compiler-owned
  # .atto comment; the generated header must never admit it.
  file(WRITE "${reject_file}"
    "include(\"${CMAKE_CURRENT_LIST_DIR}/../cmake/AttoCoreResourceGuard.cmake\")\n"
    "set(BAD_SOURCE [=[ // )${delimiter}\" injected ]=])\n"
    "atto_guard_core_raw_literal(BAD_SOURCE \"${delimiter}\")\n")
  execute_process(COMMAND "${CMAKE_COMMAND}" -P "${reject_file}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(result EQUAL 0)
    message(FATAL_ERROR "Injected C++ raw-string delimiter was incorrectly allowed")
  endif()
  if(NOT error MATCHES "would terminate generated C\\+\\+ raw string")
    message(FATAL_ERROR "Missing fail-closed delimiter diagnostic: ${error}")
  endif()
  file(REMOVE "${reject_file}")
endforeach()
message(STATUS "Atto Core C++ raw-string source guard: valid resources + two malicious closers rejected")
