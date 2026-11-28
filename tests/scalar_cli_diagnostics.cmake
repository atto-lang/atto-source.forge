# Execute the native Forge CLI and verify stable source-safe diagnostics.
if(NOT DEFINED FORGE OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "missing compiler/diagnostics destination")
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")
function(check_diag label source expected)
  set(input "${OUT_DIR}/diagnostic_${label}.atto")
  set(output "${OUT_DIR}/diagnostic_${label}.cpp")
  file(WRITE "${input}" "${source}")
  file(WRITE "${output}" "stale source")
  execute_process(COMMAND "${FORGE}" "${input}" -o "${output}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "2" OR NOT EXISTS "${output}" OR NOT "${out}" STREQUAL "")
    message(FATAL_ERROR "${label}: invalid source removed existing output")
  endif()
  file(READ "${output}" after)
  if(NOT "${after}" STREQUAL "stale source")
    message(FATAL_ERROR "${label}: invalid source modified existing output")
  endif()
  set(fresh "${OUT_DIR}/diagnostic_${label}_new.cpp")
  file(REMOVE "${fresh}" "${fresh}.part")
  execute_process(COMMAND "${FORGE}" "${input}" -o "${fresh}"
    RESULT_VARIABLE rc_new OUTPUT_VARIABLE out_new ERROR_VARIABLE err_new)
  if(NOT "${rc_new}" STREQUAL "2")
    message(FATAL_ERROR "${label}: invalid source exit=${rc_new}, stderr=[${err_new}]")
  endif()
  if(EXISTS "${fresh}" OR EXISTS "${fresh}.part")
    message(FATAL_ERROR "${label}: rejected source created an output artifact")
  endif()
  if(NOT "${out_new}" STREQUAL "")
    message(FATAL_ERROR "${label}: rejected source wrote unexpected stdout=[${out_new}]")
  endif()
  if(NOT "${err_new}" MATCHES "^${expected} byte=[0-9]+\n$")
    message(FATAL_ERROR "${label}: fresh-output rejection diagnostic mismatch, got [${err_new}]")
  endif()
  if(NOT "${err}" MATCHES "^${expected} byte=[0-9]+\n$")
    message(FATAL_ERROR "${label}: expected ${expected} with byte offset, got [${err}]")
  endif()
endfunction()
check_diag(lexical "fnc main(): int {return @;}" "E_PARSE_LEXICAL")
check_diag(ownership "fnc main(): int {return copy!42;}" "E_OWNERSHIP_BANG_UNSUPPORTED")
string(REPEAT "/*" 129 too_deep)
string(REPEAT "*/" 129 close_deep)
check_diag(comment_nesting "fnc main(): int {return ${too_deep}42${close_deep};}" "E_PARSE_LEXICAL")
check_diag(syntax "fnc main(): int {return 1 + ;}" "E_PARSE_SYNTAX")
check_diag(range "fnc main(): int {return 9223372036854775808;}" "E_PARSE_RANGE")
string(REPEAT "(" 40 left)
string(REPEAT ")" 40 right)
check_diag(depth "fnc main(): int {return ${left}1${right};}" "E_PARSE_DEPTH")
set(many "fnc main(): int {return 1")
foreach(i RANGE 1 134)
  string(APPEND many "+1")
endforeach()
string(APPEND many ";}")
check_diag(nodes "${many}" "E_PARSE_NODES")

check_diag(infix_caret "fnc main(): int {return 12 ^ 10;}" "E_PARSE_SYNTAX")
check_diag(split_xor "fnc main(): int {return 12 ^ ^ 10;}" "E_PARSE_SYNTAX")
check_diag(double_return "fnc main(): int {^^ 42;}" "E_PARSE_SYNTAX")
check_diag(boolean_xor "fnc main(): int {return true ^^ false;}" "E_PARSE_SYNTAX")

check_diag(grouped_spawn "fnc main(): int {return spawn Worker() with {};}" "E_SPAWN_OPTIONS_UNSUPPORTED")

# Scalar direction denial remains distinct from the earlier suspension gate.
check_diag(pipe "fnc main(): int {return 1 pipe 2;}" "E_UNSUPPORTED_CONTINUOUS_PIPE")
check_diag(rpipe "fnc main(): int {return 2 rpipe 1;}" "E_UNSUPPORTED_CONTINUOUS_PIPE")
# Exact same UTF-8 source bytes as atto-platform-conformance PR #102.
# The narrow native scalar compiler must reject each pipeline with a stable
# existing earlier suspension-unsupported diagnostic, not produce a guessed C++ executable.
if(NOT DEFINED ROOT)
  message(FATAL_ERROR "ROOT required for pinned .atto conformance fixtures")
endif()
foreach(entry IN ITEMS
  "actor-forward:dd59abdb707d05dabac30139feb4bd03b60459d1"
  "actor-reverse:da3d759d56aac4cfd6919521df862edb594f426f"
  "actor-missing-emits:6755a01ce43f7ac33f64d86d43dff7c7257185ca"
  "actor-wrong-direction:12ee1a935c43477213604593d73b0cecf761fead"
  "mixed-channel-stream-forward:6e2528f7c4b278c0fff9671c8c5985bfbd94adb1"
  "mixed-channel-stream-reverse:060b61b80cfb3946a9510edd1190be6256212424"
  "actor-duplicate-direction:d738937d72c791c41d55f088eaed65a5f9ed224f"
  "actor-mixed-directions:1addd9cc44879d24f7e76b2d5eb69d6837a711ab"
  "stopped-source:cdfa3473793a0eed556288db06ebaa3ed8f9d4c7"
  "closed-channel:523e0da79a1e373c03c1ef216259b34c0b0c0889"
  "stopped-consumer:8896a1bf0aef74926be291ace2d86ecf852985b1"
  "stopped-actor-sink:329661c81fa4bb832fcacbbff5a4fb068e1bb6dd"
)
  string(REPLACE ":" ";" columns "${entry}")
  list(GET columns 0 fixture_name)
  list(GET columns 1 expected_blob)
  set(fixture "${ROOT}/fixtures/pipe-rpipe-v1/${fixture_name}.atto")
  execute_process(
    COMMAND git hash-object "${fixture}"
    OUTPUT_VARIABLE current_blob
    RESULT_VARIABLE hash_rc
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT "${hash_rc}" STREQUAL "0" OR NOT "${current_blob}" STREQUAL "${expected_blob}")
    message(FATAL_ERROR "${fixture_name}: canonical source Git blob mismatch")
  endif()
  file(READ "${fixture}" source)
  check_diag("canonical_${fixture_name}" "${source}" "E_SUSPENDS_UNSUPPORTED")
endforeach()
message(STATUS "ATTO_FORGE_CANONICAL_PIPE_FAILCLOSED checks=12")
