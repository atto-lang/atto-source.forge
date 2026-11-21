#include "compiler/call_native.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using oresforge::calls::Kind;
using oresforge::calls::Module;
using oresforge::calls::Node;

template<class F>
void reject(F&& action, const std::string& fragment) {
  try {
    std::forward<F>(action)();
  } catch(const std::runtime_error& error) {
    if(std::string(error.what()).find(fragment)!=std::string::npos) return;
    throw std::runtime_error("wrong dispatch rejection: "+std::string(error.what())+
                             " (expected "+fragment+")");
  }
  throw std::runtime_error("unsafe static dispatch IR accepted: "+fragment);
}

Node& inc_call(Module& program) {
  for(auto& function : program.functions)
    for(auto& node : function.nodes)
      if(node.kind==Kind::Call && node.name=="inc") return node;
  throw std::runtime_error("canonical .atto fixture lacks inc call");
}

void verify_direct_aot(const std::string& source) {
  const Module admitted=oresforge::calls::parse(source);
  oresforge::calls::verify(admitted);
  if(admitted.functions.size()!=3 || admitted.entry!=2 ||
     admitted.functions[0].name!="inc" ||
     admitted.functions[1].name!="twice" ||
     admitted.functions[2].name!="main")
    throw std::runtime_error("unexpected compiler-owned source declaration order");
  if(oresforge::calls::interpret(admitted,std::span<const std::int64_t>{})!=42)
    throw std::runtime_error("real Forge callable interpreter returned wrong value");
  const std::string asmText=oresforge::calls::emit_asm(admitted,oresforge::Arch::X86_64);
  for(const int callee : {0,1}) {
    if(asmText.find("  call .Lores_fn_"+std::to_string(callee)+"\n")==std::string::npos)
      throw std::runtime_error("known call did not lower to a direct native symbol");
  }
  if(asmText.find("  call *")!=std::string::npos ||
     asmText.find("  jmp *")!=std::string::npos)
    throw std::runtime_error("closed scalar callable emitted indirect branch");
  // Fuel must be initialized once per exported entry and decremented for
  // every evaluated AST node, including work reached by recursive direct calls.
  // Without this, bounded call depth still permits exponential native work.
  if(asmText.find("  movq $200000, %r9\n")==std::string::npos ||
     asmText.find("  testq %r9, %r9\n  je .Lores_budget\n  decq %r9\n")
       ==std::string::npos ||
     asmText.find(".Lores_budget:\n  ud2\n")==std::string::npos)
    throw std::runtime_error("native callable execution-fuel budget missing");

  {
    Module bad=admitted;
    inc_call(bad).callee=1;
    reject([&]{oresforge::calls::verify(bad);},"callable target/name mismatch");
    reject([&]{(void)oresforge::calls::emit_asm(bad,oresforge::Arch::X86_64);},
           "callable target/name mismatch");
  }
  {
    Module bad=admitted;
    inc_call(bad).callee=999;
    reject([&]{oresforge::calls::verify(bad);},"invalid callable target");
  }
  {
    Module bad=admitted;
    bad.functions[0].name="inc$forged";
    reject([&]{oresforge::calls::verify(bad);},"duplicate or invalid function");
  }
  {
    Module bad=admitted;
    bad.functions[0].parameters[0]="n$forged";
    reject([&]{oresforge::calls::verify(bad);},"invalid or duplicate callable parameter");
  }
  {
    Module bad=admitted;
    bad.functions[0].parameters.push_back("n");
    reject([&]{oresforge::calls::verify(bad);},"invalid or duplicate callable parameter");
  }
  {
    Module bad=admitted;
    bad.entry=0; // entry index is not authority to execute an arbitrary function
    reject([&]{oresforge::calls::verify(bad);},"callable entry is not main");
    reject([&]{(void)oresforge::calls::emit_asm(bad,oresforge::Arch::X86_64);},
           "callable entry is not main");
  }
  {
    Module bad=admitted;
    inc_call(bad).name="twice"; // same valid symbol but wrong compiler-owned slot
    reject([&]{oresforge::calls::verify(bad);},"callable target/name mismatch");
  }
  {
    Module bad=admitted;
    auto& call=inc_call(bad);
    const int shared=call.children.front();
    call.children.push_back(shared); // shared AST node + invalid arity
    reject([&]{oresforge::calls::verify(bad);},"callable AST shared node");
  }
  reject([]{(void)oresforge::calls::parse("fnc main():int{return unknown();}");},
         "unknown function");
  reject([]{(void)oresforge::calls::parse(
      "fnc one(n:int):int{return n;}fnc main():int{return one();}");},
         "function arity mismatch");
  reject([]{(void)oresforge::calls::parse(
      "fnc main():int{return 42;}fnc main():int{return 1;}");},
         "duplicate function");
  // The exported C ABI symbol oresforge_entry() has zero parameters.
  // A parameterized main must be rejected in both parser and forged IR.
  reject([]{(void)oresforge::calls::parse("fnc main(x:int):int{return x;}");},
         "callable entry must have zero parameters");
  {
    Module bad=admitted;
    bad.functions[static_cast<std::size_t>(bad.entry)].parameters.push_back("forged");
    reject([&]{oresforge::calls::verify(bad);},
           "callable entry must have zero parameters");
    reject([&]{(void)oresforge::calls::emit_asm(bad,oresforge::Arch::X86_64);},
           "callable entry must have zero parameters");
  }
  // These strings are lexically valid but source-reserved. Forged Module
  // objects must never bypass source admission by supplying them directly.
  {
    Module bad=admitted;
    bad.functions[0].name="int";
    reject([&]{oresforge::calls::verify(bad);},"duplicate or invalid function");
  }
  {
    Module bad=admitted;
    bad.functions[0].parameters[0]="return";
    reject([&]{oresforge::calls::verify(bad);},
           "invalid or duplicate callable parameter");
  }
  {
    Module bad=admitted;
    inc_call(bad).value=42;
    reject([&]{oresforge::calls::verify(bad);},
           "unexpected callable numeric payload");
    reject([&]{(void)oresforge::calls::emit_asm(bad,oresforge::Arch::X86_64);},
           "unexpected callable numeric payload");
  }
  {
    Module bad=admitted;
    auto& inc=bad.functions[0];
    inc.nodes[static_cast<std::size_t>(inc.result)].value=42;
    reject([&]{oresforge::calls::verify(bad);},
           "unexpected callable numeric payload");
  }
  reject([]{(void)oresforge::calls::parse(
      "fnc int():int{return 42;}fnc main():int{return 0;}");
  },"reserved function name");
  reject([]{(void)oresforge::calls::parse(
      "fnc helper(return:int):int{return 42;}fnc main():int{return 0;}");
  },"invalid or duplicate parameter");
  std::cout<<"FORGE_STATIC_DISPATCH_AOT_SOURCE_PROOF|functions=3|direct_calls=2|"
              "interpreter=42|negative_source_ir_cases=19\n";
}
}

int main(int argc,char** argv) {
  try {
    if(argc!=2 && argc!=3)
      throw std::runtime_error("usage: test DIRECT_42.atto [--asm]");
    std::ifstream in(argv[1],std::ios::binary);
    if(!in)throw std::runtime_error("missing canonical .atto fixture");
    const std::string source(std::istreambuf_iterator<char>{in},
                             std::istreambuf_iterator<char>{});
    if(argc==3) {
      const std::string mode(argv[2]);
      const auto verified=oresforge::calls::parse(source);
      if(mode=="--check-42") {
        // Independent host interpreter oracle for the same source that the
        // native guest script later assembles, links, and actually executes.
        if(oresforge::calls::interpret(
             verified,std::span<const std::int64_t>{})!=42)
          throw std::runtime_error("callable guest oracle expected exactly 42");
        const auto native=oresforge::calls::emit_asm(
            verified,oresforge::Arch::X86_64);
        if(native.find("  call .Lores_fn_")==std::string::npos ||
           native.find("  call *")!=std::string::npos ||
           native.find("  jmp *")!=std::string::npos)
          throw std::runtime_error("native callable dispatch must be direct");
        std::cout<<"FORGE_STAGE0_NATIVE_CALL_ORACLE|value=42|functions="
                 <<verified.functions.size()<<'\n';
        return 0;
      }
      if(mode=="--asm") {
        std::cout<<oresforge::calls::emit_asm(
            verified,oresforge::Arch::X86_64);
        return 0;
      }
      throw std::runtime_error("invalid output mode");
    }
    verify_direct_aot(source);
    return 0;
  }catch(const std::exception& error){
    std::cerr<<"FORGE_STATIC_DISPATCH_AOT_FAIL: "<<error.what()<<'\n';
    return 1;
  }
}
