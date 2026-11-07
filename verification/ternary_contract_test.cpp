#include "compiler/call_native.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void expect(bool valid, const char* message) {
  if (!valid) throw std::runtime_error(message);
}
void accepts(std::string_view source, std::int64_t result) {
  const auto module=oresforge::calls::parse(source);
  expect(oresforge::calls::interpret(module,{})==result,"ternary scalar mismatch");
  const auto asm_text=oresforge::calls::emit_asm(module,oresforge::Arch::X86_64);
  expect(asm_text.find("je .Lores_else_")!=std::string::npos,
         "native ternary must emit a conditional branch");
}
void rejects(std::string_view source, std::string_view diagnostic) {
  try {
    (void)oresforge::calls::parse(source);
  } catch(const std::exception& error) {
    if(std::string_view(error.what()).find(diagnostic)!=std::string_view::npos)
      return;
    throw std::runtime_error("unexpected rejection: "+std::string(error.what()));
  }
  throw std::runtime_error("invalid ternary was accepted");
}
void rejects_verifier(const oresforge::calls::Module& m, std::string_view diagnostic) {
  try {
    oresforge::calls::verify(m);
  } catch(const std::exception& error) {
    if(std::string_view(error.what()).find(diagnostic)!=std::string_view::npos) return;
    throw std::runtime_error("wrong verifier diagnostic: "+std::string(error.what()));
  }
  throw std::runtime_error("unsafe mutated callable IR admitted");
}
}
int main() {
  try {
    // Same five scalar source vectors as Java and LLVM's
    // contracts/v1/conditional-expression-v1.json (Git blob e3da61d86e55...).
    accepts("pub fnc main(): int { return true ? 42 : 7; }",42);
    accepts("pub fnc main(): int { return false ? 42 : 7; }",7);
    accepts("pub fnc main(): int { return false ? 1 : true ? 42 : 3; }",42);
    accepts("pub fnc main(): int { return 20 + 1 == 21 ? 42 : 7; }",42);
    rejects("pub fnc main(): int { return 1 ? 42 : 7; }","condition_not_bool");

    // Errors in a nonselected arm must not execute at runtime; both arms
    // are nevertheless checked by parse/verify.
    accepts("fnc main():int{return true ? 42 : (9223372036854775807+1);}",42);
    rejects("fnc main():int{return true ? 42 : false;}","Boolean is not an int");
    rejects("fnc main():int{return true+1;}","Boolean is not an int");
    rejects("fnc main():int{return true;}","Boolean is not an int");
    rejects("fnc main():int{return false ? 1 : 2 ? 3 : 4;}","condition_not_bool");
    rejects("fnc main():int{return true ? absent() : 42;}","unknown function");
    rejects("fnc main(true:int):int{return 42;}","boolean literal cannot be identifier");
    rejects("fnc true():int{return 1;} fnc main():int{return 42;}","boolean literal cannot be identifier");

    auto invalid=oresforge::calls::parse("fnc main():int{return true ? 42 : 7;}");
    invalid.functions.at(0).nodes.at(0).value=2;
    rejects_verifier(invalid,"invalid Boolean literal");
    invalid=oresforge::calls::parse("fnc main():int{return true ? 42 : 7;}");
    invalid.functions.at(0).nodes.at(0).kind=oresforge::calls::Kind::Constant;
    rejects_verifier(invalid,"condition_not_bool");
    invalid=oresforge::calls::parse("fnc main():int{return true ? 42 : 7;}");
    invalid.functions.at(0).nodes.at(1).kind=oresforge::calls::Kind::Boolean;
    invalid.functions.at(0).nodes.at(1).value=1;
    rejects_verifier(invalid,"conditional branch is not int");
    std::cout<<"FORGE_TERNARY_CONTRACT_PASS vectors=5 negative=8 mutations=3\n";
    return 0;
  } catch(const std::exception& e) {
    std::cerr<<"FORGE_TERNARY_CONTRACT_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
