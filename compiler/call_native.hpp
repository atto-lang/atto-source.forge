#pragma once
#include "codegen/native.hpp"
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oresforge::calls {
// Opt-in, deliberately narrow stage-0 callable language. Distinct from the
// legacy scalar IR; no misleading claims about the general Oreslang ABI.
enum class Kind {
  Constant, Parameter, Add, Sub, Mul, Less, LessEqual, Greater,
  GreaterEqual, Equal, NotEqual, If, Call, Boolean, Not, Ternary, And, Or, BooleanXor,
  OptionSome, OptionNone, OptionCall, OptionMatch, OptionPayload, Div, ResultTrap, ResultMatch, ResultOk
};
// Provisional scalar native status ABI. Two INTEGER-class eightbytes on
// SysV x86-64: value in RAX, error code in RDX. No heap allocation.
enum class ArithmeticStatus : std::uint64_t {
  Ok=0, Overflow=65537, DivisionByZero=65538
};
struct ArithmeticResult {
  std::int64_t value;
  ArithmeticStatus status;
};
static_assert(std::is_standard_layout_v<ArithmeticResult> &&
              std::is_trivially_copyable_v<ArithmeticResult>);
static_assert(sizeof(ArithmeticResult)==16 && offsetof(ArithmeticResult,status)==8);
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
  bool returns_option_int = false;
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
