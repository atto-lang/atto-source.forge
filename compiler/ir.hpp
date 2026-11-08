#pragma once
#include <cstdint>
#include <functional>
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
};
void verify(const Program&);
std::int64_t interpret(const Program&);
std::int64_t interpret(const Program&,std::span<const std::int64_t> args);
// Host diagnostic hook for bounded straight-line IR (not suspend-safe actor execution).
std::int64_t interpret_metered(const Program&,std::span<const std::int64_t> args,
    const std::function<void(std::uint64_t,bool)>& on_checkpoint);
std::string dump_ir(const Program&);
}
