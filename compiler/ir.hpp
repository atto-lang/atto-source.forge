#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace oresforge {
enum class Op { Constant, Parameter, Add, Sub, Mul, Div, Abs, Return };

enum class EffectKind : std::uint8_t {
  Return=0, Throw=1, Raise=2, Panic=3, Cancel=4, Fatal=5, ProcessExit=6
};
enum class NumericErrorCode : std::uint32_t {
  ArithmeticOverflowError=65537u,
  DivisionByZeroError=65538u,
  InexactConversionError=65539u,
  NumericRangeError=65540u,
  NumericParseError=65541u,
  NumericContextError=65542u,
  NumericResourceError=65543u,
  NonterminatingDecimalError=65544u
};
struct EffectStatus {
  EffectKind kind=EffectKind::Return;
  std::uint32_t code=0;
};
struct I64EffectResult {
  std::int64_t value=0;
  EffectStatus effect{};
  const char* diagnostic=nullptr;
  [[nodiscard]] bool is_return() const noexcept {
    return effect.kind==EffectKind::Return;
  }
};
class GuestEffectTransport final : public std::runtime_error {
public:
  GuestEffectTransport(EffectStatus status,const char* message)
    : std::runtime_error(message ? message : "guest effect"), status_(status) {}
  [[nodiscard]] EffectStatus status() const noexcept { return status_; }
private:
  EffectStatus status_;
};

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
I64EffectResult interpret_effect(const Program&);
I64EffectResult interpret_effect(const Program&,std::span<const std::int64_t> args);
std::int64_t interpret(const Program&);
std::int64_t interpret(const Program&,std::span<const std::int64_t> args);
std::string dump_ir(const Program&);
}
