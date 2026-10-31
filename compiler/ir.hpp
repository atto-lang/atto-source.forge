#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace oresforge {
enum class Op { Constant, Parameter, Add, Sub, Mul, Div, Return,
                TableLookup, TableInsert, TableInsertNew, TableUpdateCounter };
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
  // Explicit internal-only interpreter profile. General IR and native
  // codegen cannot silently execute shared-table effects.
  bool shared_table_stage0 = false;
};
// Trusted runtime dispatcher. Never expose this object or caller-identity
// construction to guest code. Scalar guest programs carry only i64 operands.
class GuestI64TableDispatch {
 public:
  virtual ~GuestI64TableDispatch() = default;
  virtual std::int64_t lookup(std::int64_t key) = 0;
  virtual std::int64_t insert(std::int64_t key, std::int64_t value) = 0;
  virtual std::int64_t insert_new(std::int64_t key, std::int64_t value) = 0;
  virtual std::int64_t update_counter(std::int64_t key, std::int64_t delta) = 0;
};
void verify(const Program&);
std::int64_t interpret(const Program&);
std::int64_t interpret(const Program&,std::span<const std::int64_t> args);
// Only for source compiled with parse_shared_i64_stage0_source(). The
// dispatch is supplied by the trusted scheduler adapter, not by guest data.
std::int64_t interpret_shared_i64_stage0(
    const Program&, std::span<const std::int64_t> args, GuestI64TableDispatch&);
std::string dump_ir(const Program&);
}
