#include "compiler/frontend.hpp"
#include "compiler/call_native.hpp"
#include <stdexcept>
#include <string>
#include <string_view>
#include <iostream>
namespace {
void accept(std::string_view program) {
  try { (void)oresforge::parse_source(program); }
  catch(const std::exception& e) {
    throw std::runtime_error(std::string("unexpected accepted-source failure: ")+e.what());
  }
}
void reject(std::string_view program,std::string_view expected) {
  try { (void)oresforge::parse_source(program); }
  catch(const std::exception& e) {
    const std::string message(e.what());
    if(message.find(expected)!=std::string::npos &&
       message.find("at byte ")!=std::string::npos) return;
    throw std::runtime_error("wrong negative diagnostic: "+message);
  }
  throw std::runtime_error("legacy operator silently accepted");
}
void not_ownership(std::string_view program) {
  try { (void)oresforge::parse_source(program); }
  catch(const std::exception& e) {
    if(std::string(e.what()).find("OWNERSHIP_BANG_BACKEND_UNSUPPORTED")!=std::string::npos)
      throw std::runtime_error("atomic !eq misclassified as an ownership bang");
  }
}
}
int main() {
  try {
    accept("pub fnc main(): int { return (2 * 3) + 1; }");
    accept("pub fnc main(): int { /* % ^ ^^ */ return 7; }");
    accept("pub fnc main(): int { // % ^ ^^ in comment\n return 7; }");
    reject("pub fnc main(): int { return 7 % 3; }","legacy % operator is removed");
    reject("pub fnc main(): int { return 7%3; }","legacy % operator is removed");
    // Both accepted spellings must lower to identical IR and real native
    // assembly; the callable lane must not create a different call ABI.
    const std::string ordinary="fnc helper(): int { return 41; } pub fnc main(): int { return helper() + 1; }";
    const std::string caret="fnc helper(): int { ^ 41; } pub fnc main(): int { ^ helper() + 1; }";
    const auto a=oresforge::parse_source(ordinary);
    const auto b=oresforge::parse_source(caret);
    if (oresforge::interpret(b)!=42 || oresforge::dump_ir(a)!=oresforge::dump_ir(b))
      throw std::runtime_error("scalar caret IR/return mismatch");
    const auto ca=oresforge::calls::parse(ordinary);
    const auto cb=oresforge::calls::parse(caret);
    if (oresforge::calls::interpret(cb,{})!=42)
      throw std::runtime_error("callable caret return mismatch");
    for (auto arch:{oresforge::Arch::X86_64,oresforge::Arch::ARM64}) {
      if (oresforge::emit_asm(a,arch)!=oresforge::emit_asm(b,arch) ||
          oresforge::calls::emit_asm(ca,arch)!=oresforge::calls::emit_asm(cb,arch))
        throw std::runtime_error("caret native assembly mismatch");
    }
    auto reject_callable=[](std::string_view source,std::string_view expected) {
      try { (void)oresforge::calls::parse(source); }
      catch (const std::exception& e) {
        if (std::string_view(e.what()).find(expected)!=std::string_view::npos) return;
        throw;
      }
      throw std::runtime_error("unexpected callable source acceptance");
    };
    reject_callable("pub fnc main(): int { return 7 ^ 3; }","single caret is only a return statement");
    reject_callable("pub fnc main(): int { return 7 ^^ 3; }","FORGE_XOR_BACKEND_UNSUPPORTED");
    reject_callable("fnc hidden(): int { return 7 ^^ 3; } pub fnc main(): int { ^ 42; }","FORGE_XOR_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return 5 / 2; }","FORGE_CHECKED_DIVISION_BACKEND_UNSUPPORTED");
    accept("pub fnc main(): int { ^ 42; }");
    accept("fnc helper(): int { ^ 41; } pub fnc main(): int { ^ helper() + 1; }");
    reject("pub fnc main(): int { return 7 ^ 3; }","single caret is only a return statement");
    reject("pub fnc main(): int { return 7 ^^ 3; }","FORGE_XOR_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return (2 + 3) % 2; }","legacy % operator is removed");
    reject("pub fnc hidden(): int { return 4 ^ 3; } pub fnc main(): int { return 7; }",
           "single caret is only a return statement");
    // Nested comments must not expose ownership-looking text to the
    // stage-0 lexer; both parsing and rejection remain depth-bounded.
    accept("pub fnc main(): int { /* outer /* borrow! mut item */ tail */ return 7; }");
    {
      std::string deep="pub fnc main(): int { ";
      for (int i=0;i<129;++i) deep+="/*";
      deep+="copy!42";
      for (int i=0;i<129;++i) deep+="*/";
      deep+=" return 7; }";
      reject(deep,"block comment nesting too deep");
    }
    // Ownership bang aliases are recognized, but the scalar bootstrap must
    // reject them deliberately until it has checked move/borrow/share IR.
    accept("pub fnc main(): int { /* copy! 42 */ return 7; }");
    reject("pub fnc main(): int { return copy!42; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return copy! 42; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return take!42; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return borrow! foo; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return borrow! mut foo; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return borrow!mut foo; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    // The verb is adjacent to '!': trivia afterward cannot launder a loan.
    reject("pub fnc main(): int { return borrow!\nmut foo; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return borrow!/* checked loan */mut foo; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return mut borrow!\nfoo; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return mut borrow!foo; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return share! foo; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    reject("pub fnc main(): int { return copy!eqfoo; }",
           "OWNERSHIP_BANG_BACKEND_UNSUPPORTED");
    not_ownership("pub fnc main(): int { return copy!eq foo; }");
    reject("pub fnc main(): int { return rt borrow mut foo; }",
           "runtime ownership operation requires typed lowering in Forge");
    reject("pub fnc main(): int { return rt mut borrow foo; }",
           "runtime ownership operation requires typed lowering in Forge");
    std::cout<<"FORGE_LEGACY_OPERATOR_GRAMMAR_PASS\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"FORGE_LEGACY_OPERATOR_GRAMMAR_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
