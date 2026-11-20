#pragma once
#include "compiler/source_exports_v1.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace oresforge {
enum class Op { Constant, Parameter, Add, Sub, Mul, Div, Return };
struct Instruction {
  Op op;
  std::int64_t value = 0;
  int a = -1;
  int b = -1;
};
struct Program {
  std::vector<Instruction> instructions;
  // Stage-0 native ABI: up to four signed i64 arguments, in source order.
  std::size_t parameter_count = 0;
  // Source-level bindings only; never native symbol authorization.
  // Append to preserve existing legacy aggregate Program initializers.
  std::vector<SourceExportBinding> source_export_bindings;
};
void verify(const Program&);
std::int64_t interpret(const Program&);
std::int64_t interpret(const Program&,std::span<const std::int64_t> args);
std::string dump_ir(const Program&);
}
