#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace oresforge::nominal {

// Deliberately bounded stage-1 scalar class, NOT general nominal/actor dispatch.
// One stack-owned i64 field, a mutable add method and a read method.
// Method calls are statically inlined; no GC, vtables or escaping self.
struct ScalarClass {
  std::string owner;
  std::string field;
  std::string binding;
  std::string writer;
  std::string reader;
  std::int64_t initializer = 0;
  std::int64_t argument = 0;
};

ScalarClass parse(std::string_view source);
std::int64_t interpret(const ScalarClass& program);
// Emits an actual x86_64 SysV function oresforge_entry() -> i64;
// arithmetic traps on overflow. No arm64 or virtual-call ABI is implied.
std::string emit_x86_64_asm(const ScalarClass& program);
} // namespace oresforge::nominal
