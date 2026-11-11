cmake_minimum_required(VERSION 3.20)
if(NOT EXISTS "${FORGE}")
  message(FATAL_ERROR "missing real Forge compiler")
endif()
foreach(case_name IN ITEMS eq neq bang_eq eq_unreachable neq_unreachable bang_eq_unreachable)
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
  if(NOT "${exit_code}" STREQUAL "1" OR NOT output STREQUAL "")
    message(FATAL_ERROR "${case_name}: unsupported typed equality emitted native output")
  endif()
  string(FIND "${diagnostic}" "ATTO_EQ_BOOL_AOT_UNSUPPORTED" found)
  string(FIND "${diagnostic}" "at byte " offset)
  if(found EQUAL -1 OR offset EQUAL -1)
    message(FATAL_ERROR "${case_name}: wrong rejection diagnostic: ${diagnostic}")
  endif()
  message(STATUS "PASS ${case_name}: typed Boolean equality rejected before AOT emission")
endforeach()

# A split ! eq or a longer !equation identifier must not be treated as !eq.
# Both are invalid in the stage-0 grammar and must report the actual ! byte.
foreach(case_name IN ITEMS long_bang_eq split_bang_eq)
  set(input "${ROOT}/verification/fixtures/erl-otp-operator-aot/${case_name}.atto")
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "missing reviewed boundary fixture: ${case_name}")
  endif()
  file(READ "${input}" source)
  string(FIND "${source}" "return 7 !" return_prefix)
  if(return_prefix EQUAL -1)
    message(FATAL_ERROR "${case_name}: fixture lost invalid ! boundary")
  endif()
  math(EXPR bang_byte "${return_prefix} + 9")
  execute_process(
    COMMAND "${FORGE}" emit-asm x86_64 "${input}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE diagnostic
    TIMEOUT 10)
  if(NOT "${exit_code}" STREQUAL "1" OR NOT output STREQUAL "")
    message(FATAL_ERROR "${case_name}: malformed operator unexpectedly emitted assembly")
  endif()
  string(FIND "${diagnostic}" "invalid character at byte ${bang_byte}" expected)
  string(FIND "${diagnostic}" "ATTO_EQ_BOOL_AOT_UNSUPPORTED" wrong_alias)
  if(expected EQUAL -1 OR NOT wrong_alias EQUAL -1)
    message(FATAL_ERROR "${case_name}: incorrect lexical-boundary rejection: ${diagnostic}")
  endif()
  message(STATUS "PASS ${case_name}: invalid ! rejected at exact byte, not !eq alias")
endforeach()

# Positive control prevents a broken compiler that rejects everything from
# passing this negative-only AOT gate. Nested comment operator spellings
# must be ignored, and real native x86-64 assembly must be emitted.
set(positive "${ROOT}/verification/fixtures/erl-otp-operator-aot/comment_only.atto")
if(NOT EXISTS "${positive}")
  message(FATAL_ERROR "missing positive comment-only fixture")
endif()
execute_process(
  COMMAND "${FORGE}" emit-asm x86_64 "${positive}"
  RESULT_VARIABLE exit_code
  OUTPUT_VARIABLE assembly
  ERROR_VARIABLE diagnostic
  TIMEOUT 10)
string(FIND "${assembly}" ".text" text_section)
string(FIND "${assembly}" "movabs $15, %rax" expected_value)
if(NOT "${exit_code}" STREQUAL "0" OR
   NOT diagnostic STREQUAL "" OR
   text_section EQUAL -1 OR
   expected_value EQUAL -1)
  message(FATAL_ERROR "positive comment-only i64 fixture failed native code generation: ${diagnostic}")
endif()
message(STATUS "PASS comment_only: operators in nested comments ignored, native i64 assembly emitted")
