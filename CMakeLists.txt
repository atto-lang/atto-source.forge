cmake_minimum_required(VERSION 3.20)
project(atto_forge_checked_i64 LANGUAGES CXX)

include(CTest)
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

