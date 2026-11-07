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
  GreaterEqual, Equal, NotEqual, If, Call, Local, Let
};
struct Node {
  Kind kind;
  std::int64_t value = 0;
  std::string name;
  std::vector<int> children;
  int callee = -1;
  // Formal slot for each lexical/source-order argument.
  // Empty metadata represents legacy positional-only constructed IR.
  std::vector<std::string> argument_labels;
  std::vector<std::size_t> argument_slots;
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
Module parse(std::string_view source);
void verify(const Module& program);
std::int64_t interpret(const Module& program, std::span<const std::int64_t> args);
std::string emit_asm(const Module& program, Arch arch);
}
