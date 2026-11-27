#include "atto/scalar_eval.hpp"
#include <cassert>
#include <limits>
#include <string>
using namespace atto::forge;
using namespace atto::forge::scalar;
static std::string program(const std::string& expr) {
  return "pub fnc main(): int { return " + expr + "; }";
}
int main() {
  struct Case { const char* expr; i64 expected; };
  for (const auto c : {
    Case{"42",42}, {"4 + 5 * 6",34}, {"(4 + 5) * 6",54},
    {"10 - 2 * 3",4}, {"-10 + +3",-7}, {"-7 / 3",-2},
    {"-7 % 3",-1}, {"7 % -3",1}, {"-7 % -3",-1},
    {"-9223372036854775808",std::numeric_limits<i64>::min()},
    {"-0009223372036854775808",std::numeric_limits<i64>::min()},
    {"9223372036854775807",std::numeric_limits<i64>::max()},
    {"-9223372036854775808 + 1",std::numeric_limits<i64>::min() + 1},
    {"8/2*3+1",13},
    {"1 <<< 63",std::numeric_limits<i64>::min()},
    {"-1 >>> 1",std::numeric_limits<i64>::max()},
    {"-8 ashr 2",-2}, {"(1 <<< 63) >>> 63",1},
    {"-8 ashr 2 + 1",-1}, {"~0",-1}, {"-1 & 42",42}, {"1 | 4",5}, {"6 ^^ 3",5},
    {"~0 & 255",255}, {"1 | 2 & 4",1}, {"1 ^^ 3 & 2",3},
    {"1 | 2 ^^ 3 & 2",1}, {"~(-9223372036854775808)",9223372036854775807LL}
  }) {
    const auto value = evaluate_source(program(c.expr));
    assert(value.ok() && *value.value == c.expected);
  }
  for (const char* expr : {
    "9223372036854775807 + 1",
    "-9223372036854775808 - 1",
    "-0009223372036854775808 - 1",
    "-9223372036854775808 / -1",
    "-9223372036854775808 % -1",
    "-(-9223372036854775808)",
    "9223372036854775807 * 2"
  }) assert(evaluate_source(program(expr)).error == EvalError::overflow);
  for (const char* expr : {"1 / 0", "2 % 0", "1 / (7 - 7)"})
    assert(evaluate_source(program(expr)).error == EvalError::divide_by_zero);
  for (const char* expr : {"1 <<< 64", "1 <<< -1", "1 >>> 64",
      "1 ashr -1", "1 <<< (32 + 32)"})
    assert(evaluate_source(program(expr)).error == EvalError::shift_distance);
  assert(evaluate_source("fnc main(): int { ^ 12 ^^ 10; }").value == 6);
  assert(evaluate_source(program("12 ^ 10")).error == EvalError::parse_error);
  assert(evaluate_source(program("abc")).error == EvalError::parse_error);
  auto ast = parse_source(program("5+3"));
  assert(ast.ok());
  ast.nodes[0].op = Op::add; ast.nodes[0].left = 0;
  assert(evaluate(ast).error == EvalError::invalid_ast);
  auto detached = parse_source(program("5+3"));
  assert(detached.ok() && validate_ast(detached));
  detached.root = 0;
  assert(!validate_ast(detached));
  assert(evaluate(detached).error == EvalError::invalid_ast);
  auto stray = parse_source(program("5+3"));
  stray.nodes.push_back(Node{Op::literal, 0, 0, 12});
  stray.root = stray.nodes.size()-1;
  assert(!validate_ast(stray));
  assert(evaluate(stray).error == EvalError::invalid_ast);
  auto forged = parse_source(program("5+3"));
  forged.nodes.back().op = static_cast<Op>(999);
  assert(!validate_ast(forged));
  assert(evaluate(forged).error == EvalError::invalid_ast);
  auto bad_unary = parse_source(program("~7"));
  assert(bad_unary.ok());
  bad_unary.nodes.back().right = 1;
  assert(!validate_ast(bad_unary));
  assert(evaluate(bad_unary).error == EvalError::invalid_ast);
  const auto range = parse_source("type R = int where self >= 1 and self <= 42; fnc main(): int { return 42::<R>; }");
  assert(range.ok() && evaluate(range).value == 42);
  assert(evaluate_source("type R = int where self > 0; fnc main(): int { return 0::<R>; }").error == EvalError::refinement_violation);
  const std::string contract_header="type R = int where self >= 1 and self <= 42; fnc f(int x) -> int requires x >= 1 requires x <= 42";
  const std::string contract_tail=" pub fnc main() -> int { return f(2); }";
  for(const char* body:{" { return 84/x; }","\nbegin return 84/x; end"," :: return 84/x; end","\n:: return 84/x; end"}){
    const auto parsed=parse_verified_strict_i64(contract_header+body+contract_tail);
    assert(parsed.ok()&&evaluate(parsed).value==42);
  }
  for(const char* body:{" begin return 84/x; end"," :: return 84/x; }"," :: return 84/x;"," :: begin return 84/x; end"})
    assert(!parse_verified_strict_i64(contract_header+body+contract_tail).ok());
  // Host-model controls only: native C++ guest execution is a separate gate.
  // NOT lowers only in the verified predicate lane. The true/false
  // fact swap must make the nonzero divisor proof available on the true edge.
  // Ordinary scalar Boolean slots and short-circuit conditional returns.
  for(const auto& source : {
      "pub fnc main(): int { if ¬false && !false then return 42; else return 0; fi }",
      "pub fnc main(): int { val bool b = ¬false; val bool c = b && !!true; if c then return 42; else return 0; fi }",
      "pub fnc main(): int { return (false || true && ¬false) ? 42 : 0; }"}) {
    const auto p=parse_source(source);assert(p.ok());assert(evaluate(p).value==42);
  }
  for(const auto& source : {
      "pub fnc main(): int { val bool b = true; return b + 1; }",
      "pub fnc main(): int { val bool b = true; return b; }",
      "pub fnc main(): int { if false && ¬1 then return 42; else return 0; fi }",
      "pub fnc main(): int { val bool b = true; if b¬ then return 42; else return 0; fi }"})assert(!parse_source(source).ok());
  const std::string unicode_prefix = "type R = int where self >= 1 and self <= 42; ";
  for (const auto* body : {
      "fnc f(int x) -> int { if ¬(x == 0) { return 42/x; } return 42; } pub fnc main() -> int { return f(1); }",
      "fnc f(int x) -> int { if ¬¬(x == 0) { return 42; } return 0; } pub fnc main() -> int { return f(0); }",
      "fnc f(int x) -> int { if ¬false && ¬(x != 0) { return 42; } return 0; } pub fnc main() -> int { return f(0); }"}) {
    const auto parsed = parse_verified_strict_i64(unicode_prefix + body);
    assert(parsed.ok() && validate_ast(parsed) && evaluate(parsed).value == 42);
  }
  for (const auto* body : {
      "fnc f(int x) -> int { if ¬x { return 42; } return 0; } pub fnc main() -> int { return f(1); }",
      "fnc f(int x) -> int { if ¬x == 0 { return 42; } return 0; } pub fnc main() -> int { return f(0); }",
      "fnc f(int x) -> int { if ¬(x == 0) { return 42; } return 42/x; } pub fnc main() -> int { return f(1); }"})
    assert(!parse_verified_strict_i64(unicode_prefix + body).ok());
  const std::string guard = "type R = int where self >= 1 and self <= 42; "
      "fnc f(int x) -> int { if x != 0 && 42 / x > 0 { return 1; } return 42; } "
      "pub fnc main() -> int { return f(0); }";
  auto lazy = parse_verified_strict_i64(guard);
  assert(lazy.ok() && evaluate(lazy).value == 42);
  // A branch-local call must not execute its argument on the skipped path.
  const std::string helper_prefix = "type R = int where self >= 1 and self <= 42; "
      "fnc good(int x) -> int { val y=x; return y; } ";
  for (const auto* body : {
      "fnc f(int x) -> int { if x != 0 && good(42/x)>0 { return 1; } return 42; } pub fnc main() -> int { return f(0); }",
      "fnc f(int x) -> int { if x == 0 || good(42/x)>0 { return 42; } return 0; } pub fnc main() -> int { return f(0); }",
      "fnc f(int x) -> int { if x != 0 { return good(42/x); } return 0; } pub fnc main() -> int { return f(1); }",
      "fnc outer(int x) -> int { return good(x); } fnc f(int x) -> int { if x != 0 { return outer(42/x); } return 0; } pub fnc main() -> int { return f(1); }"}) {
    const auto parsed = parse_verified_strict_i64(helper_prefix + body);
    assert(parsed.ok() && validate_ast(parsed) && evaluate(parsed).value == 42);
  }
  for (const auto* body : {
      "fnc f(int x) -> int { if x == 0 { return good(42/x); } return 42; } pub fnc main() -> int { return f(1); }",
      "fnc f(int x, int y) -> int { if x != 0 { return good(42/y); } return 42; } pub fnc main() -> int { return f(1,1); }"})
    assert(!parse_verified_strict_i64(helper_prefix + body).ok());
  auto forged_choice = lazy;
  for(auto& n:forged_choice.nodes)if(n.op==Op::choose){n.value=-1;break;}
  assert(!validate_ast(forged_choice) && evaluate(forged_choice).error==EvalError::invalid_ast);
  // Transfer state must come from a scheduled, already evaluated version.
  const auto transfer=parse_verified_strict_i64(
    "type Count = int where self >= 0 and self <= 42; "
    "pub fnc main() -> int { let Count x = 0; let Count y = 0; "
    "while x < 42 { x=x+1; y=x; if x==42 { break; } } return y; }");
  assert(transfer.ok() && validate_ast(transfer) && evaluate(transfer).value==42);
  const auto group=std::find_if(transfer.nodes.begin(),transfer.nodes.end(),
    [](const auto& n){return n.op==Op::loop_group&&!n.exits.empty();});
  assert(group!=transfer.nodes.end()&&group->states.size()==2&&group->effects.size()>=2);
  const auto gi=static_cast<std::size_t>(group-transfer.nodes.begin());
  for(int mutation=0;mutation<6;++mutation){
    auto bad=transfer;auto& g=bad.nodes[gi];auto& x=g.exits.front();
    switch(mutation){
      case 0:x.values[0]=g.states[1];break;
      case 1:x.values[0]=g.right;break;
      case 2:x.condition=g.right;break;
      case 3:std::swap(g.effects[0],g.effects[1]);break;
      case 4:g.effects[1]=g.effects[0];break;
      case 5:g.updates[0]=g.states[1];break;
    }
    assert(!validate_ast(bad)&&evaluate(bad).error==EvalError::invalid_ast);
  }
  auto bad_range=range;
  bad_range.nodes.back().value=43;bad_range.nodes.back().upper=42;
  assert(evaluate(bad_range).error==EvalError::invalid_ast);
}
