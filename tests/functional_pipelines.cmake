# Real Atto source -> Forge C++ emission -> native executable.
# Not a host-language reference model or parser-only proof.
if(NOT DEFINED ROOT OR NOT DEFINED FORGE OR NOT DEFINED CXX OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "missing required Forge functional guest inputs")
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")
set(guest_flags)
if(SANITIZERS)
  list(APPEND guest_flags -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

function(compile_and_execute name expected)
  set(source "${ROOT}/fixtures/functional-pipelines-v1/${name}.atto")
  set(generated "${OUT_DIR}/${name}.cpp")
  set(replay "${OUT_DIR}/${name}-replay.cpp")
  set(executable "${OUT_DIR}/${name}")
  execute_process(COMMAND "${FORGE}" --check "${source}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0" OR NOT "${out}" STREQUAL "" OR NOT "${err}" STREQUAL "")
    message(FATAL_ERROR "${name}: admission rejected ${rc}: ${err}")
  endif()
  execute_process(COMMAND "${FORGE}" "${source}" -o "${generated}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0" OR NOT "${out}" STREQUAL "" OR NOT "${err}" STREQUAL "" OR NOT EXISTS "${generated}")
    message(FATAL_ERROR "${name}: code generation failed ${rc}: ${err}")
  endif()
  execute_process(COMMAND "${FORGE}" "${source}" -o "${replay}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${replay}")
    message(FATAL_ERROR "${name}: deterministic second compilation failed")
  endif()
  file(SHA256 "${generated}" first)
  file(SHA256 "${replay}" second)
  if(NOT "${first}" STREQUAL "${second}")
    message(FATAL_ERROR "${name}: emitted C++ is not deterministic")
  endif()
  execute_process(COMMAND "${CXX}" -std=c++20 -Wall -Wextra -Werror -pedantic
    ${guest_flags} "-I${ROOT}/include" "${generated}" -o "${executable}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "${name}: native build failed ${rc}: ${err}")
  endif()
  execute_process(COMMAND "${executable}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0" OR
     NOT "${out}" STREQUAL "${expected}\n" OR NOT "${err}" STREQUAL "")
    message(FATAL_ERROR "${name}: native guest mismatch exit=${rc}, stdout=[${out}], stderr=[${err}]")
  endif()
  message(STATUS "ATTO_FORGE_FUNCTIONAL_GUEST_EXECUTED ${name}=${expected} sha256=${first}")
endfunction()

compile_and_execute(block-body 83)
compile_and_execute(inline-arrow-body 83)
compile_and_execute(reverse-associative 42)
compile_and_execute(mixed-parenthesized 42)

function(reject_no_artifact name)
  set(source "${ROOT}/fixtures/functional-pipelines-v1/${name}.atto")
  set(output "${OUT_DIR}/${name}.cpp")
  file(REMOVE "${output}")
  execute_process(COMMAND "${FORGE}" --check "${source}"
    RESULT_VARIABLE check_rc OUTPUT_VARIABLE check_out ERROR_VARIABLE check_err)
  if(NOT "${check_rc}" STREQUAL "2" OR "${check_err}" STREQUAL "" OR NOT "${check_out}" STREQUAL "")
    message(FATAL_ERROR "${name}: invalid source accepted in check mode")
  endif()
  execute_process(COMMAND "${FORGE}" "${source}" -o "${output}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "2" OR
     NOT "${out}" STREQUAL "" OR "${err}" STREQUAL "" OR EXISTS "${output}")
    message(FATAL_ERROR "${name}: rejection left an artifact, rc=${rc}")
  endif()
  message(STATUS "ATTO_FORGE_FUNCTIONAL_REJECT_NO_ARTIFACT ${name}")
endfunction()

reject_no_artifact(reject-return-type)
reject_no_artifact(reject-unterminated-body)
reject_no_artifact(reject-ambiguous-mixed-direction)
reject_no_artifact(reject-reverse-mixed-unparenthesized)

set(extra_negative
  "fnc main(): int { val a = b; val b = 42; return a; }"
  "fnc main(): int { val a = 1; val a = 2; return a; }"
  "fnc double(int x): int -> x * 2; fnc main(): int -> 20 |> double(21);"
  "fnc main(): int -> 21 <| 42;"
  "fnc main(): int -> 20 |> missing;"
  "fnc main(): int -> 1 + true;"
  "fnc main(): int -> 9223372036854775808;"
  "fnc main(): int -> 1 / 0;"
  "fnc double(int x): int -> x*2; fnc main(): int -> double <| 1 |> double;"
  "fnc main(): int -> main();"
)
set(idx 0)
foreach(body IN LISTS extra_negative)
  math(EXPR idx "${idx}+1")
  set(source "${OUT_DIR}/negative-${idx}.atto")
  file(WRITE "${source}" "${body}\n")
  set(output "${OUT_DIR}/negative-${idx}.cpp")
  execute_process(COMMAND "${FORGE}" "${source}" -o "${output}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "2" OR EXISTS "${output}" OR
     NOT "${out}" STREQUAL "" OR "${err}" STREQUAL "")
    message(FATAL_ERROR "extra_negative ${idx}: unexpected acceptance/artifact")
  endif()
endforeach()
message(STATUS "ATTO_FORGE_FUNCTIONAL_GUEST_MATRIX=4 native_guests 14 negative_no_artifacts")
