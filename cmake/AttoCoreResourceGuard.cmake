# Keep source .atto bytes from accidentally breaking the C++ raw-literal
# wrapper generated at CMake configure time. No source text is rewritten.
function(atto_guard_core_raw_literal source_variable delimiter)
  if(NOT DEFINED ${source_variable})
    message(FATAL_ERROR "Missing canonical Atto Core source variable: ${source_variable}")
  endif()
  string(LENGTH "${delimiter}" length)
  if(length GREATER 16 OR length LESS 1 OR
     NOT delimiter MATCHES "^[A-Za-z0-9_]+$")
    message(FATAL_ERROR "Invalid Atto Core raw-string delimiter")
  endif()
  set(close_token ")${delimiter}\"")
  string(FIND "${${source_variable}}" "${close_token}" close_index)
  if(NOT close_index EQUAL -1)
    message(FATAL_ERROR "Canonical Atto Core source would terminate generated C++ raw string: ${source_variable}")
  endif()
endfunction()
