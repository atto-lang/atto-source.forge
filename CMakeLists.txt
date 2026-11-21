cmake_minimum_required(VERSION 3.20)
project(OresForge VERSION 0.1.0 LANGUAGES CXX)
option(ORESFORGE_FUZZ "Build Clang libFuzzer target" OFF)
add_library(oresforge_core STATIC compiler/frontend.cpp compiler/ir.cpp compiler/call_native.cpp codegen/native.cpp runtime/actor.cpp runtime/generator_frame.cpp jit/tiered.cpp)
target_include_directories(oresforge_core PUBLIC .)
target_compile_features(oresforge_core PUBLIC cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(oresforge_core PRIVATE -Wall -Wextra -Wconversion)
endif()
add_executable(oresforge compiler/main.cpp)
target_link_libraries(oresforge PRIVATE oresforge_core)
include(CTest)
if(BUILD_TESTING)
  # Real source -> verified Forge callable IR -> direct native x86_64 call
  # admission. Separate from arbitrary nominal/trait/actor dispatch.
  add_executable(oresforge_static_dispatch_aot_verifier
    verification/static_dispatch_aot_test.cpp)
  target_link_libraries(oresforge_static_dispatch_aot_verifier PRIVATE oresforge_core)
  target_compile_features(oresforge_static_dispatch_aot_verifier PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_static_dispatch_aot_verifier PRIVATE
      -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  endif()
  add_test(NAME oresforge_static_dispatch_aot_source_and_ir
    COMMAND oresforge_static_dispatch_aot_verifier
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/static-dispatch/direct-42.atto")
  set_tests_properties(oresforge_static_dispatch_aot_source_and_ir
    PROPERTIES TIMEOUT 20)

  # Real .atto -> callable IR -> x86_64 SysV assembly -> linked/executed
  # native guests at O0 and O2. Linux-only signal assertions are deliberate.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
     CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$" AND
     CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_test(NAME oresforge_static_dispatch_aot_compiled_native_guests
      COMMAND "${CMAKE_COMMAND}"
        "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DVERIFIER=$<TARGET_FILE:oresforge_static_dispatch_aot_verifier>"
        "-DCXX=${CMAKE_CXX_COMPILER}"
        "-DHOST_ARCH=${CMAKE_SYSTEM_PROCESSOR}"
        "-DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/static-dispatch-native-guests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verification/static_dispatch_native_guests.cmake")
    set_tests_properties(oresforge_static_dispatch_aot_compiled_native_guests
      PROPERTIES TIMEOUT 180)
  endif()

  # All Forge source frontends must deny raw ESC and DEL before lowering.
  # This adds execution coverage; source admission already resides in main.
  add_executable(oresforge_source_control_admission_test
    tests/source_control_admission_test.cpp)
  target_include_directories(oresforge_source_control_admission_test PRIVATE .)
  target_link_libraries(oresforge_source_control_admission_test PRIVATE oresforge_core)
  target_compile_features(oresforge_source_control_admission_test PRIVATE cxx_std_20)
  add_test(NAME forge_source_terminal_control_admission
    COMMAND oresforge_source_control_admission_test)
  set_tests_properties(forge_source_terminal_control_admission PROPERTIES TIMEOUT 15)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(ORE_NUMERIC_I64_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated_numeric_i64_cases.hpp")
  add_custom_command(
    OUTPUT "${ORE_NUMERIC_I64_HEADER}"
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/generate_numeric_i64_cases.py"
      --corpus "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1/numeric-parity/source-i64-cases.json"
      --output "${ORE_NUMERIC_I64_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/generate_numeric_i64_cases.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1/numeric-parity/source-i64-cases.json"
    VERBATIM)
  add_custom_target(oresforge_numeric_i64_cases DEPENDS "${ORE_NUMERIC_I64_HEADER}")
  set(ORE_NUMERIC_OVERFLOW_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated_numeric_overflow_cases.hpp")
  add_custom_command(
    OUTPUT "${ORE_NUMERIC_OVERFLOW_HEADER}"
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/generate_numeric_overflow_cases.py"
      --corpus "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1/numeric-parity/source-i64-overflow-cases.json"
      --output "${ORE_NUMERIC_OVERFLOW_HEADER}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/generate_numeric_overflow_cases.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/generate_numeric_i64_cases.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1/numeric-parity/source-i64-overflow-cases.json"
    VERBATIM)
  add_custom_target(oresforge_numeric_overflow_cases DEPENDS "${ORE_NUMERIC_OVERFLOW_HEADER}")
  # Host compiler source-byte admission test: no guest-language execution
  # or Python tooling is required.
  add_executable(oresforge_source_utf8_offsets
    verification/source_utf8_offsets_test.cpp)
  target_include_directories(oresforge_source_utf8_offsets PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(oresforge_source_utf8_offsets PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_source_utf8_offsets PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME oresforge_source_utf8_offsets COMMAND oresforge_source_utf8_offsets)
  set_tests_properties(oresforge_source_utf8_offsets PROPERTIES TIMEOUT 15)
  # Host-policy substrate only; guest syscall interception remains unimplemented.
  add_executable(oresforge_scoped_policy_tests
    contracts/v1/scoped_permission_policy_test.cpp)
  target_include_directories(oresforge_scoped_policy_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1")
  target_compile_features(oresforge_scoped_policy_tests PRIVATE cxx_std_17)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_scoped_policy_tests PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME scoped_permission_host_policy_substrate
    COMMAND oresforge_scoped_policy_tests)
  set_tests_properties(scoped_permission_host_policy_substrate PROPERTIES TIMEOUT 15)
  add_executable(oresforge_pinned_tcp_tests
    contracts/v1/scoped_tcp_boundary_test.cpp)
  target_include_directories(oresforge_pinned_tcp_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1")
  target_compile_features(oresforge_pinned_tcp_tests PRIVATE cxx_std_17)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_pinned_tcp_tests PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME native_pinned_tcp_host_boundary COMMAND oresforge_pinned_tcp_tests)
  set_tests_properties(native_pinned_tcp_host_boundary PROPERTIES TIMEOUT 15)

  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1/annotation-admission-v1.tsv"
    "${CMAKE_CURRENT_BINARY_DIR}/annotation-admission-v1.tsv" COPYONLY)
  # Keep real source frontend fixtures in CTest's build working directory.
  # These are identical Atto Git blobs to the Java/LLVM/Wasm/.NET contract.
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/http-actor-v1")
  foreach(atto_http_fixture serial-1x1.atto explicit-1x1.atto unsupported-3x4.atto)
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/fixtures/http-actor-v1/${atto_http_fixture}"
      "${CMAKE_CURRENT_BINARY_DIR}/http-actor-v1/${atto_http_fixture}"
      COPYONLY)
  endforeach()
  add_executable(oresforge_tests verification/tests.cpp)
  add_dependencies(oresforge_tests oresforge_numeric_i64_cases oresforge_numeric_overflow_cases)
  target_include_directories(oresforge_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(oresforge_tests PRIVATE oresforge_core)
  add_test(NAME oresforge_unit COMMAND oresforge_tests)
  # Execute the real stage-0 parser/IR rejection and positive-control checks.
  add_executable(oresforge_match_tilde_failclosed
    verification/atto_match_tilde_failclosed.cpp)
  target_link_libraries(oresforge_match_tilde_failclosed PRIVATE oresforge_core)
  add_test(NAME oresforge_match_tilde_failclosed
    COMMAND oresforge_match_tilde_failclosed)
  # This invokes the real Forge frontend on operator-positive and
  # operator-negative sources, including unreachable declarations.
  add_executable(oresforge_legacy_operator_grammar
    verification/legacy_operator_grammar.cpp)
  target_link_libraries(oresforge_legacy_operator_grammar PRIVATE oresforge_core)
  add_test(NAME oresforge_legacy_operator_grammar
    COMMAND oresforge_legacy_operator_grammar)
  set_tests_properties(oresforge_legacy_operator_grammar PROPERTIES TIMEOUT 15)
  # Unicode source-preflight regression, including visible parser admission.
  add_executable(oresforge_source_unicode_control verification/source_unicode_control_test.cpp)
  target_link_libraries(oresforge_source_unicode_control PRIVATE oresforge_core)
  add_test(NAME oresforge_source_unicode_control COMMAND oresforge_source_unicode_control)
  set_tests_properties(oresforge_source_unicode_control PROPERTIES TIMEOUT 15)
  # Stage-0 single-file AttoLang path admission. Imports remain fail-closed in Forge.
  add_test(NAME oresforge_cli_atto_guest
    COMMAND oresforge run "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.atto")
  set_tests_properties(oresforge_cli_atto_guest PROPERTIES
    PASS_REGULAR_EXPRESSION "^14\n?$" TIMEOUT 10)
  # Real compiler check mode: no program result is published on success,
  # and parser, unsupported semantics, and checked-i64 failures stay closed.
  add_test(NAME oresforge_cli_scalar_check
    COMMAND "${CMAKE_COMMAND}"
      "-DCOMPILER=$<TARGET_FILE:oresforge>"
      "-DVALID=${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.atto"
      "-DOVERFLOW=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scalar-check/overflow.atto"
      "-DUNSUPPORTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scalar-check/unsupported.atto"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/verification/scalar_check_cli.cmake")
  set_tests_properties(oresforge_cli_scalar_check PROPERTIES TIMEOUT 45)
  add_test(NAME oresforge_cli_legacy_ores_guest
    COMMAND oresforge run "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.ores")
  set_tests_properties(oresforge_cli_legacy_ores_guest PROPERTIES
    PASS_REGULAR_EXPRESSION "^14\n?$" TIMEOUT 10)
  add_executable(oresforge_generator_frame_v1 verification/generator_frame_v1_test.cpp)
  target_link_libraries(oresforge_generator_frame_v1 PRIVATE oresforge_core)
  target_compile_features(oresforge_generator_frame_v1 PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_generator_frame_v1 PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME oresforge_generator_frame_v1 COMMAND oresforge_generator_frame_v1)
  set_tests_properties(oresforge_generator_frame_v1 PROPERTIES TIMEOUT 20)
  add_executable(oresforge_state_region_provenance
    verification/state_region_provenance.cpp)
  target_include_directories(oresforge_state_region_provenance PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(oresforge_state_region_provenance PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_state_region_provenance PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME oresforge_state_region_provenance_v1
    COMMAND oresforge_state_region_provenance)
  set_tests_properties(oresforge_state_region_provenance_v1 PROPERTIES TIMEOUT 15)
  # Host-only positive filesystem resolution substrate, not guest import linking.
  add_executable(oresforge_root_resolver_test verification/project_root_resolver_test.cpp)
  target_include_directories(oresforge_root_resolver_test PRIVATE .)
  target_compile_features(oresforge_root_resolver_test PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_root_resolver_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME oresforge_project_root_resolver_host_only COMMAND oresforge_root_resolver_test)
  set_tests_properties(oresforge_project_root_resolver_host_only PROPERTIES TIMEOUT 20)
  # Already present in main via PR #34; keep these registrations exactly once.
  # This is a trusted-local mailbox policy regression, not a process sandbox
  # or compiled Oreslang tenant execution test.
  add_executable(oresforge_tenant_isolation_policy verification/tenant_isolation_policy.cpp)
  target_link_libraries(oresforge_tenant_isolation_policy PRIVATE oresforge_core)
  add_test(NAME oresforge_tenant_isolation_policy COMMAND oresforge_tenant_isolation_policy)
  set_tests_properties(oresforge_tenant_isolation_policy PROPERTIES TIMEOUT 15)
  add_executable(oresforge_mailbox_destructor verification/actor_mailbox_destructor.cpp)
  target_link_libraries(oresforge_mailbox_destructor PRIVATE oresforge_core)
  add_test(NAME oresforge_actor_mailbox_destructor COMMAND oresforge_mailbox_destructor)
  add_executable(oresforge_mailbox_limits verification/actor_mailbox_limits.cpp)
  target_link_libraries(oresforge_mailbox_limits PRIVATE oresforge_core)
  add_test(NAME oresforge_actor_mailbox_limits COMMAND oresforge_mailbox_limits)
  add_executable(oresforge_mailbox_auth_precedence verification/actor_mailbox_authority_precedence.cpp)
  target_link_libraries(oresforge_mailbox_auth_precedence PRIVATE oresforge_core)
  add_test(NAME oresforge_actor_mailbox_auth_precedence COMMAND oresforge_mailbox_auth_precedence)
  # Exercise the real mailbox with concurrent sends, deep-copy ownership,
  # backpressure, byte-credit recovery and generation/authority fencing.
  find_package(Threads REQUIRED)
  add_executable(oresforge_mailbox_stress verification/actor_mailbox_concurrency.cpp)
  target_link_libraries(oresforge_mailbox_stress PRIVATE oresforge_core Threads::Threads)
  add_test(NAME oresforge_actor_mailbox_concurrency COMMAND oresforge_mailbox_stress)
  set_tests_properties(oresforge_actor_mailbox_concurrency PROPERTIES TIMEOUT 60)
  add_executable(oresforge_mailbox_mpmc verification/actor_mailbox_mpmc.cpp)
  target_link_libraries(oresforge_mailbox_mpmc PRIVATE oresforge_core Threads::Threads)
  add_test(NAME oresforge_actor_mailbox_mpmc COMMAND oresforge_mailbox_mpmc)
  set_tests_properties(oresforge_actor_mailbox_mpmc PROPERTIES TIMEOUT 60)
  set_tests_properties(oresforge_actor_mailbox_limits PROPERTIES TIMEOUT 20)
  # Independent native heap and logical frame stack per authenticated
  # mailbox incarnation. This is real C++ host execution, not .atto lowering.
  add_executable(oresforge_actor_incarnation_memory_v1
    verification/actor_incarnation_memory_v1_test.cpp)
  target_link_libraries(oresforge_actor_incarnation_memory_v1
    PRIVATE oresforge_core Threads::Threads)
  target_compile_features(oresforge_actor_incarnation_memory_v1 PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_actor_incarnation_memory_v1 PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME oresforge_actor_incarnation_memory_v1_host
    COMMAND oresforge_actor_incarnation_memory_v1)
  set_tests_properties(oresforge_actor_incarnation_memory_v1_host
    PROPERTIES TIMEOUT 30)
  add_executable(oresforge_mailbox_model verification/actor_mailbox_model.cpp)
  target_link_libraries(oresforge_mailbox_model PRIVATE oresforge_core)
  add_test(NAME oresforge_actor_mailbox_model COMMAND oresforge_mailbox_model)
  set_tests_properties(oresforge_actor_mailbox_model PROPERTIES TIMEOUT 30)
  add_executable(oresforge_actor_arena_tests verification/actor_arena_test.cpp)
  target_link_libraries(oresforge_actor_arena_tests PRIVATE oresforge_core)
  add_test(NAME oresforge_actor_arena COMMAND oresforge_actor_arena_tests)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME oresforge_cli_source_budget
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/cli_source_budget.py"
      "$<TARGET_FILE:oresforge>")
  set_tests_properties(oresforge_cli_source_budget PROPERTIES TIMEOUT 20)
  # Explicit bootstrap denial of ETS/pg before scalar interpreter/JIT/IR
  # publication. These are compiler negative tests, not guest feature proof.
  add_test(NAME oresforge_ets_pg_fail_closed_source
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/ets_pg_source_admission.py"
      "$<TARGET_FILE:oresforge>")
  set_tests_properties(oresforge_ets_pg_fail_closed_source PROPERTIES TIMEOUT 30)
  # Real compiler reject test: comma :: / typed inline => Type -> lambdas
  # are still unsupported in Forge stage-0; no false parity receipt.
  add_test(NAME oresforge_comma_lambda_fail_closed
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/comma_lambda_source_admission.py"
      "$<TARGET_FILE:oresforge>")
  set_tests_properties(oresforge_comma_lambda_fail_closed PROPERTIES TIMEOUT 90)
  # Source to IR interpreter (not machine code or native AOT parity).
  add_test(NAME oresforge_source_i64_interpreter
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/source_i64_executable.py"
      --compiler "$<TARGET_FILE:oresforge>")
  set_tests_properties(oresforge_source_i64_interpreter PROPERTIES TIMEOUT 100)
endif()
# Static-facts v0: only literal main admitted, with actual Forge verified IR.
if(BUILD_TESTING)
  add_test(NAME oresforge_static_facts_v0_negative
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/static_facts_v0.py" --self-test)
  add_test(NAME oresforge_static_facts_v0_real_compiler
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/static_facts_v0.py"
      --compiler "$<TARGET_FILE:oresforge>"
      --source "${CMAKE_CURRENT_SOURCE_DIR}/contracts/scalar/v1/fixtures/literal-42.ores")
  set_tests_properties(oresforge_static_facts_v0_negative oresforge_static_facts_v0_real_compiler
    PROPERTIES TIMEOUT 30)
endif()
if(ORESFORGE_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "libFuzzer requires Clang")
  endif()
  # Instrument the actual compiler implementation, not only the harness.
  # -fsanitize=fuzzer-no-link keeps the static library linkable with libFuzzer.
  target_compile_options(oresforge_core PRIVATE
    -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
  add_executable(oresforge_fuzz verification/fuzz_frontend.cpp)
  target_link_libraries(oresforge_fuzz PRIVATE oresforge_core)
  target_compile_options(oresforge_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(oresforge_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  # Stateful differential fuzzing against the actual actor mailbox implementation.
  add_executable(oresforge_mailbox_fuzz verification/fuzz_actor_mailbox.cpp)
  target_link_libraries(oresforge_mailbox_fuzz PRIVATE oresforge_core)
  target_compile_options(oresforge_mailbox_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(oresforge_mailbox_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

# Admission gates for language surfaces that this backend does not lower yet.
if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME value_boundary_admission
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verification/value_boundaries.py"
      "$<TARGET_FILE:oresforge>")
  set_tests_properties(value_boundary_admission PROPERTIES TIMEOUT 60)
  # Source admission only: reject unsupported Option/Result/reactive guest forms.
  add_test(NAME elm_composition_source_admission
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verification/elm_composition_admission.py"
      "$<TARGET_FILE:oresforge>")
  set_tests_properties(elm_composition_source_admission PROPERTIES TIMEOUT 90)
  add_test(NAME numeric_reference_and_capability_admission
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
      -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p "test_numeric_*.py" -v)
  set_tests_properties(numeric_reference_and_capability_admission PROPERTIES TIMEOUT 30)
  add_test(NAME refinement_admission
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verification/refinement_admission_gate.py"
      --compiler "$<TARGET_FILE:oresforge>")
  set_tests_properties(refinement_admission PROPERTIES TIMEOUT 60)
  # Shared Java-reference scalar source fixtures: executed guest values + negative admission.
  add_test(NAME source_scalar_parity
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/contracts/scalar/v1/run.py"
      --backend forge --mode stdout-i64 --command "$<TARGET_FILE:oresforge> run {source}")
  set_tests_properties(source_scalar_parity PROPERTIES TIMEOUT 240)
  # Bootstrap Forge must reject all source imports until a verified module
  # graph exists; no successful scalar IR may be published from such source.
  add_test(NAME source_import_parity_admission
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/verification/import_parity_admission.py"
      "$<TARGET_FILE:oresforge>")
  set_tests_properties(source_import_parity_admission PROPERTIES TIMEOUT 60)
  add_test(NAME scalar_parity_harness_selftest
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/contracts/scalar/v1/selftest.py")
  set_tests_properties(scalar_parity_harness_selftest PROPERTIES TIMEOUT 30)

endif()

# A real host assembler/linker executes the emitted entry and compares its
# printed i64 value with the checked source interpreter. This is a *stage-0*
# native smoke only: it does not execute an Oreslang-written compiler.
if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|aarch64|arm64)$")
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME oresforge_native_asm_executable
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/native_asm_executable.py"
      --compiler "$<TARGET_FILE:oresforge>")
  set_tests_properties(oresforge_native_asm_executable PROPERTIES TIMEOUT 180)
  add_test(NAME oresforge_runtime_argument_native_executable
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/runtime_native_executable.py"
      --compiler "$<TARGET_FILE:oresforge>")
  set_tests_properties(oresforge_runtime_argument_native_executable PROPERTIES TIMEOUT 180)
  # New opt-in callable language: real x86-64 interfunction calls, lazy if,
  # direct/mutual recursion and bounded native stack-depth traps.
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    add_test(NAME oresforge_real_native_recursive_calls
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/verification/native_call_executable.py"
        --compiler "$<TARGET_FILE:oresforge>")
    set_tests_properties(oresforge_real_native_recursive_calls PROPERTIES TIMEOUT 180)
  endif()
endif()

# HOST-ONLY AI dispatch fencing parity v1; NOT an Oreslang guest / durable store.
if(BUILD_TESTING)
  add_executable(ores_agent_host_dispatch_parity_v1
    contracts/v1/agent-dispatch/agent_host_dispatch_test.cpp)
  target_include_directories(ores_agent_host_dispatch_parity_v1 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1/agent-dispatch")
  target_compile_features(ores_agent_host_dispatch_parity_v1 PRIVATE cxx_std_17)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ores_agent_host_dispatch_parity_v1 PRIVATE
      -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  endif()
  add_test(NAME agent_model_dispatch_host_fencing_v1
    COMMAND ores_agent_host_dispatch_parity_v1)
  set_tests_properties(agent_model_dispatch_host_fencing_v1 PROPERTIES TIMEOUT 15)
endif()

# Host-only tenant isolate admission. Executable native guest sandboxing is NOT proven.
if(BUILD_TESTING)
  add_executable(oresforge_isolate_host_test verification/isolate_host_test.cpp)
  target_include_directories(oresforge_isolate_host_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(oresforge_isolate_host_test PRIVATE cxx_std_20)
  find_package(Threads REQUIRED)
  target_link_libraries(oresforge_isolate_host_test PRIVATE Threads::Threads)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_isolate_host_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  endif()
  add_test(NAME oresforge_tenant_isolate_host_admission COMMAND oresforge_isolate_host_test)
  set_tests_properties(oresforge_tenant_isolate_host_admission PROPERTIES TIMEOUT 30)
endif()

# First verified native Forge IsolateVM bridge: compile .ores to actual
# runtime-argument x86-64 assembly, link with an independent C++ host driver,
# run two tenant states against the same guest machine code.
if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
  add_test(NAME oresforge_compiled_tenant_isolate_native
    COMMAND "${CMAKE_COMMAND}"
      "-DORESFORGE_COMPILER=$<TARGET_FILE:oresforge>"
      "-DORESFORGE_CXX=${CMAKE_CXX_COMPILER}"
      "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}"
      "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/tenant_native_entry"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/verification/tenant_native_entry.cmake")
  set_tests_properties(oresforge_compiled_tenant_isolate_native PROPERTIES TIMEOUT 90)
endif()

# HOST-ONLY Option<T> recursive flatten substrate. Source-language admission,
# Future<T> scheduler integration, and guest execution remain independent gates.
if(BUILD_TESTING)
  add_executable(oresforge_wrapper_flatten_option_host verification/wrapper_flatten_option_host_test.cpp)
  target_include_directories(oresforge_wrapper_flatten_option_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
  # std::optional: do not rely on the compiler default standard (AppleClang < C++17).
  target_compile_features(oresforge_wrapper_flatten_option_host PRIVATE cxx_std_17)
  target_compile_options(oresforge_wrapper_flatten_option_host PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>)
  add_test(NAME forge_wrapper_option_host_conformance COMMAND oresforge_wrapper_flatten_option_host)
  set_tests_properties(forge_wrapper_option_host_conformance PROPERTIES TIMEOUT 20)
endif()

# Linux x86-64 native actor stack/heap substrate. HOST CALLBACKS ONLY:
# this is not `define actor` guest compilation or a tenant sandbox.
if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
  add_executable(oresforge_actor_stack_domain
    verification/actor_stack_domain_test.cpp)
  target_include_directories(oresforge_actor_stack_domain PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(oresforge_actor_stack_domain PRIVATE cxx_std_20)
  find_package(Threads REQUIRED)
  target_link_libraries(oresforge_actor_stack_domain PRIVATE Threads::Threads)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_actor_stack_domain PRIVATE
      -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  endif()
  add_test(NAME forge_actor_stack_domain_host_v1 COMMAND oresforge_actor_stack_domain)
  set_tests_properties(forge_actor_stack_domain_host_v1 PROPERTIES
    TIMEOUT 30 LABELS "host-only;native-actor-stack")
endif()


# Source admission only: no claim of compiled AttoLang guest execution.
if(BUILD_TESTING)
  add_executable(oresforge_source_bidi_admission verification/source_bidi_admission_test.cpp)
  target_include_directories(oresforge_source_bidi_admission PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(oresforge_source_bidi_admission PRIVATE cxx_std_20)
  add_test(NAME forge_source_bidi_admission_host COMMAND oresforge_source_bidi_admission)
  set_tests_properties(forge_source_bidi_admission_host PROPERTIES TIMEOUT 20)
endif()

# ---- atto-lang additions (kept in the ores-canonical reconciliation) ----
option(ATTO_FORGE_SANITIZERS "Use ASan and UBSan for native tests" ON)

add_library(atto_forge_checked_i64 INTERFACE)
target_include_directories(atto_forge_checked_i64 INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(atto_forge_checked_i64 INTERFACE cxx_std_20)

add_executable(checked_i64_test tests/checked_i64.cpp)
target_link_libraries(checked_i64_test PRIVATE atto_forge_checked_i64)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(checked_i64_test PRIVATE -Wall -Wextra -Werror -pedantic)
  if(ATTO_FORGE_SANITIZERS)
    target_compile_options(checked_i64_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(checked_i64_test PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME checked_i64_test COMMAND checked_i64_test)

# Host-only ownership contract model: not a Forge guest compiler.
# Run sanitizer-backed native lifetime and concurrent weak-reference tests.
find_package(Threads REQUIRED)
add_executable(ownership_v1_test tests/ownership_v1.cpp)
target_link_libraries(ownership_v1_test PRIVATE atto_forge_checked_i64 Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # Ensure assertions remain enabled even under -DCMAKE_BUILD_TYPE=Release.
  target_compile_options(ownership_v1_test PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
  if(ATTO_FORGE_SANITIZERS)
    target_compile_options(ownership_v1_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ownership_v1_test PRIVATE -fsanitize=address,undefined)
  endif()
endif()
if(MSVC)
  target_compile_options(ownership_v1_test PRIVATE /UNDEBUG)
endif()
add_test(NAME ownership_v1_test COMMAND ownership_v1_test)
# Deterministic native stress matrix: malformed graphs, exact budgets,
# concurrent region retirement and immovable in-place pin construction.
add_executable(ownership_stress_v1_test tests/ownership_stress_v1.cpp)
target_link_libraries(ownership_stress_v1_test PRIVATE atto_forge_checked_i64 Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ownership_stress_v1_test PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
  if(ATTO_FORGE_SANITIZERS)
    target_compile_options(ownership_stress_v1_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ownership_stress_v1_test PRIVATE -fsanitize=address,undefined)
  endif()
elseif(MSVC)
  target_compile_options(ownership_stress_v1_test PRIVATE /UNDEBUG)
endif()
add_test(NAME ownership_stress_v1_test COMMAND ownership_stress_v1_test)


# Native bounded literal reader; host-only scalar primitive.
add_executable(decimal_i64_test tests/decimal_i64.cpp)
target_link_libraries(decimal_i64_test PRIVATE atto_forge_checked_i64)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(decimal_i64_test PRIVATE -Wall -Wextra -Werror -pedantic)
  if(ATTO_FORGE_SANITIZERS)
    target_compile_options(decimal_i64_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(decimal_i64_test PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME decimal_i64_test COMMAND decimal_i64_test)

# Restricted native lexer has no Python or borrowed source buffers.
add_executable(scalar_lexer_test tests/scalar_lexer.cpp)
target_link_libraries(scalar_lexer_test PRIVATE atto_forge_checked_i64)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(scalar_lexer_test PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
  if(ATTO_FORGE_SANITIZERS)
    target_compile_options(scalar_lexer_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(scalar_lexer_test PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME scalar_lexer_test COMMAND scalar_lexer_test)

# Strict, bounded native scalar AST parser; no guest execution claim.
add_executable(scalar_parser_test tests/scalar_parser.cpp)
target_link_libraries(scalar_parser_test PRIVATE atto_forge_checked_i64)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(scalar_parser_test PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
  if(ATTO_FORGE_SANITIZERS)
    target_compile_options(scalar_parser_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(scalar_parser_test PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME scalar_parser_test COMMAND scalar_parser_test)

# Checked native scalar AST evaluator: no guest execution claim.
add_executable(scalar_eval_test tests/scalar_eval.cpp)
target_link_libraries(scalar_eval_test PRIVATE atto_forge_checked_i64)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(scalar_eval_test PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
  if(ATTO_FORGE_SANITIZERS)
    target_compile_options(scalar_eval_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(scalar_eval_test PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME scalar_eval_test COMMAND scalar_eval_test)

# Deterministic C++ emission from the bounded scalar AST.
add_executable(scalar_emit_test tests/scalar_emit.cpp)
target_link_libraries(scalar_emit_test PRIVATE atto_forge_checked_i64)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(scalar_emit_test PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
  if(ATTO_FORGE_SANITIZERS)
    target_compile_options(scalar_emit_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(scalar_emit_test PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME scalar_emit_test COMMAND scalar_emit_test)

# Real compiled restricted scalar guests; no Python bootstrap.
add_executable(forge_scalar tools/forge_scalar.cpp)
target_link_libraries(forge_scalar PRIVATE atto_forge_checked_i64)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(forge_scalar PRIVATE -Wall -Wextra -Werror -pedantic)
  if(ATTO_FORGE_SANITIZERS)
    target_compile_options(forge_scalar PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(forge_scalar PRIVATE -fsanitize=address,undefined)
  endif()
endif()
add_test(NAME scalar_guest_pipeline
  COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DCXX=${CMAKE_CXX_COMPILER}"
    "-DSANITIZERS=${ATTO_FORGE_SANITIZERS}"
    "-DFORGE=$<TARGET_FILE:forge_scalar>"
    "-DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scalar-guests"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/scalar_guest.cmake")

# Executed generated-native-guest conformance corpus under GCC and Clang.
add_test(NAME scalar_guest_conformance
  COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DCXX=${CMAKE_CXX_COMPILER}"
    "-DSANITIZERS=${ATTO_FORGE_SANITIZERS}"
    "-DFORGE=$<TARGET_FILE:forge_scalar>"
    "-DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scalar-conformance"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/scalar_conformance.cmake")

# Failed compiler inputs yield typed bounded diagnostics, no echoed source.
add_test(NAME scalar_cli_diagnostics
  COMMAND "${CMAKE_COMMAND}"
    "-DFORGE=$<TARGET_FILE:forge_scalar>"
    "-DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scalar-diagnostics"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/scalar_cli_diagnostics.cmake")

# Compiler builtins independently attest checked-i64 primitives at boundary
# and 100,000 deterministic high-entropy signed operand pairs.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_executable(checked_i64_oracle_test tests/checked_i64_oracle.cpp)
  target_link_libraries(checked_i64_oracle_test PRIVATE atto_forge_checked_i64)
  target_compile_options(checked_i64_oracle_test PRIVATE -Wall -Wextra -Werror -pedantic)
  if(ATTO_FORGE_SANITIZERS)
    target_compile_options(checked_i64_oracle_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(checked_i64_oracle_test PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME checked_i64_oracle_test COMMAND checked_i64_oracle_test)
endif()

# Deterministic native emission verified with 48 compiled executable guests.
add_test(NAME scalar_guest_reproducibility
  COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DCXX=${CMAKE_CXX_COMPILER}"
    "-DSANITIZERS=${ATTO_FORGE_SANITIZERS}"
    "-DFORGE=$<TARGET_FILE:forge_scalar>"
    "-DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scalar-reproducibility"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/scalar_reproducibility.cmake")

# Parse-only admission without output side effects.
add_test(NAME scalar_check_only
  COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DFORGE=$<TARGET_FILE:forge_scalar>"
    "-DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/scalar-check"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/scalar_check.cmake")
