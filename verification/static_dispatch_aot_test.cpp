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
  reject([]{(void)oresforge::calls::parse("fnc main():int{return unknown();}");},
         "unknown function");
  reject([]{(void)oresforge::calls::parse(
      "fnc one(n:int):int{return n;}fnc main():int{return one();}");},
         "function arity mismatch");
  reject([]{(void)oresforge::calls::parse(
      "fnc main():int{return 42;}fnc main():int{return 1;}");},
         "duplicate function");
  std::cout<<"FORGE_STATIC_DISPATCH_AOT_SOURCE_PROOF|functions=3|direct_calls=2|"
              "interpreter=42|negative_ir_cases=8\n";
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
      if(std::string(argv[2])!="--asm")throw std::runtime_error("invalid output mode");
      const auto verified=oresforge::calls::parse(source);
      std::cout<<oresforge::calls::emit_asm(verified,oresforge::Arch::X86_64);
      return 0;
    }
    verify_direct_aot(source);
    return 0;
  }catch(const std::exception& error){
    std::cerr<<"FORGE_STATIC_DISPATCH_AOT_FAIL: "<<error.what()<<'\n';
    return 1;
  }
}
