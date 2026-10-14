#include "compiler/frontend.hpp"
#include "compiler/ir.hpp"
#include "codegen/native.hpp"
#include "runtime/actor.hpp"
#include "jit/tiered.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
void ensure(bool yes,const char* message) {
  if (!yes) throw std::runtime_error(message);
}
void reject(const std::function<void()>& fn) {
  bool caught=false;
  try { fn(); } catch (const std::exception&) { caught=true; }
  ensure(caught,"expected rejection");
}
oresforge::Program source(const std::string& expr) {
  return oresforge::parse_source("define pub fnc main() : int { return "+expr+"; }");
}
}
int main() {
  try {
    using namespace oresforge;
    ensure(interpret(source("2+3*4"))==14,"precedence");
    ensure(interpret(source("(2+3)*4"))==20,"parentheses");
    ensure(interpret(source("-3/2"))==-1,"division rounding");
    ensure(interpret(source("-(3+2)"))==-5,"unary");
    ensure(interpret(parse_source("pub define fnc main():int { return 9; }"))==9,"pub define");
    reject([] { source("9223372036854775808"); });
    ensure(interpret(source("-9223372036854775808"))==
               std::numeric_limits<std::int64_t>::min(),"i64 minimum");
    reject([] { interpret(source("--9223372036854775808")); });
    reject([] { interpret(source("-9223372036854775808/-1")); });
    reject([] { parse_source(std::string(1024 * 1024 + 1, ' ')); });
    reject([] { source(std::string(200, '(') + "1" + std::string(200, ')')); });
    reject([] { source(std::string(200, '-') + "1"); });
    reject([] {
      std::string expr="1";
      for (int i=0; i<50000; ++i) expr += "+1";
      source(expr);
    });
    reject([] {
      Program too_large;
      too_large.instructions.resize(100001, {Op::Constant,0});
      verify(too_large);
    });
    reject([] { interpret(source("9223372036854775807 + 1")); });
    reject([] { interpret(source("3/0")); });
    reject([] { parse_source("fnc main():int {return 1;} garbage"); });
    reject([] { parse_source("fnc main():int {return x;}"); });
    reject([] { parse_source("pub pub fnc main():int {return 1;}"); });
    Program broken{{{Op::Add,0,0,0},{Op::Return,0,0,-1}}};
    reject([&] { verify(broken); });
    Program missing{{{Op::Constant,3}}};
    reject([&] { verify(missing); });
    ensure(dump_ir(source("2+3*4")).find("mul.i64.checked")!=std::string::npos,"IR dump");
    ensure(emit_asm(source("2+3*4"),Arch::X86_64).find("movabs $14")!=std::string::npos,"x86_64 asm");
    ensure(emit_asm(source("2+3*4"),Arch::ARM64).find("movz x0, #14")!=std::string::npos,"arm64 asm");
    reject([] { emit_asm(source("1/0"),Arch::X86_64); });
    jit::Executor executor({2,3});
    ensure(executor.run(source("1+2"))==3,"interpreter");
    ensure(executor.requested_tier()==jit::Tier::Interpreter,"tier0");
    executor.run(source("1+2"));
    ensure(executor.requested_tier()==jit::Tier::BaselineRequested,"tier1 request");
    executor.run(source("1+2"));
    ensure(executor.requested_tier()==jit::Tier::OptimizedRequested,"tier2 request");
    ensure(executor.effective_tier()==jit::Tier::Interpreter,"fallback honest");
    runtime::Mailbox box(9,1,4);
    auto old=box.ref();
    std::array<std::uint8_t,3> bytes={1,2,3};
    box.send(old,bytes);
    bytes[0]=99;
    reject([&] { box.send(old,bytes); });
    ensure(box.queued()==1,"capacity");
    auto received=box.receive();
    ensure(received && received->at(0)==1,"deep copy");
    ensure(!box.receive(),"empty");
    reject([&] { box.send({10,old.generation},bytes); });
    box.close();
    reject([&] { box.send(old,bytes); });
    auto next=box.restart();
    ensure(next.generation>old.generation,"generation");
    reject([&] { box.send(old,bytes); });
    box.send(next,bytes);
    std::vector<std::uint8_t> huge(runtime::kMaxMessageBytes+1);
    reject([&] { box.send(next,huge); });
    std::cout<<"OresForge bootstrap tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr<<"FAILED: "<<error.what()<<'\n';
    return 1;
  }
}
