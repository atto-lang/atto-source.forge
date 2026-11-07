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
}
int main() {
  try {
    accept("pub fnc main(): int { return (2 * 3) + 1; }");
    accept("pub fnc main(): int { /* % ^ ^^ */ return 7; }");
    accept("pub fnc main(): int { // % ^ ^^ in comment\n return 7; }");
    accept("pub fnc main(): int { /* operator true false */ return 7; }");
    reject("pub fnc main(): int { return 7 % 3; }","legacy % operator is removed");
    reject("pub fnc main(): int { return 7%3; }","legacy % operator is removed");
    reject("pub fnc main(): int { return 7 ^ 3; }","legacy ^/^^ operators are removed");
    reject("pub fnc main(): int { return 7 ^^ 3; }","legacy ^/^^ operators are removed");
    reject("pub fnc main(): int { return (2 + 3) % 2; }","legacy % operator is removed");
    reject("pub fnc hidden(): int { return 4 ^ 3; } pub fnc main(): int { return 7; }",
           "legacy ^/^^ operators are removed");
    // Reject prohibited declarations and implicit bool-to-int values even in
    // unused helper functions. These are bootstrap admission tests only.
    reject("pub fnc operator(a:int):int{return a;} pub fnc main():int{return 7;}",
           "user-defined operator overloading is forbidden");
    reject("define operator +(int a,int b):int{return a+b;} pub fnc main():int{return 7;}",
           "user-defined operator overloading is forbidden");
    reject("pub fnc main():int{return true;}",
           "no implicit bool-to-int conversion");
    reject("pub fnc hidden():int{return false;} pub fnc main():int{return 7;}",
           "no implicit bool-to-int conversion");
    std::cout<<"FORGE_LEGACY_OPERATOR_GRAMMAR_PASS positive=4 negative=10\\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"FORGE_LEGACY_OPERATOR_GRAMMAR_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
