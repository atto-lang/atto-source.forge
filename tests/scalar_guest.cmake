# Executed restricted .atto -> native C++ -> binary guest verification.
if(NOT DEFINED ROOT OR NOT DEFINED FORGE OR NOT DEFINED CXX OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "missing scalar guest integration variables")
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")
function(verify_guest fixture want_rc want_out want_err)
  set(source "${ROOT}/fixtures/scalar-v1/${fixture}.atto")
  set(cpp "${OUT_DIR}/${fixture}.cpp")
  set(binary "${OUT_DIR}/${fixture}")
  execute_process(COMMAND "${FORGE}" "${source}" -o "${cpp}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "${fixture}: frontend failed: ${rc}: ${err}")
  endif()
  execute_process(COMMAND "${CXX}" -std=c++20 -Wall -Wextra -Werror
    "-I${ROOT}/include" "${cpp}" -o "${binary}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "${fixture}: generated C++ build failed: ${err}")
  endif()
  execute_process(COMMAND "${binary}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "${want_rc}" OR
     NOT "${out}" STREQUAL "${want_out}" OR
     NOT "${err}" STREQUAL "${want_err}")
    message(FATAL_ERROR "${fixture}: compiled guest returned ${rc}, stdout=[${out}], stderr=[${err}]")
  endif()
endfunction()
verify_guest(arithmetic "0" "34\n" "")
verify_guest(overflow "70" "" "E_OVERFLOW\n")
verify_guest(divzero "71" "" "E_DIVZERO\n")
verify_guest(minimum "0" "-9223372036854775808\n" "")

set(invalid "${OUT_DIR}/invalid.atto")
set(stale "${OUT_DIR}/invalid.cpp")
file(WRITE "${invalid}" "fnc main(): int {return os.read_env(\"HOME\");}")
file(WRITE "${stale}" "stale source")
execute_process(COMMAND "${FORGE}" "${invalid}" -o "${stale}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "2" OR EXISTS "${stale}")
  message(FATAL_ERROR "invalid source published or stale output retained")
endif()
string(REPEAT "1" 17000 huge)
file(WRITE "${OUT_DIR}/huge.atto" "${huge}")
execute_process(COMMAND "${FORGE}" "${OUT_DIR}/huge.atto" -o "${stale}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "2" OR EXISTS "${stale}")
  message(FATAL_ERROR "source-size budget violated")
endif()
