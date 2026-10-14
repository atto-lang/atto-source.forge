#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace oresforge {
enum class Op { Constant, Add, Sub, Mul, Div, Return };
struct Instruction {
  Op op;
  std::int64_t value = 0;
  int a = -1;
  int b = -1;
};
struct Program { std::vector<Instruction> instructions; };
void verify(const Program&);
std::int64_t interpret(const Program&);
std::string dump_ir(const Program&);
}
