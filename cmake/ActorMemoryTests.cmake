# These tests exercise runtime host components, not guest actor lowering.
add_executable(oresforge_actor_arena_tests verification/actor_arena_test.cpp)
add_executable(oresforge_actor_memory_integration
  verification/actor_memory_integration.cpp runtime/actor.cpp)
foreach(target IN ITEMS oresforge_actor_arena_tests oresforge_actor_memory_integration)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_link_libraries(${target} PRIVATE Threads::Threads)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endforeach()
add_test(NAME oresforge_actor_arena COMMAND oresforge_actor_arena_tests)
add_test(NAME oresforge_actor_memory_integration COMMAND oresforge_actor_memory_integration)
set_tests_properties(oresforge_actor_arena oresforge_actor_memory_integration
  PROPERTIES TIMEOUT 60)
