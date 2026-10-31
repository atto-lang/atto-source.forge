cmake_minimum_required(VERSION 3.20)
project(OresForge VERSION 0.1.0 LANGUAGES CXX)
option(ORESFORGE_FUZZ "Build Clang libFuzzer target" OFF)
add_library(oresforge_core STATIC compiler/frontend.cpp compiler/ir.cpp compiler/call_native.cpp compiler/singleton_stage0.cpp codegen/native.cpp runtime/actor.cpp jit/tiered.cpp)
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

  find_package(Threads REQUIRED)
  add_executable(oresforge_singleton_shared_runtime verification/singleton_shared_runtime.cpp)
  target_include_directories(oresforge_singleton_shared_runtime PRIVATE .)
  target_compile_features(oresforge_singleton_shared_runtime PRIVATE cxx_std_20)
  target_link_libraries(oresforge_singleton_shared_runtime PRIVATE Threads::Threads oresforge_core)
  add_test(NAME oresforge_singleton_shared_runtime COMMAND oresforge_singleton_shared_runtime)
  set_tests_properties(oresforge_singleton_shared_runtime PROPERTIES TIMEOUT 30)

  add_executable(oresforge_singleton_admission verification/singleton_admission.cpp)
  target_link_libraries(oresforge_singleton_admission PRIVATE oresforge_core)
  add_test(NAME oresforge_singleton_admission COMMAND oresforge_singleton_admission)
  set_tests_properties(oresforge_singleton_admission PROPERTIES TIMEOUT 15)

  # Compiler-backed stage-0 boundary: real singleton declaration source is
  # resolved to compiler-owned identity and drives the bounded host registry.
  # The normal frontend remains fail-closed; this is not full guest parity.
  add_executable(oresforge_singleton_guest_stage0 verification/singleton_guest_stage0.cpp)
  target_link_libraries(oresforge_singleton_guest_stage0 PRIVATE oresforge_core Threads::Threads)
  add_test(NAME oresforge_singleton_guest_stage0 COMMAND oresforge_singleton_guest_stage0)
  set_tests_properties(oresforge_singleton_guest_stage0 PROPERTIES TIMEOUT 30)

  add_executable(oresforge_tests verification/tests.cpp)
  add_dependencies(oresforge_tests oresforge_numeric_i64_cases oresforge_numeric_overflow_cases)
  target_include_directories(oresforge_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(oresforge_tests PRIVATE oresforge_core)
  add_test(NAME oresforge_unit COMMAND oresforge_tests)
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


