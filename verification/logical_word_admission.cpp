#include "compiler/frontend.hpp"
#include "compiler/call_native.hpp"
#include "compiler/ir.hpp"
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void accept_all(std::string_view program,std::int64_t expected) {
  const auto scalar=oresforge::parse_source(program);
  if (oresforge::interpret(scalar)!=expected)
    throw std::runtime_error("scalar interpreter result mismatch");
  const auto runtime=oresforge::parse_runtime_source(program);
  if (oresforge::interpret(runtime)!=expected)
    throw std::runtime_error("runtime-entry interpreter result mismatch");
  const auto callable=oresforge::calls::parse(program);
  if (oresforge::calls::interpret(callable,std::span<const std::int64_t>{})!=expected)
    throw std::runtime_error("callable interpreter result mismatch");
}
void reject_direct_ir() {
  // A public caller can bypass lexing and construct a Module by hand.
  // Require the verifier, interpreter and native emitter to reject it.
  const auto helper=oresforge::calls::parse(
    "fnc helper(): int { return 5; } pub fnc main(): int { return 7; }");
  const auto param=oresforge::calls::parse(
    "fnc helper(v: int): int { return v; } pub fnc main(): int { return 7; }");
  int rejected=0;
  auto check=[&](const oresforge::calls::Module& m, std::string_view location) {
    const auto must_reject=[&](auto execute, std::string_view entry) {
      try { execute(); }
      catch(const std::exception& e) {
        if (std::string_view(e.what()).find("reserved logical word") != std::string_view::npos) {
          ++rejected;
          return;
        }
        throw std::runtime_error(std::string(location)+": "+std::string(entry)+
                                 " unexpected diagnostic: "+e.what());
      }
      throw std::runtime_error(std::string(location)+": "+std::string(entry)+
                               " accepted reserved logical word in public IR");
    };
    must_reject([&]{oresforge::calls::verify(m);},"verify");
    must_reject([&]{(void)oresforge::calls::interpret(m,std::span<const std::int64_t>{});},
                "interpret");
    must_reject([&]{(void)oresforge::calls::emit_asm(m,oresforge::Arch::X86_64);},
                "emit_asm");
  };
  for(const std::string word: {"and","or","xor"}) {
    auto named=helper;
    named.functions.at(0).name=word;
    check(named,"function name");
    auto parameter=param;
    parameter.functions.at(0).parameters.at(0)=word;
    check(parameter,"parameter name");
  }
  if(rejected!=18)throw std::runtime_error("missing IR entrypoint rejection");
}
void reject_all(std::string_view program) {
  const auto check=[&](auto parse,std::string_view profile) {
    try {parse(program);}
    catch (const std::exception& e) {
      const std::string diagnostic(e.what());
      if (diagnostic.find("logical word operator requires typed bool/CFG lowering")!=std::string::npos &&
          diagnostic.find("at byte ")!=std::string::npos)
        return;
      throw std::runtime_error(std::string(profile)+": unexpected diagnostic: "+diagnostic);
    }
    throw std::runtime_error(std::string(profile)+": reserved logical word accepted");
  };
  check([](std::string_view s){(void)oresforge::parse_source(s);},"scalar");
  check([](std::string_view s){(void)oresforge::parse_runtime_source(s);},"runtime-entry");
  check([](std::string_view s){(void)oresforge::calls::parse(s);},"callable");
}
}
int main() {
  try {
    accept_all("pub fnc main(): int { return 7; }",7);
    accept_all("fnc android(): int { return 5; } pub fnc main(): int { return android()+2; }",7);
    accept_all("fnc candy(): int { return 5; } pub fnc main(): int { return candy()+2; }",7);
    accept_all("fnc origin(): int { return 5; } pub fnc main(): int { return origin()+2; }",7);
    accept_all("fnc xorValue(): int { return 5; } pub fnc main(): int { return xorValue()+2; }",7);
    accept_all("fnc AND(): int { return 5; } pub fnc main(): int { return AND()+2; }",7);
    accept_all("pub fnc main(): int { // and or xor appear only in comments\n return 7; }",7);
    int count=0;
    for(const std::string word: {"and","or","xor"}) {
      reject_all("fnc "+word+"(): int { return 1; } pub fnc main(): int { return 7; }"); ++count;
      reject_all("fnc helper("+word+": int): int { return 7; } pub fnc main(): int { return helper(2); }"); ++count;
      reject_all("pub fnc main(): int { return 1 "+word+" 2; }"); ++count;
      reject_all("fnc hidden(): int { return 1 "+word+" 2; } pub fnc main(): int { return 7; }"); ++count;
      reject_all("pub fnc main(): int { return "+word+"; }"); ++count;
      reject_all("pub fnc main(): int { return "+word+"(); }"); ++count;
    }
    reject_direct_ir();
    std::cout<<"FORGE_LOGICAL_WORD_FAIL_CLOSED_PASS positive=7 negative="<<count
             <<" profiles=3 direct_ir_rejections=18\n";
    return 0;
  } catch(const std::exception& e) {
    std::cerr<<"FORGE_LOGICAL_WORD_FAIL_CLOSED_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
