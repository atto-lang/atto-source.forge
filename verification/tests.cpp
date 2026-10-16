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
#include <string_view>
#include <type_traits>
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
void reject_diagnostic(const std::function<void()>& fn,std::string_view diagnostic) {
  try {
    fn();
  } catch (const std::exception& error) {
    if (std::string_view(error.what()).find(diagnostic)==std::string_view::npos)
      throw std::runtime_error("source rejected for wrong reason: "+
                               std::string(error.what()));
    return;
  }
  throw std::runtime_error("malformed source admitted unexpectedly");
}
oresforge::Program source(const std::string& expr) {
  return oresforge::parse_source("define pub fnc main() : int { return "+expr+"; }");
}
}
int main() {
  try {
    using namespace oresforge;
    // Import graph linking is not available in the Forge scalar bootstrap.
    // Reject with the *right* diagnostic before IR or assembly emission.
    for (const auto& path : {"+/src/helper", "+src/helper", "@/src/helper", "pkg:math/src/helper",
                             "./helper", "../helper", "common/helper",
                             ".hidden/helper"}) {
      reject_diagnostic([&] {
        parse_source(std::string("import fnc helper from ") +
                     static_cast<char>(34) + path + static_cast<char>(34) +
                     "; fnc main():int{return 42;}");
      }, "source imports require verified module graph lowering");
    }
    ensure(interpret(parse_source("fnc main():int{return 42;} // import ignored"))==42,
           "import in a comment does not trigger module admission");
    // Named-call and tagged pipeline lowering are still unavailable in
    // Forge's i64 bootstrap. Verify explicit rejection before codegen.
    for (const auto& expression : {
        "1 |> twice", "(1 |> twice)", "1+2 |> plus(3)",
        "1 |> Option.map(inc)", "1 |> Result.map_err(annotate)",
        "1 |> Option /*comment*/ . map(inc)",
        "1 |> plus(2 |> Result.map_err(f))"
    }) {
      reject_diagnostic([&] { source(expression); },
                        "value pipeline requires named callable lowering");
    }
    ensure(interpret(parse_source("fnc main():int{return 1;} // |> ignored"))==1,
           "value pipeline syntax inside comments must not be tokenized");
    ensure(interpret(source("2+3*4"))==14,"precedence");
    ensure(interpret(source("(2+3)*4"))==20,"parentheses");
    for (const auto& expression : {"3/2", "4/2", "5/0", "-9223372036854775808/-1"})
      reject_diagnostic([&] { source(expression); }, "float result lowering");
    // Internal checked-i64 IR division is distinct from the source / operator.
    Program internal_division{{{Op::Constant,-3},{Op::Constant,2},
                              {Op::Div,0,0,1},{Op::Return,0,2,-1}}};
    ensure(interpret(internal_division)==-1,"internal checked division");
    internal_division.instructions[1].value=0;
    reject_diagnostic([&] { interpret(internal_division); }, "division by zero");
    internal_division.instructions[0].value=std::numeric_limits<std::int64_t>::min();
    internal_division.instructions[1].value=-1;
    reject_diagnostic([&] { interpret(internal_division); }, "division overflow");
    ensure(interpret(source("-(3+2)"))==-5,"unary");
    ensure(interpret(parse_source("pub define fnc main():int { return 9; }"))==9,"pub define");
    ensure(interpret(parse_source("define pub fnc main() -> int { return 2+3*4; }"))==14,
           "canonical formatter arrow");
    reject([] { parse_source("fnc main() - > int { return 1; }"); });
    reject([] { parse_source("fnc main() ->> int { return 1; }"); });
    reject([] { source("9223372036854775808"); });
    ensure(interpret(source("-9223372036854775808"))==
               std::numeric_limits<std::int64_t>::min(),"i64 minimum");
    reject([] { interpret(source("--9223372036854775808")); });
    reject([] { interpret(source("-9223372036854775808/-1")); });
    reject([] { parse_source(std::string(1024 * 1024 + 1, ' ')); });
    // These are complete *valid* programs with invalid bytes inside comments.
    // A generic assertThrows would pass for an unrelated lexical error; require
    // the encoding-specific diagnostic even where lexing would ignore comments.
    const std::string comment="fnc main():int {return 1;} // ";
    reject_diagnostic([&] { parse_source(comment+
        static_cast<char>(0xc0)+static_cast<char>(0xaf)); },"not valid UTF-8");
    reject_diagnostic([&] { parse_source(comment+
        static_cast<char>(0xed)+static_cast<char>(0xa0)+static_cast<char>(0x80)); },
        "not valid UTF-8");
    reject_diagnostic([&] { parse_source(comment+
        static_cast<char>(0xe2)+static_cast<char>(0x82)); },"not valid UTF-8");
    reject_diagnostic([&] { parse_source(comment+
        static_cast<char>(0xf4)+static_cast<char>(0x90)+
        static_cast<char>(0x80)+static_cast<char>(0x80)); },"not valid UTF-8");
    reject_diagnostic([&] { parse_source(comment+
        static_cast<char>(0x80)); },"not valid UTF-8");
    reject_diagnostic([&] { std::string text=comment; text.push_back('\0');
        parse_source(text); },"source contains NUL byte");
    ensure(interpret(parse_source("fnc main():int {return 3;} // caf\xc3\xa9"))==3,
           "valid UTF-8 comment");

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
    reject_diagnostic([] { emit_asm(source("1"),static_cast<Arch>(99)); },
                      "unsupported architecture");
    jit::Executor executor({2,3});
    ensure(executor.run(source("1+2"))==3,"interpreter");
    ensure(executor.requested_tier()==jit::Tier::Interpreter,"tier0");
    executor.run(source("1+2"));
    ensure(executor.requested_tier()==jit::Tier::BaselineRequested,"tier1 request");
    executor.run(source("1+2"));
    ensure(executor.requested_tier()==jit::Tier::OptimizedRequested,"tier2 request");
    ensure(executor.effective_tier()==jit::Tier::Interpreter,"fallback honest");
    static_assert(!std::is_constructible_v<runtime::ActorRef,
                  std::uint64_t, std::uint64_t>,
                  "ActorRef must not be constructible from numeric IDs");
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
    runtime::Mailbox wrong(10,1,4);
    reject([&] { box.send(wrong.ref(),bytes); });
    runtime::Mailbox duplicate(9,1,4);
    reject([&] { box.send(duplicate.ref(),bytes); }); // same ID+generation
    box.close();
    reject([&] { box.send(old,bytes); });
    auto next=box.restart();
    ensure(next.generation>old.generation,"generation");
    ensure(box.queued()==0,"restart discarded all pending envelopes");
    reject([&] { box.send(old,bytes); });
    reject([&] { box.send(duplicate.ref(),bytes); });
    box.send(next,bytes);
    std::vector<std::uint8_t> huge(runtime::kMaxMessageBytes+1);
    reject([&] { box.send(next,huge); });
    box.close();
    ensure(box.queued()==0,"stop discarded pending owned envelopes");
    reject([&] { box.send(next,bytes); });
    auto newest=box.restart();
    reject([&] { box.send(next,bytes); });
    box.send(newest,bytes);
    ensure(box.queued()==1,"fresh capability works after second restart");
    std::cout<<"OresForge bootstrap tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr<<"FAILED: "<<error.what()<<'\n';
    return 1;
  }
}
