cmake_minimum_required(VERSION 3.20)
project(OresForge VERSION 0.1.0 LANGUAGES CXX)
option(ORESFORGE_FUZZ "Build Clang libFuzzer target" OFF)
add_library(oresforge_core STATIC compiler/frontend.cpp compiler/ir.cpp compiler/call_native.cpp codegen/native.cpp runtime/actor.cpp jit/tiered.cpp)
target_include_directories(oresforge_core PUBLIC .)
target_compile_features(oresforge_core PUBLIC cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(oresforge_core PRIVATE -Wall -Wextra -Wconversion)
endif()
add_executable(oresforge compiler/main.cpp)
target_link_libraries(oresforge PRIVATE oresforge_core)
include(CTest)
if(BUILD_TESTING)
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

  add_executable(oresforge_tests verification/tests.cpp)
  add_dependencies(oresforge_tests oresforge_numeric_i64_cases oresforge_numeric_overflow_cases)
  target_include_directories(oresforge_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(oresforge_tests PRIVATE oresforge_core)
  add_test(NAME oresforge_unit COMMAND oresforge_tests)
  # This invokes the real Forge frontend on operator-positive and
  # operator-negative sources, including unreachable declarations.
  add_executable(oresforge_legacy_operator_grammar
    verification/legacy_operator_grammar.cpp)
  target_link_libraries(oresforge_legacy_operator_grammar PRIVATE oresforge_core)
  add_test(NAME oresforge_legacy_operator_grammar
    COMMAND oresforge_legacy_operator_grammar)
  set_tests_properties(oresforge_legacy_operator_grammar PROPERTIES TIMEOUT 15)
  # Stage-0 single-file AttoLang path admission. Imports remain fail-closed in Forge.
  add_test(NAME oresforge_cli_atto_guest
    COMMAND oresforge run "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.atto")
  set_tests_properties(oresforge_cli_atto_guest PROPERTIES
    PASS_REGULAR_EXPRESSION "^14\n?$" TIMEOUT 10)
  add_test(NAME oresforge_cli_legacy_ores_guest
    COMMAND oresforge run "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.ores")
  set_tests_properties(oresforge_cli_legacy_ores_guest PROPERTIES
    PASS_REGULAR_EXPRESSION "^14\n?$" TIMEOUT 10)
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
  target_compile_options(oresforge_wrapper_flatten_option_host PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>)
  add_test(NAME forge_wrapper_option_host_conformance COMMAND oresforge_wrapper_flatten_option_host)
  set_tests_properties(forge_wrapper_option_host_conformance PROPERTIES TIMEOUT 20)
endif()
