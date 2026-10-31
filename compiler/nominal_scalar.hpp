#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>

namespace oresforge::nominal {

// Deliberately bounded stage-1 scalar class, NOT general nominal/actor dispatch.
// One stack-owned i64 field, a mutable add method and a read method.
// Method calls are statically inlined; no GC, vtables or escaping self.
// Verified source-level class methods; the current subset supports a single
// i64 field and statically inlines method bodies into a caller-owned frame.
// This IR separates method identity from individual receiver call sites.
struct Method {
  enum class Kind { Add, Subtract, Read };
  std::string name;
  Kind kind;
  std::size_t value_arity = 0;
};
struct Step {
  enum class Kind { Construct, Mutate, ReturnRead };
  Kind kind;
  std::size_t instance = 0;
  std::size_t method = 0;
  std::int64_t argument = 0;
};
struct ScalarClass {
  std::string owner;
  std::string field;
  std::string binding;
  std::string writer;
  std::string reader;
  std::int64_t initializer = 0;
  std::int64_t argument = 0;
  std::vector<Method> methods;
  std::vector<std::string> instances;
  std::vector<Step> steps;
};

ScalarClass parse(std::string_view source);
std::int64_t interpret(const ScalarClass& program);
// Emits an actual x86_64 SysV function oresforge_entry() -> i64;
// arithmetic traps on overflow. No arm64 or virtual-call ABI is implied.
std::string emit_x86_64_asm(const ScalarClass& program);
} // namespace oresforge::nominal
