cmake_minimum_required(VERSION 3.20)
project(OresForge VERSION 0.1.0 LANGUAGES C CXX)
option(ORESFORGE_FUZZ "Build Clang libFuzzer target" OFF)
add_library(oresforge_core STATIC compiler/frontend.cpp compiler/ir.cpp compiler/call_native.cpp compiler/actor_source.cpp codegen/native.cpp runtime/actor.cpp jit/tiered.cpp)
target_include_directories(oresforge_core PUBLIC .)
target_compile_features(oresforge_core PUBLIC cxx_std_20)
# Native bounded actor storage shared with the LLVM implementation. This is a
# host API; guest actor syntax and ownership lowering remain unsupported.
add_library(oresforge_actor_arena STATIC runtime/arena/ActorArena.c)
target_include_directories(oresforge_actor_arena PUBLIC runtime/arena)
target_compile_features(oresforge_actor_arena PRIVATE c_std_11)
find_package(Threads REQUIRED)
add_library(oresforge_scalar_actor STATIC runtime/scalar_actor.cpp runtime/arena/ActorOwnedI64Heap.c)
target_include_directories(oresforge_scalar_actor PUBLIC . runtime/arena)
target_compile_features(oresforge_scalar_actor PRIVATE c_std_11 PUBLIC cxx_std_20)
target_link_libraries(oresforge_scalar_actor PUBLIC Threads::Threads)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(oresforge_actor_arena PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
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
  add_executable(oresforge_actor_arena_tests verification/actor_arena.cpp)
  target_link_libraries(oresforge_actor_arena_tests PRIVATE oresforge_actor_arena Threads::Threads)
  target_compile_features(oresforge_actor_arena_tests PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_actor_arena_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(oresforge_actor_arena_tests PRIVATE ORES_ARENA_TEST_ALLOCATIONS=1)
    target_link_options(oresforge_actor_arena_tests PRIVATE
      -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free)
  endif()
  add_test(NAME oresforge_actor_arena_persistent_scratch COMMAND oresforge_actor_arena_tests)
  set_tests_properties(oresforge_actor_arena_persistent_scratch PROPERTIES TIMEOUT 25)
  # Actor-private logical frame storage: native host tests only. These do
  # not certify per-actor switched machine stacks or compiled .atto actors.
  add_executable(oresforge_actor_frames_tests verification/actor_frames.cpp)
  target_link_libraries(oresforge_actor_frames_tests PRIVATE Threads::Threads)
  target_include_directories(oresforge_actor_frames_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(oresforge_actor_frames_tests PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_actor_frames_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  endif()
  add_test(NAME oresforge_actor_private_frames_host COMMAND oresforge_actor_frames_tests)
  set_tests_properties(oresforge_actor_private_frames_host PROPERTIES TIMEOUT 30)

  # Sealed, stackless typed actor frame VM and negative bytecode admission.
  # Host VM foundation only: not yet compiler-lowered native Forge actors.
  add_executable(oresforge_actor_frame_vm_tests verification/actor_frame_vm.cpp)
  target_link_libraries(oresforge_actor_frame_vm_tests PRIVATE oresforge_scalar_actor Threads::Threads)
  target_compile_features(oresforge_actor_frame_vm_tests PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_actor_frame_vm_tests PRIVATE
      -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  endif()
  add_test(NAME oresforge_actor_frame_vm_host COMMAND oresforge_actor_frame_vm_tests)
  set_tests_properties(oresforge_actor_frame_vm_host PROPERTIES TIMEOUT 40)

  # In-process scalar actor compiler admission; much more comprehensive than
  # merely requiring a nonzero process exit on one malformed input.
  add_executable(oresforge_actor_source_admission_tests
    verification/actor_source_admission.cpp)
  target_link_libraries(oresforge_actor_source_admission_tests PRIVATE oresforge_core)
  target_compile_features(oresforge_actor_source_admission_tests PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_actor_source_admission_tests PRIVATE
      -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  endif()
  add_test(NAME oresforge_actor_source_admission
    COMMAND oresforge_actor_source_admission_tests)
  set_tests_properties(oresforge_actor_source_admission PROPERTIES TIMEOUT 30)

  add_executable(oresforge_scalar_actor_frames_tests verification/scalar_actor_frames.cpp)
  target_link_libraries(oresforge_scalar_actor_frames_tests PRIVATE oresforge_scalar_actor)
  target_compile_features(oresforge_scalar_actor_frames_tests PRIVATE cxx_std_20)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(oresforge_scalar_actor_frames_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
  endif()
  add_test(NAME oresforge_actor_heap_frame_bridge_host COMMAND oresforge_scalar_actor_frames_tests)
  set_tests_properties(oresforge_actor_heap_frame_bridge_host PROPERTIES TIMEOUT 30)

  add_executable(oresforge_scalar_actor_tests verification/scalar_actor.cpp)
  target_link_libraries(oresforge_scalar_actor_tests PRIVATE oresforge_scalar_actor)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(oresforge_scalar_actor_tests PRIVATE ORES_ARENA_TEST_ALLOCATIONS=1)
    target_link_options(oresforge_scalar_actor_tests PRIVATE
      -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free)
  endif()
  add_test(NAME oresforge_scalar_actor_heap_and_turns COMMAND oresforge_scalar_actor_tests)
  set_tests_properties(oresforge_scalar_actor_heap_and_turns PROPERTIES TIMEOUT 30)
  add_executable(oresforge_scalar_heap_safety_tests verification/scalar_heap_safety.cpp)
  target_link_libraries(oresforge_scalar_heap_safety_tests PRIVATE oresforge_scalar_actor)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(oresforge_scalar_heap_safety_tests PRIVATE ORES_ARENA_TEST_ALLOCATIONS=1)
    target_link_options(oresforge_scalar_heap_safety_tests PRIVATE
      -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free)
  endif()
  add_test(NAME oresforge_scalar_heap_safety COMMAND oresforge_scalar_heap_safety_tests)
  set_tests_properties(oresforge_scalar_heap_safety PROPERTIES TIMEOUT 30)
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

# Compiler-backed opt-in stage-0 .atto actor path. The real oresforge parser
# emits C++ callbacks, which are built with Forge's actual private heap and
# logical frame runtime and then executed in native CTests. Not Cranelift,
# general actor codegen or a self-hosted compiler.
if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(ORESFORGE_ACTOR_SANITIZED_ARG "")
  if(CMAKE_CXX_FLAGS MATCHES "fsanitize")
    set(ORESFORGE_ACTOR_SANITIZED_ARG "--sanitized")
  endif()
  add_test(NAME oresforge_compiled_atto_scalar_actor_native
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/verification/compiled_atto_scalar_actor.py"
      --compiler "$<TARGET_FILE:oresforge>"
      --root "${CMAKE_CURRENT_SOURCE_DIR}"
      --cc "${CMAKE_C_COMPILER}"
      --cxx "${CMAKE_CXX_COMPILER}"
      ${ORESFORGE_ACTOR_SANITIZED_ARG})
  set_tests_properties(oresforge_compiled_atto_scalar_actor_native PROPERTIES TIMEOUT 180)
endif()
