#include "compiler/frontend.hpp"
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
    reject("pub fnc main(): int { return 7 ^ 3; }","legacy ^/^^ operators are removed");
    reject("pub fnc main(): int { return 7 ^^ 3; }","legacy ^/^^ operators are removed");
    reject("pub fnc main(): int { return (2 + 3) % 2; }","legacy % operator is removed");
    reject("pub fnc hidden(): int { return 4 ^ 3; } pub fnc main(): int { return 7; }",
           "legacy ^/^^ operators are removed");
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
    std::cout<<"FORGE_LEGACY_OPERATOR_GRAMMAR_PASS positive=5 negative=21 atomic-neq=1\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"FORGE_LEGACY_OPERATOR_GRAMMAR_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
