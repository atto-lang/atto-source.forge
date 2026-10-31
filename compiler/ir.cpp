#include "compiler/ir.hpp"
#include <limits>
#include <sstream>
#include <stdexcept>
namespace oresforge {
namespace {
bool before(int index, std::size_t offset) {
  return index >= 0 && static_cast<std::size_t>(index) < offset;
}
const char* opcode(Op op) {
  switch (op) {
    case Op::Constant: return "const.i64";
    case Op::Parameter: return "param.i64";
    case Op::Add: return "add.i64.checked";
    case Op::Sub: return "sub.i64.checked";
    case Op::Mul: return "mul.i64.checked";
    case Op::Div: return "div.i64.checked";
    case Op::Abs: return "abs.i64.checked";
    case Op::Return: return "ret";
  }
  throw std::runtime_error("unknown opcode");
}
I64EffectResult checked(Op op, std::int64_t a, std::int64_t b) noexcept {
  std::int64_t result = 0;
  constexpr EffectStatus overflow{
    EffectKind::Throw,
    static_cast<std::uint32_t>(NumericErrorCode::ArithmeticOverflowError)};
  constexpr EffectStatus divide_by_zero{
    EffectKind::Throw,
    static_cast<std::uint32_t>(NumericErrorCode::DivisionByZeroError)};
  switch (op) {
    case Op::Add:
      if (__builtin_add_overflow(a,b,&result))
        return {0,overflow,"i64 add overflow"};
      return {result,{}};
    case Op::Sub:
      if (__builtin_sub_overflow(a,b,&result))
        return {0,overflow,"i64 sub overflow"};
      return {result,{}};
    case Op::Mul:
      if (__builtin_mul_overflow(a,b,&result))
        return {0,overflow,"i64 mul overflow"};
      return {result,{}};
    case Op::Div:
      if (b == 0) return {0,divide_by_zero,"division by zero"};
      if (a == std::numeric_limits<std::int64_t>::min() && b == -1)
        return {0,overflow,"i64 division overflow"};
      return {a / b,{}};
    default:
      return {0,{EffectKind::Fatal,0xFFFFFFFDu},"not a binary opcode"};
  }
}
I64EffectResult checked_abs(std::int64_t value) noexcept {
  if (value == std::numeric_limits<std::int64_t>::min())
    return {0,{EffectKind::Throw,
      static_cast<std::uint32_t>(NumericErrorCode::ArithmeticOverflowError)},
      "i64 abs overflow"};
  return {value < 0 ? -value : value,{}};
}
}
void verify(const Program& p) {
  if (p.parameter_count > 4) throw std::runtime_error("runtime entry parameter budget exceeded");
  if (p.instructions.empty()) throw std::runtime_error("empty IR");
  if (p.instructions.size() > 100000) throw std::runtime_error("IR instruction budget exceeded");
  for (std::size_t i=0; i<p.instructions.size(); ++i) {
    const auto& ins = p.instructions[i];
    if (ins.op == Op::Constant) {
      if (ins.a != -1 || ins.b != -1) throw std::runtime_error("constant operands");
    } else if (ins.op == Op::Parameter) {
      if (ins.a != -1 || ins.b != -1 || ins.value < 0 ||
          static_cast<std::uint64_t>(ins.value) >= p.parameter_count)
        throw std::runtime_error("invalid runtime parameter index");
    } else if (ins.op == Op::Return) {
      if (i+1 != p.instructions.size() || !before(ins.a,i) || ins.b != -1)
        throw std::runtime_error("return must be valid and terminal");
    } else if (ins.op == Op::Abs) {
      if (!before(ins.a,i) || ins.b != -1)
        throw std::runtime_error("abs must use one prior operand");
    } else {
      switch (ins.op) {
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: break;
        default: throw std::runtime_error("unknown opcode");
      }
      if (!before(ins.a,i) || !before(ins.b,i))
        throw std::runtime_error("use before definition");
    }
  }
  if (p.instructions.back().op != Op::Return) throw std::runtime_error("missing return");
}
I64EffectResult interpret_effect(
    const Program& p,std::span<const std::int64_t> args) {
  verify(p);
  if (args.size()!=p.parameter_count)
    throw std::runtime_error("runtime argument count mismatch");
  std::vector<std::int64_t> values;
  values.reserve(p.instructions.size());
  for (const auto& ins : p.instructions) {
    if (ins.op == Op::Return)
      return {values.at(static_cast<std::size_t>(ins.a)),{}};
    if (ins.op == Op::Constant) {
      values.push_back(ins.value);
      continue;
    }
    if (ins.op == Op::Parameter) {
      values.push_back(args[static_cast<std::size_t>(ins.value)]);
      continue;
    }
    I64EffectResult step = ins.op == Op::Abs
      ? checked_abs(values.at(static_cast<std::size_t>(ins.a)))
      : checked(ins.op,values.at(static_cast<std::size_t>(ins.a)),
                       values.at(static_cast<std::size_t>(ins.b)));
    if (!step.is_return()) return step;
    values.push_back(step.value);
  }
  return {0,{EffectKind::Fatal,0xFFFFFFFCu},"missing return"};
}
I64EffectResult interpret_effect(const Program& p) {
  return interpret_effect(p,std::span<const std::int64_t>{});
}
std::int64_t interpret(const Program& p,std::span<const std::int64_t> args) {
  const auto result=interpret_effect(p,args);
  if (result.is_return()) return result.value;
  throw GuestEffectTransport(result.effect,result.diagnostic);
}
std::int64_t interpret(const Program& p) {
  return interpret(p,std::span<const std::int64_t>{});
}
std::string dump_ir(const Program& p) {
  verify(p);
  std::ostringstream out;
  for (std::size_t i=0; i<p.instructions.size(); ++i) {
    const auto& ins=p.instructions[i];
    if (ins.op == Op::Return) out << "ret %" << ins.a;
    else {
      out << '%' << i << " = " << opcode(ins.op);
      if (ins.op == Op::Constant || ins.op == Op::Parameter) out << ' ' << ins.value;
      else if (ins.op == Op::Abs) out << " %" << ins.a;
      else out << " %" << ins.a << ", %" << ins.b;
    }
    out << '\n';
  }
  return out.str();
}
}
