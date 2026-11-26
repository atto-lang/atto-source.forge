#include "compiler/frontend.hpp"
#include "codegen/native.hpp"
#include <stdexcept>
#include <string>
#include <string_view>
using namespace oresforge;
static void require(bool b) { if (!b) throw std::runtime_error("export regression"); }
int main() {
  const std::string main="fnc main(): int { return 42; }\n";
  const auto baseline=parse_source(main);
  for (const auto& src : {std::string("export pub main;\n")+main,
      main+"export pub fnc main;", "export pub main;\n"+main+"export pub fnc main;"}) {
    const auto p=parse_source(src);
    require(interpret(p)==42);
    require(dump_ir(p)==dump_ir(baseline));
    for(auto arch:{Arch::X86_64,Arch::ARM64}) require(emit_asm(p,arch)==emit_asm(baseline,arch));
  }
  for(const auto& src:{std::string("export pub[pkg] main;\n")+main,
      main+"export pub fnc helper;", main+"export pub class main;",
      "export pub main;\n"+main+"export pub[pkg] main;",
      std::string("fnc main(): int { export pub main; return 42; }"),
      std::string("export pub main; fnc helper(): int { return 42; }"),
      main+"export pub main as other;", main+"export pub main.x;"}) {
    bool denied=false;
    try { (void)parse_source(src); } catch(const std::runtime_error&) { denied=true; }
    require(denied);
  }
  std::string budget;
  for(int i=0;i<128;++i) budget+="export pub main;\n";
  require(interpret(parse_source(budget+main))==42);
  bool denied=false;
  try { (void)parse_source(budget+main+"export pub main;"); }
  catch(const std::runtime_error&) { denied=true; }
  require(denied);
}
