#pragma once
#include "codegen/native.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oresforge::calls {
// Opt-in, deliberately narrow stage-0 callable language. Distinct from the
// legacy scalar IR; no misleading claims about the general Oreslang ABI.
enum class Kind {
  Constant, Parameter, Add, Sub, Mul, Less, LessEqual, Greater,
  GreaterEqual, Equal, NotEqual, If, Call
};
struct Node {
  Kind kind;
  std::int64_t value = 0;
  std::string name;
  std::vector<int> children;
  int callee = -1;
};
struct Function {
  std::string name;
  std::vector<std::string> parameters;
  std::vector<Node> nodes;
  int result = -1;
};
struct Module {
  std::vector<Function> functions;
  int entry = -1;
};
// Default entry is strictly zero-argument. A trusted host may opt into an
// exact i64 arity (1..4), exported under a distinct ABI symbol.
Module parse(std::string_view source, std::size_t expected_entry_arity=0);
void verify(const Module& program, std::size_t expected_entry_arity=0);
std::int64_t interpret(const Module& program, std::span<const std::int64_t> args);
std::string emit_asm(const Module& program, Arch arch, std::size_t expected_entry_arity=0);
}
