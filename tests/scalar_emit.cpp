#include "atto/scalar_emit.hpp"
#include <cassert>
#include <string>
using namespace atto::forge::scalar;
int main() {
  for(const auto* body:{
    "let int b=21;let int a<-b;return a+b;",
    "let int a=0;let int b=21;set a<-b;return a+b;",
    "let int a<-42;return a;",
    "let int a=0;set a<-42;return a;"
  }){
    const auto source=std::string("fnc main():int{")+body+"}";
    assert(parse_source(source).ok());
    assert(emit_cpp_source(source));
  }
  for(const auto* body:{
    "const int a<-42;return a;", "int a<-42;return a;",
    "val int a<-42;return a;", "let int a=0;set! a<-42;return a;",
    "let int a=0;a<-42;return a;", "let int a=0;set missing<-42;return a;",
    "let int a=42;set a<-a;return a;", "let int a<-true;return a;",
    "let int a< -42;return a;", "let int a<-21+21;return a;",
    "let int a<-b;return a;", "let int a=21;let int a<-a;return a;"
  }) assert(!emit_cpp_source(std::string("fnc main():int{")+body+"}"));

  const auto fixed=emit_cpp_source("fnc main():int{val int x=42;return x;}");
  assert(fixed);
  for (const auto* declaration : {"int x=42;", "const int x=42;",
      "const x:int=42;", "const x as int=42;", "const mut int x=42;"}) {
    const auto input=std::string("fnc main():int{")+declaration+"return x;}";
    assert(emit_cpp_source(input)==fixed);
  }
  assert(!emit_cpp_source("fnc main():int{const int x=1;const int x=42;return x;}"));
  const auto source = "pub fnc main(): int {return 4 + 5 * 6;}";
  const auto c = emit_cpp_source(source);
  assert(c && c == emit_cpp_source(source));
  assert(c->find("checked_add") != std::string::npos);
  assert(c->find("checked_mul") != std::string::npos);
  assert(c->find("std::printf") != std::string::npos);
  const auto mn = emit_cpp_source(
    "fnc main(): int {return -9223372036854775808;}");
  assert(mn && mn->find("numeric_limits<atto::forge::i64>::min()") != std::string::npos);
  const auto bitwise = emit_cpp_source(
    "fnc main(): int {return ~0 & 15 | 1 ^^ 2;}");
  assert(bitwise && bitwise->find("bit_not") != std::string::npos);
  assert(bitwise->find("bit_and") != std::string::npos);
  assert(bitwise->find("bit_or") != std::string::npos);
  assert(bitwise->find("bit_xor") != std::string::npos);
  assert(emit_cpp_source("fnc main(): int { ^ 12 ^^ 10; }") ==
         emit_cpp_source("fnc main(): int { return 12 ^^ 10; }"));
  assert(!emit_cpp_source("fnc main(): int { return 12 ^ 10; }"));
  const auto shift = emit_cpp_source("fnc main(): int { ^ 1 <<< 64; }");
  assert(shift && shift->find("E_SHIFT_DISTANCE") != std::string::npos);
  const auto traps = emit_cpp_source("fnc main(): int {return 3 / 0;}");
  assert(traps && traps->find("E_DIVZERO") != std::string::npos);
  assert(traps->find("return 71") != std::string::npos);
  const auto overflow = emit_cpp_source(
    "fnc main(): int {return 9223372036854775807 + 1;}");
  assert(overflow && overflow->find("E_OVERFLOW") != std::string::npos);
  assert(!emit_cpp_source("fnc main(): int {return 2;}; int malicious(){}"));
  assert(!emit_cpp_source("fnc main(): int {return system; }"));
  auto malformed = parse_source(source);
  assert(malformed.ok());
  malformed.nodes[0].op = Op::add;
  malformed.nodes[0].left = 0;
  assert(!emit_cpp(malformed));
  auto detached = parse_source(source);
  assert(detached.ok() && validate_ast(detached));
  detached.root = 0;
  assert(!validate_ast(detached));
  assert(!emit_cpp(detached));
  auto stray = parse_source(source);
  stray.nodes.push_back(Node{Op::literal, 0, 0, 100});
  stray.root = stray.nodes.size() - 1;
  assert(!validate_ast(stray));
  assert(!emit_cpp(stray));
  auto forged = parse_source(source);
  forged.nodes.back().op = static_cast<Op>(900);
  assert(!validate_ast(forged));
  assert(!emit_cpp(forged));
  const auto checked=parse_source("type R = int where self >= 1 and self <= 42; fnc main(): int {return 42::<R>;}");
  assert(checked.ok() && emit_cpp(checked));
  const std::string pre="type Bounded = int where self >= 1 and self <= 42; ";
  for(auto expression:{"40+2","84/2","42 as Bounded","-9223372036854775808"}){
    auto strict=parse_verified_strict_i64(pre+"pub fnc main(): int { return "+expression+"; }");
    assert(strict.ok() && validate_ast(strict) && emit_cpp(strict));
  }
  for(auto expression:{"42/0","-9223372036854775808 / -1","9223372036854775807+1","(9223372036854775807+1)*0","43 as Bounded"}){
    auto strict=parse_verified_strict_i64(pre+"pub fnc main(): int { return "+expression+"; }");
    assert(strict.error==ParseError::verify_unproven && !emit_cpp(strict));
  }
  assert(parse_verified_strict_i64("pub fnc main(): int {return 42;}").error==ParseError::verify_unsupported);
  assert(parse_verified_strict_i64(pre+"pub fnc main(): int {val int x = 9223372036854775807+1;return 42;}").error==ParseError::verify_unproven);
  const auto loop=parse_verified_strict_i64(
    "type Count = int where self >= 0 and self <= 42; "
    "pub fnc main() -> int { let Count x = 0; let Count y = 0; "
    "while x < 42 { x=x+1; y=x; if x==42 { break; } } return y; }");
  assert(loop.ok()&&emit_cpp(loop));
  auto invalid=loop;
  for(auto& node:invalid.nodes)if(node.op==Op::loop_group&&!node.exits.empty()){
    node.exits.front().values[0]=node.right;break;
  }
  assert(!validate_ast(invalid)&&!emit_cpp(invalid));
  auto bad_range=checked;bad_range.nodes.back().value=43;bad_range.nodes.back().upper=42;
  assert(!emit_cpp(bad_range));
}
