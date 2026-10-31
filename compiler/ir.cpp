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
    case Op::Return: return "ret";
    case Op::TableLookup: return "guest.table.lookup.i64";
    case Op::TableInsert: return "guest.table.insert.i64";
    case Op::TableInsertNew: return "guest.table.insert_new.i64";
    case Op::TableUpdateCounter: return "guest.table.update_counter.i64";
  }
  throw std::runtime_error("unknown opcode");
}
std::int64_t checked(Op op, std::int64_t a, std::int64_t b) {
  std::int64_t result = 0;
  switch (op) {
    case Op::Add:
      if (__builtin_add_overflow(a,b,&result)) throw std::runtime_error("i64 add overflow");
      return result;
    case Op::Sub:
      if (__builtin_sub_overflow(a,b,&result)) throw std::runtime_error("i64 sub overflow");
      return result;
    case Op::Mul:
      if (__builtin_mul_overflow(a,b,&result)) throw std::runtime_error("i64 mul overflow");
      return result;
    case Op::Div:
      if (b == 0) throw std::runtime_error("division by zero");
      if (a == std::numeric_limits<std::int64_t>::min() && b == -1)
        throw std::runtime_error("i64 division overflow");
      return a / b;
    default: throw std::runtime_error("not a binary opcode");
  }
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
    } else if (ins.op == Op::TableLookup || ins.op == Op::TableInsert ||
               ins.op == Op::TableInsertNew || ins.op == Op::TableUpdateCounter) {
      if (!p.shared_table_stage0 || ins.value != 0 || !before(ins.a,i) ||
          (ins.op == Op::TableLookup ? ins.b != -1 : !before(ins.b,i)))
        throw std::runtime_error("invalid or unadmitted shared-table IR");
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
namespace {
std::int64_t interpret_impl(const Program& p,std::span<const std::int64_t> args,
                            GuestI64TableDispatch* table) {
  verify(p);
  if (p.shared_table_stage0 != (table != nullptr))
    throw std::runtime_error("shared-table stage0 requires matching trusted dispatcher");
  if (args.size()!=p.parameter_count) throw std::runtime_error("runtime argument count mismatch");
  std::vector<std::int64_t> values;
  values.reserve(p.instructions.size());
  for (const auto& ins : p.instructions) {
    if (ins.op == Op::Return) return values.at(static_cast<std::size_t>(ins.a));
    if (ins.op == Op::Constant) values.push_back(ins.value);
    else if (ins.op == Op::Parameter)
      values.push_back(args[static_cast<std::size_t>(ins.value)]);
    else if (ins.op == Op::TableLookup)
      values.push_back(table->lookup(values.at(static_cast<std::size_t>(ins.a))));
    else if (ins.op == Op::TableInsert)
      values.push_back(table->insert(values.at(static_cast<std::size_t>(ins.a)),
                                    values.at(static_cast<std::size_t>(ins.b))));
    else if (ins.op == Op::TableInsertNew)
      values.push_back(table->insert_new(values.at(static_cast<std::size_t>(ins.a)),
                                        values.at(static_cast<std::size_t>(ins.b))));
    else if (ins.op == Op::TableUpdateCounter)
      values.push_back(table->update_counter(values.at(static_cast<std::size_t>(ins.a)),
                                             values.at(static_cast<std::size_t>(ins.b))));
    else values.push_back(checked(ins.op, values.at(static_cast<std::size_t>(ins.a)),
                                           values.at(static_cast<std::size_t>(ins.b))));
  }
  throw std::runtime_error("missing return");
}
}
std::int64_t interpret(const Program& p,std::span<const std::int64_t> args) {
  return interpret_impl(p,args,nullptr);
}
std::int64_t interpret(const Program& p) {
  return interpret(p,std::span<const std::int64_t>{});
}
std::int64_t interpret_shared_i64_stage0(
    const Program& p, std::span<const std::int64_t> args,
    GuestI64TableDispatch& table) {
  return interpret_impl(p,args,&table);
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
      else if (ins.op == Op::TableLookup) out << " %" << ins.a;
      else out << " %" << ins.a << ", %" << ins.b;
    }
    out << '\n';
  }
  return out.str();
}
}
