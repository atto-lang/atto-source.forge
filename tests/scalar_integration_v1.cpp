#ifdef NDEBUG
#undef NDEBUG
#endif
#include "atto/scalar_eval.hpp"
#include "atto/scalar_emit.hpp"
#include <cassert>
#include <string>
#include <string_view>

using namespace atto::forge::scalar;

static void valid(std::string_view source, atto::forge::i64 expected) {
  const auto p = parse_source(source);
  assert(p.ok() && validate_ast(p));
  const auto v = evaluate(p);
  assert(v.ok() && *v.value == expected);
  const auto native = emit_cpp(p);
  assert(native.has_value() && !native->empty());
}
static void invalid(std::string_view source, ParseError wanted) {
  const auto p = parse_source(source);
  assert(!p.ok() && p.error == wanted);
  assert(!emit_cpp(p).has_value());
}
int main() {
  valid("fnc main(): int {return 12 ^^ 10;}", 6);
  valid("fnc main(): int {^ 42;}", 42);
  valid("fnc main(): int {return seq(1, 2, 42);}", 42);
  valid("fnc main(): int {return (> 1, 2, 42);}", 42);
  valid("fnc main(): int {return sync task<int> {return 42;};}", 42);
  valid("fnc main(): int {do sync task {};return 42;}", 42);
  valid("fnc main(): int {do sync task<void> {return;};return 42;}", 42);
  valid("fnc main(): int {return sync task<int> {return seq(1,2,42);};}", 42);
  valid("int <- main()\nbegin\n^ 42;\nend.", 42);
  valid("int\xE2\x80\xA2 main()\nbegin\n^ 42;\nend.", 42);
  valid("fnc main(): int {return /* outer /* copy!untrusted */ safe */ 42;}", 42);
  valid("pub fnc main(): int {let value: int = 42;val Pointer<int> ptr = rt ref value;"
        "val answer = rt deref ptr;rt unref ptr;return answer;}", 42);

  std::string deep = "fnc main(): int {return ";
  for (int i = 0; i < 128; ++i) deep += "/*";
  deep += " copy!hidden ";
  for (int i = 0; i < 128; ++i) deep += "*/";
  deep += " 42;}";
  valid(deep, 42);
  std::string too_deep = "fnc main(): int {return ";
  for (int i = 0; i < 129; ++i) too_deep += "/*";
  too_deep += " copy!hidden ";
  for (int i = 0; i < 129; ++i) too_deep += "*/";
  too_deep += " 42;}";
  invalid(too_deep, ParseError::lexical);

  invalid("fnc main(): int {do sync task<int> {return 41;};return 42;}",
          ParseError::task_return_type);
  invalid("fnc main(): int {do sync task {return 41;};return 42;}",
          ParseError::task_return_type);
  invalid("fnc main(): int {sync task {};return 42;}", ParseError::syntax);
  invalid("fnc main(): int {sync task<void> {};return 42;}", ParseError::syntax);
  invalid("fnc main(): int {return task<int> {return 42;};}",
          ParseError::task_unsupported);
  invalid("fnc main(): int {return c << 42;}",
          ParseError::io_direction_unsupported);
  invalid("fnc main(): int {return c >> 42;}",
          ParseError::io_direction_unsupported);
  invalid("fnc main(): int {return pipe;}",
          ParseError::unsupported_continuous_pipe);
  invalid("fnc main(): int {return rpipe;}",
          ParseError::unsupported_continuous_pipe);
  invalid("fnc main(): int {return copy!42;}",
          ParseError::ownership_bang_unsupported);
  invalid("fnc main(): int {return 12 ^ 10;}", ParseError::syntax);
  invalid("fnc main(): int {return seq(1,);}", ParseError::syntax);

  auto ordered = parse_source("fnc main(): int {return seq(1,2,42);}");
  assert(ordered.ok() && ordered.nodes[ordered.root].op == Op::sequence);
  auto forged_order = ordered;
  auto& root = forged_order.nodes[forged_order.root];
  const auto saved = root.left;
  root.left = root.right;
  root.right = saved;
  assert(!validate_ast(forged_order) && !emit_cpp(forged_order));
  auto shared_child = ordered;
  shared_child.nodes[shared_child.root].left =
      shared_child.nodes[shared_child.root].right;
  assert(!validate_ast(shared_child) && !emit_cpp(shared_child));

  auto divzero = evaluate_source("fnc main(): int {return seq(1/0, 42);}");
  assert(divzero.error == EvalError::divide_by_zero);
  auto overflow = evaluate_source(
      "fnc main(): int {return (> 9223372036854775807+1, 42);}");
  assert(overflow.error == EvalError::overflow);
  return 0;
}
