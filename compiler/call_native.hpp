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
  GreaterEqual, Equal, NotEqual, If, Call, Boolean, Not, Ternary, And, Or, BooleanXor, Div, Trap, ResultCall, BindResult, Local, Ok, Some, None, OptionIf, OptionCall
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
// Distinct Option ABI: tag 0=None, 1=Some; value zeroed for None.
struct ScalarOption {
  std::int64_t value;
  std::uint64_t tag;
};
static_assert(std::is_standard_layout_v<ScalarOption> &&
              std::is_trivially_copyable_v<ScalarOption>);
static_assert(sizeof(ScalarOption)==16 && offsetof(ScalarOption,tag)==8);
// Public inspection carrier; not the internal register convention.
// tag is the declared return type's variant (zero for int); effect is
// nonzero only for an escaping ordinary arithmetic error.
struct ScalarOutcome {
  std::int64_t value;
  std::uint64_t tag;
  ArithmeticStatus effect;
};
static_assert(std::is_standard_layout_v<ScalarOutcome> &&
              std::is_trivially_copyable_v<ScalarOutcome>);
static_assert(sizeof(ScalarOutcome)==24 && offsetof(ScalarOutcome,effect)==16);
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
  bool returns_scalar_result = false;
  bool returns_scalar_option = false;
};
struct Module {
  std::vector<Function> functions;
  int entry = -1;
};
Module parse(std::string_view source);
void verify(const Module& program);
std::int64_t interpret(const Module& program, std::span<const std::int64_t> args);
// Only the explicitly typed Result<int,Error> entry may use this API.
// A nonzero status is an Err payload, not an escaping ordinary throw.
ArithmeticResult interpret_result(const Module& program, std::span<const std::int64_t> args);
ScalarOption interpret_option(const Module& program, std::span<const std::int64_t> args);
ScalarOutcome interpret_effect(const Module& program, std::span<const std::int64_t> args);
std::string emit_asm(const Module& program, Arch arch);
}
