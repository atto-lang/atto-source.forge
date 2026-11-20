# Run the exact 15-case Java/JS numeric corpus as restricted compiled Forge
# native scalar guests. This is an expression-level adapter, not the original
# multi-function source program supported by Java/JS.
if(NOT DEFINED ROOT OR NOT DEFINED CXX OR NOT DEFINED FORGE OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "missing Forge checked-i64 guest test inputs")
endif()
set(corpus "${ROOT}/contracts/checked-i64-all-backends-v1/cases.tsv")
file(SHA256 "${corpus}" actual_sha)
if(NOT actual_sha STREQUAL "e56dfccea11757ff18fc72a81ba5d643380e69184ab309eedf45e1e7b940e133")
  message(FATAL_ERROR "shared checked-i64 corpus sha256 drift")
endif()
file(STRINGS "${corpus}" rows ENCODING UTF-8)
list(LENGTH rows count)
if(NOT count EQUAL 15)
  message(FATAL_ERROR "shared checked-i64 corpus must have fifteen cases")
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")
set(flags)
if(SANITIZERS)
  list(APPEND flags -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# Explicit mapping keeps semantic differences honest. The pinned corpus SHA
# fixes every ID, kind, source expression and expected outcome.
set(ids
  safe-zero safe-upper safe-lower safe-multiply safe-negate safe-min-rem
  safe-negative-rem overflow-add overflow-sub overflow-mul overflow-negate
  overflow-mul-min overflow-cancel overflow-argument zero-divisor)
set(kinds
  value value value value value value value
  overflow overflow overflow overflow overflow overflow overflow zero)
set(expectations
  "0" "9223372036854775806" "-9223372036854775807"
  "-9223372030926249001" "42" "0" "-1"
  "ORES_I64_OVERFLOW" "ORES_I64_OVERFLOW" "ORES_I64_OVERFLOW"
  "ORES_I64_OVERFLOW" "ORES_I64_OVERFLOW" "ORES_I64_OVERFLOW"
  "ORES_I64_OVERFLOW" "DIVISION_BY_ZERO")
set(expressions
  "0+0"
  "9223372036854775807+(-1)"
  "-9223372036854775808-(-1)"
  "-3037000499*3037000499"
  "-(-42)"
  "-9223372036854775808%(-1)"
  "-7%3"
  "9223372036854775807+1"
  "-9223372036854775808-1"
  "3037000500*3037000500"
  "-(-9223372036854775808)"
  "-9223372036854775808*(-1)"
  "(9223372036854775807+1)-1"
  "(-(-9223372036854775808))+1"
  "1%0")
foreach(n RANGE 0 14)
  list(GET rows ${n} record)
  string(REPLACE "|" ";" fields "${record}")
  list(LENGTH fields field_count)
  if(NOT field_count EQUAL 4)
    message(FATAL_ERROR "malformed shared checked-i64 case")
  endif()
  list(GET fields 0 actual_id)
  list(GET fields 1 actual_kind)
  list(GET fields 2 original_expression)
  list(GET fields 3 actual_expected)
  list(GET ids ${n} id)
  list(GET kinds ${n} kind)
  list(GET expectations ${n} expectation)
  list(GET expressions ${n} expression)
  if(NOT actual_id STREQUAL id OR NOT actual_kind STREQUAL kind
       OR NOT actual_expected STREQUAL expectation
       OR original_expression STREQUAL "")
    message(FATAL_ERROR "shared checked-i64 source case changed")
  endif()

  set(source "${OUT_DIR}/${id}.atto")
  set(cpp "${OUT_DIR}/${id}.cpp")
  set(binary "${OUT_DIR}/${id}")
  file(WRITE "${source}" "pub fnc main(): int { return ${expression}; }\n")
  execute_process(COMMAND "${FORGE}" "${source}" -o "${cpp}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "${id}: Forge compiler rejected reviewed scalar: ${err}")
  endif()
  execute_process(COMMAND "${CXX}" -std=c++20 -Wall -Wextra -Werror
      ${flags} "-I${ROOT}/include" "${cpp}" -o "${binary}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "${id}: native guest compile failed: ${err}")
  endif()
  if(kind STREQUAL "value")
    set(expected_rc "0")
    set(expected_out "${expectation}\n")
    set(expected_err "")
  elseif(kind STREQUAL "overflow")
    set(expected_rc "70")
    set(expected_out "")
    set(expected_err "E_OVERFLOW\n")
  elseif(kind STREQUAL "zero")
    set(expected_rc "71")
    set(expected_out "")
    set(expected_err "E_DIVZERO\n")
  else()
    message(FATAL_ERROR "unknown shared numeric outcome class")
  endif()
  execute_process(COMMAND "${binary}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "${expected_rc}" OR
     NOT "${out}" STREQUAL "${expected_out}" OR
     NOT "${err}" STREQUAL "${expected_err}")
    message(FATAL_ERROR
      "${id}: native guest got rc=[${rc}], out=[${out}], err=[${err}]")
  endif()
endforeach()
message(STATUS "Forge restricted native guest checked-i64 corpus 15/15 PASS; no full-language parity claim")
