# Experimental .atto guest compilation: not canonical module body compilation.
if(NOT DEFINED ROOT OR NOT DEFINED CXX OR NOT DEFINED FORGE_ALLOC OR NOT DEFINED OUT_DIR)
  message(FATAL_ERROR "MISSING_ALLOCATION_GUEST_TEST_ARGUMENT")
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")
set(guest_flags)
if(SANITIZERS)
  list(APPEND guest_flags -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()
function(compile_run name want_rc want_out want_err)
  set(source "${ROOT}/fixtures/alloc-guest-v1/${name}.atto")
  set(cpp "${OUT_DIR}/${name}.cpp")
  set(binary "${OUT_DIR}/${name}.bin")
  # Remove only owned build artifacts so repeated ctest runs are deterministic.
  file(REMOVE "${cpp}" "${binary}")
  execute_process(COMMAND "${FORGE_ALLOC}" --experimental-host-profile "${source}" -o "${cpp}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${cpp}")
    message(FATAL_ERROR "${name}: allocation compiler rejected guest: ${rc} ${err}")
  endif()
  execute_process(COMMAND "${CXX}" -std=c++20 -O1 -Wall -Wextra -Werror -pedantic -UNDEBUG
    ${guest_flags} "-I${ROOT}/include" "${cpp}" -o "${binary}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "${name}: generated native build failed: ${rc} ${err}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ASAN_OPTIONS=detect_leaks=1" "${binary}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "${want_rc}" OR
     NOT "${out}" STREQUAL "${want_out}" OR
     NOT "${err}" STREQUAL "${want_err}")
    message(FATAL_ERROR "${name}: compiled guest rc=${rc} stdout=[${out}] stderr=[${err}]")
  endif()
endfunction()
function(reject_source name expected)
  set(source "${ROOT}/fixtures/alloc-guest-v1/${name}.atto")
  set(cpp "${OUT_DIR}/${name}.cpp")
  file(REMOVE "${cpp}")
  execute_process(COMMAND "${FORGE_ALLOC}" --experimental-host-profile "${source}" -o "${cpp}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT "${rc}" STREQUAL "2" OR EXISTS "${cpp}" OR
     NOT "${err}" STREQUAL "${expected}\n")
    message(FATAL_ERROR "${name}: negative compiler admission rc=${rc} stderr=[${err}]")
  endif()
endfunction()
compile_run(equal "0" "1\n" "")
compile_run(zero "0" "0\n" "")
compile_run(write "0" "255\n" "")
compile_run(bounds "72" "" "E_BUFFER_BOUNDS\n")
compile_run(negative_size "74" "" "E_BUFFER_ALLOCATION\n")
compile_run(quota "75" "" "E_TENANT_QUOTA\n")
compile_run(bad_value "73" "" "E_BUFFER_VALUE\n")
compile_run(i64_min "0" "-9223372036854775808\n" "")
reject_source(immutable "E_ALLOC_IMMUTABLE")
reject_source(direct_owned "E_SEALED_ALLOC")
reject_source(invalid_import "E_ALLOC_IMPORT")

# Production-default entry is denied: the generated host principal is test-only.
set(no_optin "${OUT_DIR}/no-optin.cpp")
file(REMOVE "${no_optin}")
execute_process(COMMAND "${FORGE_ALLOC}"
  "${ROOT}/fixtures/alloc-guest-v1/equal.atto" -o "${no_optin}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "2" OR EXISTS "${no_optin}" OR
   NOT "${err}" STREQUAL "E_ALLOC_EXPERIMENTAL_REQUIRED\n")
  message(FATAL_ERROR "Ungated allocation guest compiler accepted a nonexperimental request")
endif()

# A symlink output must never redirect the compiler into an existing file.
set(protected "${OUT_DIR}/protected.txt")
set(alias "${OUT_DIR}/alias.cpp")
file(WRITE "${protected}" "do not overwrite")
file(REMOVE "${alias}")
file(CREATE_LINK "${protected}" "${alias}" SYMBOLIC RESULT link_result)
if(NOT "${link_result}" STREQUAL "0")
  message(FATAL_ERROR "Cannot create output-symlink regression")
endif()
execute_process(COMMAND "${FORGE_ALLOC}" --experimental-host-profile
  "${ROOT}/fixtures/alloc-guest-v1/equal.atto" -o "${alias}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
file(READ "${protected}" protected_bytes)
if(NOT "${rc}" STREQUAL "2" OR
   NOT "${protected_bytes}" STREQUAL "do not overwrite" OR NOT IS_SYMLINK "${alias}")
  message(FATAL_ERROR "Allocation compiler followed a symlink output")
endif()
file(REMOVE "${alias}")

# Source size budget rejects before emitting any guest artifact.
set(oversized "${OUT_DIR}/oversized.atto")
set(oversized_cpp "${OUT_DIR}/oversized.cpp")
string(REPEAT "x" 17000 huge)
file(WRITE "${oversized}" "${huge}")
file(REMOVE "${oversized_cpp}")
execute_process(COMMAND "${FORGE_ALLOC}" --experimental-host-profile
  "${oversized}" -o "${oversized_cpp}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "2" OR EXISTS "${oversized_cpp}" OR
   NOT "${err}" STREQUAL "E_ALLOC_LIMIT\n")
  message(FATAL_ERROR "Allocation compiler admitted over-budget source")
endif()

# An input symlink must not redirect the source reader to another file.
set(source_link "${OUT_DIR}/source-link.atto")
set(source_link_cpp "${OUT_DIR}/source-link.cpp")
file(REMOVE "${source_link}" "${source_link_cpp}")
file(CREATE_LINK "${ROOT}/fixtures/alloc-guest-v1/equal.atto"
  "${source_link}" SYMBOLIC RESULT link_result)
if(NOT "${link_result}" STREQUAL "0")
  message(FATAL_ERROR "Cannot create input-symlink regression")
endif()
execute_process(COMMAND "${FORGE_ALLOC}" --experimental-host-profile
  "${source_link}" -o "${source_link_cpp}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT "${rc}" STREQUAL "2" OR EXISTS "${source_link_cpp}" OR
   NOT "${err}" STREQUAL "E_ALLOC_SOURCE\n")
  message(FATAL_ERROR "Allocation compiler followed a symlink source")
endif()
file(REMOVE "${source_link}")
