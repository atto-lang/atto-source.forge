cmake_minimum_required(VERSION 3.20)
project(OresForge VERSION 0.1.0 LANGUAGES CXX)
option(ORESFORGE_FUZZ "Build Clang libFuzzer target" OFF)
add_library(oresforge_core STATIC compiler/frontend.cpp compiler/ir.cpp codegen/native.cpp runtime/actor.cpp runtime/complex128.cpp jit/tiered.cpp)
target_include_directories(oresforge_core PUBLIC .)
target_compile_features(oresforge_core PUBLIC cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(oresforge_core PRIVATE -Wall -Wextra -Wconversion)
endif()
add_executable(oresforge compiler/main.cpp)
target_link_libraries(oresforge PRIVATE oresforge_core)
include(CTest)
if(BUILD_TESTING)
  add_executable(oresforge_tests verification/tests.cpp)
  target_link_libraries(oresforge_tests PRIVATE oresforge_core)
  add_test(NAME oresforge_unit COMMAND oresforge_tests)
  find_package(Threads REQUIRED)
  # Actor-local persistent/scratch arena host proof; not guest lowering.
  add_executable(oresforge_actor_arena_tests verification/actor_arena_test.cpp)
  target_link_libraries(oresforge_actor_arena_tests PRIVATE oresforge_core Threads::Threads)
  add_test(NAME oresforge_actor_arena COMMAND oresforge_actor_arena_tests)
  set_tests_properties(oresforge_actor_arena PROPERTIES TIMEOUT 25)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  # Host-only policy conformance (not source-compiled actor execution).
  add_executable(oresforge_actor_pipeline_policy_test
    verification/actor_pipeline_policy_test.cpp)
  target_include_directories(oresforge_actor_pipeline_policy_test PRIVATE .)
  target_compile_features(oresforge_actor_pipeline_policy_test PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_actor_pipeline_policy_test PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_executable(oresforge_actor_pipeline_concurrency_test
    verification/actor_pipeline_concurrency_test.cpp)
  target_include_directories(oresforge_actor_pipeline_concurrency_test PRIVATE .)
  target_compile_features(oresforge_actor_pipeline_concurrency_test PRIVATE cxx_std_20)
  target_link_libraries(oresforge_actor_pipeline_concurrency_test PRIVATE Threads::Threads)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_actor_pipeline_concurrency_test PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME actor_pipeline_v1_concurrent_selection_and_retirement
    COMMAND oresforge_actor_pipeline_concurrency_test)
  set_tests_properties(actor_pipeline_v1_concurrent_selection_and_retirement
    PROPERTIES TIMEOUT 25)

  add_test(NAME actor_pipeline_v1_host_policy
    COMMAND oresforge_actor_pipeline_policy_test)
  set_tests_properties(actor_pipeline_v1_host_policy PROPERTIES TIMEOUT 15)
  # Exact host-policy proof: allocation failure must not consume RR selection.
  add_executable(oresforge_actor_pipeline_rr_allocation_test
    verification/actor_pipeline_rr_allocation_test.cpp)
  target_include_directories(oresforge_actor_pipeline_rr_allocation_test PRIVATE .)
  target_compile_features(oresforge_actor_pipeline_rr_allocation_test PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_actor_pipeline_rr_allocation_test PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME actor_pipeline_v1_rr_allocation_atomic
    COMMAND oresforge_actor_pipeline_rr_allocation_test)
  set_tests_properties(actor_pipeline_v1_rr_allocation_atomic PROPERTIES TIMEOUT 15)

  add_test(NAME actor_pipeline_v1_golden_fixture
    COMMAND "${Python3_EXECUTABLE}" 
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/actor_pipeline_fixture.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/contracts/v1/actor-pipeline-routing-v1.json")
  set_tests_properties(actor_pipeline_v1_golden_fixture PROPERTIES TIMEOUT 15)


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
endif()
