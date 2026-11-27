// Typed plan mutation tests; these are host-level security gates, not guest proof.
#include "atto/scalar_emit.hpp"
#include <cassert>
#include <string>
using namespace atto::forge::scalar;
static constexpr auto exact = R"(pub fnc main(): int {
  let value: int = 42;
  val Pointer<int> ptr = rt ref value;
  val answer = rt deref ptr;
  rt unref ptr;
  return answer;
}
)";
int main() {
  const auto source = parse_source(exact);
  assert(source.ok() && source.local.has_value());
  assert(validate_ast(source));
  const auto code = emit_cpp(source);
  assert(code.has_value());
  assert(code->find("const atto::forge::i64* __atto_borrow") != std::string::npos);
  assert(code->find("__atto_borrow = nullptr") != std::string::npos);
  assert(code->find("delete ") == std::string::npos);
  auto forged = source;
  forged.local->initial = 13;
  assert(!validate_ast(forged));
  assert(!emit_cpp(forged));
  forged = source;
  forged.local->handle = forged.local->referent;
  assert(!validate_ast(forged));
  forged = source;
  forged.local->copied_scalar = forged.local->handle;
  assert(!validate_ast(forged));
  forged = source;
  forged.nodes[0].value = 17;
  assert(!validate_ast(forged));
  forged = source;
  forged.nodes.emplace_back(Node{Op::literal, 0, 0, 42});
  forged.root = 1;
  assert(!validate_ast(forged));
  // Lexical pointer profiles preserve the signed int literal gate:
  // zero and MAX_I64 are valid, an overflowing positive literal is not.
  auto zero = parse_source(std::string(exact).replace(
      std::string(exact).find("= 42;"), 5, "= 0;"));
  assert(zero.ok() && zero.local && zero.local->initial == 0);
  auto max_i64 = parse_source(std::string(exact).replace(
      std::string(exact).find("= 42;"), 5, "= 9223372036854775807;"));
  assert(max_i64.ok() && max_i64.local &&
         max_i64.local->initial == std::numeric_limits<std::int64_t>::max());
  auto overflow = parse_source(std::string(exact).replace(
      std::string(exact).find("= 42;"), 5, "= 9223372036854775808;"));
  assert(!overflow.ok() && overflow.error == ParseError::literal_out_of_range);
  auto slot = parse_source("pub fnc main(): int { let a: int = 40; let b: int = 2; let mut RawPointer<int> p = RawPointer.empty<int>(); ref! p to a; val first = deref! p; unref! p; ref! p to b; val second = deref! p; unref! p; return first + second; }");
  assert(slot.ok() && slot.reusable_slot && validate_ast(slot) && emit_cpp(slot));
  auto bad = slot;
  bad.reusable_slot->steps[1].index = 1;
  assert(!validate_ast(bad) && !emit_cpp(bad));
  bad = slot;
  bad.reusable_slot->steps.pop_back();
  assert(!validate_ast(bad));
  bad = slot;
  bad.nodes[0].value = 43;
  assert(!validate_ast(bad));
  for (const std::string body : {"val x = deref! p; return x;", "p = null; return 42;",
       "ref! p to a; ref! p to a; unref! p; return 42;", "ref! p to a; return 42;",
       "ref! p to a; unref! p; unref! p; return 42;", "return p;"}) {
    const auto invalid = parse_source("pub fnc main(): int { let a: int = 42; let mut RawPointer<int> p = RawPointer.empty<int>(); " + body + " }");
    assert(!invalid.ok() && !emit_cpp(invalid));
  }
  return 0;
}
