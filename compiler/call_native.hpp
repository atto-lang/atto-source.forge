#pragma once
#include "codegen/native.hpp"
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
  GreaterEqual, Equal, NotEqual, If, Catch, Call
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
  std::string module_name;
  std::vector<std::string> exports;
};
Module parse(std::string_view source);
// Parse one canonical library module without inventing an entrypoint. This is
// syntax/type/call admission only; multi-file linking remains a separate gate.
Module parse_library(std::string_view source);
// Link one explicitly supplied leaf library to one entry source. The entry
// must contain exactly one matching relative import module declaration.
Module parse_linked(std::string_view entry_source,
                    std::string_view library_source,
                    std::string_view expected_import_specifier);
void verify(const Module& program);
std::int64_t interpret(const Module& program, std::span<const std::int64_t> args);
std::string emit_asm(const Module& program, Arch arch);
}
