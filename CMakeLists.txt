cmake_minimum_required(VERSION 3.20)
project(OresForge VERSION 0.1.0 LANGUAGES CXX)
option(ORESFORGE_FUZZ "Build Clang libFuzzer target" OFF)
add_library(oresforge_core STATIC compiler/frontend.cpp compiler/ir.cpp codegen/native.cpp runtime/actor.cpp jit/tiered.cpp)
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
  add_executable(oresforge_tests verification/tests.cpp)
  add_dependencies(oresforge_tests oresforge_numeric_i64_cases)
  target_include_directories(oresforge_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(oresforge_tests PRIVATE oresforge_core)
  add_test(NAME oresforge_unit COMMAND oresforge_tests)
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
endif()
