# Real Atto source -> Forge C++ emission -> native executable.
# Not a host-language reference model or parser-only proof.
if(NOT DEFINED ROOT OR NOT DEFINED FORGE OR NOT DEFINED CXX OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "missing required Forge functional guest inputs")
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")

# Path-based symlink preflights are not a security boundary. Validate
# that the actual Forge source reader rejects a linked inode and a FIFO
# without blocking, and rejects an oversized opened regular file.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  set(valid_source "${OUT_DIR}/source-open-valid.atto")
  set(symlink_source "${OUT_DIR}/source-open-symlink.atto")
  set(fifo_source "${OUT_DIR}/source-open-fifo.atto")
  set(oversize_source "${OUT_DIR}/source-open-too-big.atto")
  file(WRITE "${valid_source}" "pub fnc main(): int -> 83;\n")
  file(REMOVE "${symlink_source}" "${fifo_source}" "${oversize_source}")
  file(CREATE_LINK "${valid_source}" "${symlink_source}" SYMBOLIC
       RESULT symlink_status)
  if(NOT "${symlink_status}" STREQUAL "0")
    message(FATAL_ERROR "Forge source descriptor test cannot create linked fixture")
  endif()
  execute_process(COMMAND mkfifo "${fifo_source}"
    RESULT_VARIABLE fifo_status)
  if(NOT "${fifo_status}" STREQUAL "0")
    message(FATAL_ERROR "Forge source descriptor test cannot create FIFO")
  endif()
  string(REPEAT "x" 16385 too_large)
  file(WRITE "${oversize_source}" "${too_large}")
  foreach(row IN ITEMS
      "source-open-symlink:E_SOURCE"
      "source-open-fifo:E_SOURCE"
      "source-open-too-big:E_SOURCE_LIMIT")
    string(REPLACE ":" ";" tokens "${row}")
    list(GET tokens 0 name)
    list(GET tokens 1 expected)
    execute_process(COMMAND "${FORGE}" --check "${OUT_DIR}/${name}.atto"
      TIMEOUT 5 RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT "${rc}" STREQUAL "2" OR NOT "${out}" STREQUAL "" OR
       NOT "${err}" STREQUAL "${expected}\n")
      message(FATAL_ERROR
        "Forge source open must fail closed: ${name} exit=${rc} stderr=${err}")
    endif()
  endforeach()
  file(REMOVE "${symlink_source}" "${fifo_source}" "${oversize_source}"
       "${valid_source}")
endif()

# Exact source-fixture Git blobs copied from atto-spec#121. A drifted
# fixture must fail before C++ emission, even if the output looks correct.
function(assert_pinned_blob file expected)
  set(source "${ROOT}/fixtures/functional-pipelines-v1/${file}.atto")
  execute_process(COMMAND git hash-object "${source}"
    WORKING_DIRECTORY "${ROOT}" RESULT_VARIABLE rc
    OUTPUT_VARIABLE hash ERROR_VARIABLE err OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT "${rc}" STREQUAL "0" OR NOT "${hash}" STREQUAL "${expected}")
    message(FATAL_ERROR "canonical Atto fixture blob mismatch: ${file}, observed=${hash}, error=${err}")
  endif()
endfunction()
assert_pinned_blob(block-body 4a2eab34e1ba39709198afda19a71cfce67f4030)
assert_pinned_blob(inline-arrow-body ec25b1ce607afa7d30dd5315eace12ff42f0c4d6)
assert_pinned_blob(reverse-associative e71023cd0cc258c9650167e979c45ffb5162d9f7)
assert_pinned_blob(mixed-parenthesized 0fcdce8f0b1381125d0148122441c81dfb2a0abf)
assert_pinned_blob(reject-return-type 74b2f398a72b41407039ac68cdc44b23b7ece125)
assert_pinned_blob(reject-unterminated-body 1d84e3f31fee46a73a6c111e406ebb883e5645b2)
assert_pinned_blob(reject-ambiguous-mixed-direction 7e579225f25720da5255ecb47640831be6e3a1c7)
assert_pinned_blob(reject-reverse-mixed-unparenthesized 2727d012e5482c407c2e642b3612144e67fad767)
assert_pinned_blob(edge/two-argument-61 4089c524db1d076593994ad65acb5ca7f90eea32)
assert_pinned_blob(edge/preapplied-grouped-42 9a89ada18865fead0d92a7de75415957fbb5f67f)
assert_pinned_blob(edge/signed-i64-min bbc22347d7ccb0a5be1a7c2193f11c5f2f4805cc)
assert_pinned_blob(edge/checked-overflow 59bb890b54addc1659065ab55cd565771523bfce)
assert_pinned_blob(edge/reject-i64-positive-overflow 2e584ee01ee74a3a31e282cf0116957f9ff95bd1)
assert_pinned_blob(edge/reject-i64-negative-underflow 8b63b45e406db3e19aa793e0f7a627914623b9e0)
assert_pinned_blob(edge/reject-reserved-unsafe b159def3fa26ca6023395e1c546b95c1b929fd14)

set(guest_flags)
if(SANITIZERS)
  list(APPEND guest_flags -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

function(compile_and_execute name expected)
  set(source "${ROOT}/fixtures/functional-pipelines-v1/${name}.atto")
  set(generated "${OUT_DIR}/${name}.cpp")
  set(replay "${OUT_DIR}/${name}-replay.cpp")
  set(executable "${OUT_DIR}/${name}")
  # CTest reruns must remain clean even when an earlier run generated guests.
  file(REMOVE "${generated}" "${replay}" "${executable}")
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

# Supplemental spec edge v1: real native execution.
function(compile_edge_guest name expected expected_status expected_error)
  set(source "${ROOT}/fixtures/functional-pipelines-v1/edge/${name}.atto")
  set(generated "${OUT_DIR}/edge-${name}.cpp")
  set(executable "${OUT_DIR}/edge-${name}")
  file(REMOVE "${generated}" "${executable}" "${generated}.part")
  execute_process(COMMAND "${FORGE}" --check "${source}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0" OR NOT "${out}" STREQUAL "" OR NOT "${err}" STREQUAL "")
    message(FATAL_ERROR "edge ${name}: accepted Atto program rejected: ${err}")
  endif()
  execute_process(COMMAND "${FORGE}" "${source}" -o "${generated}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${generated}" OR NOT "${err}" STREQUAL "")
    message(FATAL_ERROR "edge ${name}: missing compiled C++ output")
  endif()
  execute_process(COMMAND "${CXX}" -std=c++20 -Wall -Wextra -Werror -pedantic
    ${guest_flags} "-I${ROOT}/include" "${generated}" -o "${executable}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "edge ${name}: C++ native compilation failed: ${err}")
  endif()
  execute_process(COMMAND "${executable}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "${expected_status}" OR
     NOT "${out}" STREQUAL "${expected}" OR
     NOT "${err}" STREQUAL "${expected_error}")
    message(FATAL_ERROR "edge ${name}: native exit=${rc}, stdout=[${out}], stderr=[${err}]")
  endif()
  file(SHA256 "${generated}" digest)
  message(STATUS "ATTO_FORGE_EDGE_GUEST_EXECUTED ${name} sha256=${digest}")
endfunction()
compile_edge_guest(two-argument-61 "61\n" 0 "")
compile_edge_guest(preapplied-grouped-42 "42\n" 0 "")
compile_edge_guest(signed-i64-min "-9223372036854775808\n" 0 "")
compile_edge_guest(checked-overflow "" 70 "E_OVERFLOW\n")

# Invalid literal/authority cases must fail before guest emission.
foreach(row IN ITEMS
  "reject-i64-positive-overflow:E_LITERAL"
  "reject-i64-negative-underflow:E_LITERAL"
  "reject-reserved-unsafe:E_NAME")
  string(REPLACE ":" ";" parts "${row}")
  list(GET parts 0 name)
  list(GET parts 1 expected)
  set(source "${ROOT}/fixtures/functional-pipelines-v1/edge/${name}.atto")
  set(out_file "${OUT_DIR}/edge-${name}.cpp")
  file(REMOVE "${out_file}")
  execute_process(COMMAND "${FORGE}" --check "${source}"
    RESULT_VARIABLE check_rc OUTPUT_VARIABLE check_out ERROR_VARIABLE check_err)
  if(NOT "${check_rc}" STREQUAL "2" OR NOT "${check_out}" STREQUAL ""
     OR NOT "${check_err}" STREQUAL "${expected}\n")
    message(FATAL_ERROR "edge ${name}: invalid source accepted by --check")
  endif()
  execute_process(COMMAND "${FORGE}" "${source}" -o "${out_file}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "2" OR EXISTS "${out_file}"
     OR NOT "${out}" STREQUAL "" OR NOT "${err}" STREQUAL "${expected}\n")
    message(FATAL_ERROR "edge ${name}: rejected source produced an artifact")
  endif()
endforeach()


function(reject_no_artifact name diagnostic)
  set(source "${ROOT}/fixtures/functional-pipelines-v1/${name}.atto")
  set(output "${OUT_DIR}/${name}.cpp")
  file(REMOVE "${output}")
  execute_process(COMMAND "${FORGE}" --check "${source}"
    RESULT_VARIABLE check_rc OUTPUT_VARIABLE check_out ERROR_VARIABLE check_err)
  string(FIND "${check_err}" "${diagnostic}" check_found)
  if(NOT "${check_rc}" STREQUAL "2" OR check_found EQUAL -1 OR NOT "${check_out}" STREQUAL "")
    message(FATAL_ERROR "${name}: invalid source accepted in check mode")
  endif()
  execute_process(COMMAND "${FORGE}" "${source}" -o "${output}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  string(FIND "${err}" "${diagnostic}" emit_found)
  if(NOT "${rc}" STREQUAL "2" OR emit_found EQUAL -1 OR
     NOT "${out}" STREQUAL "" OR EXISTS "${output}")
    message(FATAL_ERROR "${name}: rejection left an artifact, rc=${rc}")
  endif()
  message(STATUS "ATTO_FORGE_FUNCTIONAL_REJECT_NO_ARTIFACT ${name}")
endfunction()

reject_no_artifact(reject-return-type E_TYPE)
reject_no_artifact(reject-unterminated-body E_PARSE)
reject_no_artifact(reject-ambiguous-mixed-direction E_MIXED_PIPE)
reject_no_artifact(reject-reverse-mixed-unparenthesized E_MIXED_PIPE)

file(WRITE "${OUT_DIR}/negative-1.atto" "fnc main(): int { val a = b; val b = 42; return a; }\n")
file(WRITE "${OUT_DIR}/negative-2.atto" "fnc main(): int { val a = 1; val a = 2; return a; }\n")
file(WRITE "${OUT_DIR}/negative-3.atto" "fnc double(int x): int -> x * 2; fnc main(): int -> 20 |> double(21);\n")
file(WRITE "${OUT_DIR}/negative-4.atto" "fnc main(): int -> 21 <| 42;\n")
file(WRITE "${OUT_DIR}/negative-5.atto" "fnc main(): int -> 20 |> missing;\n")
file(WRITE "${OUT_DIR}/negative-6.atto" "fnc main(): int -> 1 + true;\n")
file(WRITE "${OUT_DIR}/negative-7.atto" "fnc main(): int -> 9223372036854775808;\n")
file(WRITE "${OUT_DIR}/negative-8.atto" "fnc main(): int -> 1 / 0;\n")
file(WRITE "${OUT_DIR}/negative-9.atto" "fnc double(int x): int -> x*2; fnc main(): int -> double <| 1 |> double;\n")
file(WRITE "${OUT_DIR}/negative-10.atto" "fnc main(): int -> main();\n")

foreach(idx RANGE 1 10)
  set(source "${OUT_DIR}/negative-${idx}.atto")
  set(output "${OUT_DIR}/negative-${idx}.cpp")
  file(REMOVE "${output}")
  execute_process(COMMAND "${FORGE}" "${source}" -o "${output}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "2" OR EXISTS "${output}" OR
     NOT "${out}" STREQUAL "" OR "${err}" STREQUAL "")
    message(FATAL_ERROR "extra_negative ${idx}: unexpected acceptance/artifact")
  endif()
endforeach()

# The signed 64-bit minimum requires special parsing and C++ emission;
# its positive magnitude must remain a rejected literal.
set(boundary_source "${OUT_DIR}/i64-min.atto")
set(boundary_cpp "${OUT_DIR}/i64-min.cpp")
set(boundary_bin "${OUT_DIR}/i64-min")
file(WRITE "${boundary_source}" "pub fnc main(): int -> -9223372036854775808;\n")
file(REMOVE "${boundary_cpp}" "${boundary_bin}")
execute_process(COMMAND "${FORGE}" "${boundary_source}" -o "${boundary_cpp}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${boundary_cpp}" OR
   NOT "${out}" STREQUAL "" OR NOT "${err}" STREQUAL "")
  message(FATAL_ERROR "signed i64 minimum rejected before native compilation: ${err}")
endif()
execute_process(COMMAND "${CXX}" -std=c++20 -Wall -Wextra -Werror -pedantic
  ${guest_flags} "-I${ROOT}/include" "${boundary_cpp}" -o "${boundary_bin}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "signed i64 minimum produced invalid generated C++: ${err}")
endif()
execute_process(COMMAND "${boundary_bin}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0" OR
   NOT "${out}" STREQUAL "-9223372036854775808\n" OR
   NOT "${err}" STREQUAL "")
  message(FATAL_ERROR "signed i64 minimum native guest mismatch ${rc}: ${out} ${err}")
endif()

# Checked arithmetic overflow is a runtime rejection, not wraparound.
set(overflow_source "${OUT_DIR}/i64-overflow.atto")
set(overflow_cpp "${OUT_DIR}/i64-overflow.cpp")
set(overflow_bin "${OUT_DIR}/i64-overflow")
file(WRITE "${overflow_source}" "pub fnc main(): int -> 9223372036854775807 + 1;\n")
file(REMOVE "${overflow_cpp}" "${overflow_bin}")
execute_process(COMMAND "${FORGE}" "${overflow_source}" -o "${overflow_cpp}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${overflow_cpp}")
  message(FATAL_ERROR "overflow guest source should compile, but must trap when run")
endif()
execute_process(COMMAND "${CXX}" -std=c++20 -Wall -Wextra -Werror -pedantic
  ${guest_flags} "-I${ROOT}/include" "${overflow_cpp}" -o "${overflow_bin}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "overflow guest source failed native build: ${err}")
endif()
execute_process(COMMAND "${overflow_bin}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "70" OR
   NOT "${out}" STREQUAL "" OR
   NOT "${err}" STREQUAL "E_OVERFLOW\n")
  message(FATAL_ERROR "overflow guest must reject with error 70, got ${rc}: ${out} ${err}")
endif()

file(WRITE "${OUT_DIR}/negative-11.atto"
  "pub fnc main(): int -> -9223372036854775809;\n")
set(rejected "${OUT_DIR}/negative-11.cpp")
file(REMOVE "${rejected}")
execute_process(COMMAND "${FORGE}" "${OUT_DIR}/negative-11.atto" -o "${rejected}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "2" OR EXISTS "${rejected}" OR
   NOT "${out}" STREQUAL "" OR NOT "${err}" STREQUAL "E_LITERAL\n")
  message(FATAL_ERROR "out-of-range signed i64 negative must fail with no artifact")
endif()

# Reserved authority/effect names cannot be compiled as ordinary scalar
# functions or lexical locals; this is a gate, not an actor implementation.
file(WRITE "${OUT_DIR}/negative-12.atto"
  "fnc unsafe(int x): int -> x; pub fnc main(): int -> unsafe(42);\n")
file(WRITE "${OUT_DIR}/negative-13.atto"
  "fnc await(int x): int -> x; pub fnc main(): int -> await(42);\n")
file(WRITE "${OUT_DIR}/negative-14.atto"
  "fnc pipe(int x): int -> x; pub fnc main(): int -> pipe(42);\n")
foreach(idx RANGE 12 14)
  set(output "${OUT_DIR}/negative-${idx}.cpp")
  file(REMOVE "${output}")
  execute_process(COMMAND "${FORGE}" "${OUT_DIR}/negative-${idx}.atto"
    -o "${output}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "2" OR EXISTS "${output}" OR
     NOT "${out}" STREQUAL "" OR NOT "${err}" STREQUAL "E_NAME\n")
    message(FATAL_ERROR "reserved authority name escaped the scalar compiler")
  endif()
endforeach()

# Filename-only output must be accepted in a real current working
# directory and remain no-clobber on its second invocation.
set(relative_cpp "${OUT_DIR}/relative-guest.cpp")
set(relative_bin "${OUT_DIR}/relative-guest")
file(REMOVE "${relative_cpp}" "${relative_bin}" "${relative_cpp}.part")
execute_process(COMMAND "${FORGE}" "${ROOT}/fixtures/functional-pipelines-v1/block-body.atto"
  -o "relative-guest.cpp"
  WORKING_DIRECTORY "${OUT_DIR}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${relative_cpp}" OR
   NOT "${out}" STREQUAL "" OR NOT "${err}" STREQUAL "")
  message(FATAL_ERROR "filename-only -o path rejected by the Forge compiler")
endif()
file(SHA256 "${relative_cpp}" relative_sha)
execute_process(COMMAND "${FORGE}" "${ROOT}/fixtures/functional-pipelines-v1/block-body.atto"
  -o "relative-guest.cpp"
  WORKING_DIRECTORY "${OUT_DIR}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
file(SHA256 "${relative_cpp}" observed_sha)
if(NOT "${rc}" STREQUAL "2" OR NOT "${relative_sha}" STREQUAL "${observed_sha}")
  message(FATAL_ERROR "relative-path no-overwrite compiler gate failed")
endif()
execute_process(COMMAND "${CXX}" -std=c++20 -Wall -Wextra -Werror -pedantic
  ${guest_flags} "-I${ROOT}/include" "${relative_cpp}" -o "${relative_bin}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "relative-path native guest failed to compile: ${err}")
endif()
execute_process(COMMAND "${relative_bin}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "0" OR NOT "${out}" STREQUAL "83\n" OR NOT "${err}" STREQUAL "")
  message(FATAL_ERROR "relative-path native guest result mismatch")
endif()

# Stale output must not be replaced, including output and temp symlinks.
set(safe_source "${ROOT}/fixtures/functional-pipelines-v1/block-body.atto")
set(guard "${OUT_DIR}/guarded.cpp")
set(guard_temp "${guard}.part")
set(protected "${OUT_DIR}/protected.txt")
file(WRITE "${protected}" "do not replace")
file(WRITE "${guard}" "preexisting output")
execute_process(COMMAND "${FORGE}" "${safe_source}" -o "${guard}"
  RESULT_VARIABLE rc)
file(READ "${guard}" kept)
if(NOT "${rc}" STREQUAL "2" OR NOT "${kept}" STREQUAL "preexisting output")
  message(FATAL_ERROR "Forge pipeline publisher overwrote a preexisting output")
endif()
file(REMOVE "${guard}")
file(CREATE_LINK "${protected}" "${guard}" SYMBOLIC RESULT link_result)
if(NOT "${link_result}" STREQUAL "0")
  message(FATAL_ERROR "cannot establish symlink output regression")
endif()
execute_process(COMMAND "${FORGE}" "${safe_source}" -o "${guard}"
  RESULT_VARIABLE rc)
file(READ "${protected}" kept)
if(NOT "${rc}" STREQUAL "2" OR NOT "${kept}" STREQUAL "do not replace")
  message(FATAL_ERROR "Forge pipeline publisher followed output symlink")
endif()
file(REMOVE "${guard}")
file(CREATE_LINK "${protected}" "${guard_temp}" SYMBOLIC RESULT link_result)
if(NOT "${link_result}" STREQUAL "0")
  message(FATAL_ERROR "cannot establish symlink temporary regression")
endif()
execute_process(COMMAND "${FORGE}" "${safe_source}" -o "${guard}"
  RESULT_VARIABLE rc)
file(READ "${protected}" kept)
if(NOT "${rc}" STREQUAL "2" OR NOT "${kept}" STREQUAL "do not replace"
   OR EXISTS "${guard}")
  message(FATAL_ERROR "Forge pipeline publisher followed temporary symlink")
endif()
file(REMOVE "${guard_temp}")
# A preexisting ordinary .part file must be neither truncated nor removed.
file(WRITE "${guard_temp}" "do not truncate")
execute_process(COMMAND "${FORGE}" "${safe_source}" -o "${guard}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
file(READ "${guard_temp}" kept)
if(NOT "${rc}" STREQUAL "2" OR NOT "${kept}" STREQUAL "do not truncate"
   OR EXISTS "${guard}")
  message(FATAL_ERROR "Forge functional writer truncated a competing regular .part file")
endif()
file(REMOVE "${guard_temp}")

# Private file mode is required for emitted code while still in a potentially
# multi-tenant build workspace. Linux runner's stat format is deterministic.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  execute_process(COMMAND stat -c "%a" "${relative_cpp}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE mode OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT "${rc}" STREQUAL "0" OR NOT "${mode}" STREQUAL "600")
    message(FATAL_ERROR "generated native source must be mode 0600 (saw ${mode})")
  endif()
endif()

# Source-level authority words must never be treated as scalar
# callables or names, regardless of whether they appear at declaration,
# parameter, or local-binding positions.
foreach(keyword IN ITEMS sync task yield)
  foreach(location IN ITEMS function parameter local)
    if(location STREQUAL "function")
      set(source "fnc ${keyword}(int x): int -> x; pub fnc main(): int -> 42 |> ${keyword};")
    elseif(location STREQUAL "parameter")
      set(source "fnc f(int ${keyword}): int -> 42; pub fnc main(): int -> f(1);")
    else()
      set(source "pub fnc main(): int { val ${keyword} = 42; return ${keyword}; }")
    endif()
    set(input "${OUT_DIR}/reserved-${keyword}-${location}.atto")
    set(generated "${OUT_DIR}/reserved-${keyword}-${location}.cpp")
    file(WRITE "${input}" "${source}\n")
    file(REMOVE "${generated}")
    execute_process(COMMAND "${FORGE}" "${input}" -o "${generated}"
      RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT "${rc}" STREQUAL "2" OR EXISTS "${generated}" OR
       NOT "${stdout}" STREQUAL "" OR NOT "${stderr}" STREQUAL "E_NAME\n")
      message(FATAL_ERROR "reserved ${keyword}/${location} admitted or emitted guest")
    endif()
  endforeach()
endforeach()

# Real concurrency/native race test of the actual Forge CLI, only on Linux
# where linkat(AT_EMPTY_PATH) publishes its O_TMPFILE inode without names.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
  execute_process(
    COMMAND bash "${ROOT}/tests/functional_publication_race.sh"
      "${FORGE}"
      "${ROOT}/fixtures/functional-pipelines-v1/block-body.atto"
      "${OUT_DIR}" "${CXX}" "${ROOT}/include" ${guest_flags}
    RESULT_VARIABLE publication_rc
    OUTPUT_VARIABLE publication_out
    ERROR_VARIABLE publication_err)
  if(NOT "${publication_rc}" STREQUAL "0" OR
     NOT "${publication_out}" MATCHES "ATTO_FORGE_LINUX_FD_PUBLICATION_PASS")
    message(FATAL_ERROR
      "Forge Linux fd-anchored publisher gate failed: exit=${publication_rc}
stdout=${publication_out}
stderr=${publication_err}")
  endif()
  message(STATUS "${publication_out}")
endif()

message(STATUS "ATTO_FORGE_FUNCTIONAL_GUEST_MATRIX=7 native_guests_including_min_overflow_and_relative_path 27 negative_no_artifacts")
