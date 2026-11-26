#include "compiler/alloc_experiment.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr std::string_view kSource =
  "define struct Point as int x int y end "
  "define pub fnc main(a:int,b:int):int { "
  "let p = Point { x: a, y: b }; return p.x + p.y; }";
void require(bool okay, const char* msg) {
  if (!okay) throw std::runtime_error(msg);
}
void reject(const std::string& source, const char* diagnostic) {
  try {
    (void)oresforge::alloc_experiment::parse(source);
  } catch (const std::exception& e) {
    if (std::string_view(e.what()).find(diagnostic) != std::string_view::npos) return;
    throw std::runtime_error(std::string("wrong rejection: ") + e.what());
  }
  throw std::runtime_error("unsafe allocation source was accepted");
}
}
int main() {
  try {
    using namespace oresforge::alloc_experiment;
    const auto program = parse(kSource);
    const auto baseline = emit_x86_64(program, false);
    const auto optimized = emit_x86_64(program, true);
    require(baseline.find("call malloc@PLT") != std::string::npos, "heap baseline must allocate");
    require(baseline.find("call free@PLT") != std::string::npos, "heap baseline must free");
    require(optimized.find("call malloc") == std::string::npos, "safe scalar must not allocate");
    require(optimized.find("call free") == std::string::npos, "safe scalar must not free");
    require(optimized.find("jo .Loresforge_alloc_overflow") != std::string::npos,
            "safe checked arithmetic must trap on overflow");
    require(dump_ir(program).find("closed-world verified") != std::string::npos,
            "MIR must state closed-world proof");
    const std::string prefix(kSource.substr(0, kSource.size()-1));
    reject(std::string(kSource) + " actor.send(p);", "unexpected trailing");
    reject(prefix + " return p.x; }", "unsupported allocation source grammar");
    reject(prefix + " await foo(); }", "unsupported allocation source grammar");
    reject(prefix + " p.x = b; }", "unsupported allocation source grammar");
    reject(std::string(kSource).replace(0, 6, "import"), "unsupported allocation source grammar");
    reject("define struct Point as int x int y end "
           "define pub fnc main(a:int,a:int):int { let p = Point { x:a,y:a };"
           "return p.x+p.y; }", "duplicate or shadowed");
    reject("define struct Point as int x int y end "
           "define pub fnc main(a:int,b:int):int { let p = Point { x: a, y: b };"
           "return p.x+p.z; }", "unsupported allocation field");
    reject("define struct Point as int x int y end "
           "define pub fnc main(a:int,b:int):int { let p = Point { x: a, y: b };"
           "return p.x+p.y+p.y; }", "unsupported allocation source grammar");
    reject(std::string(kSource) + std::string("\0extra", 6), "unsupported allocation source character");
    const std::string literals =
      "define struct Point as int x int y end "
      "define pub fnc main(a:int,b:int):int { "
      "let temporary = Point { x: -9, y: 10 }; return temporary.y + temporary.x; }";
    const auto lit_program = parse(literals);
    require(emit_x86_64(lit_program, true).find("movabsq $-9") != std::string::npos,
            "signed initializer lowering");
    require(emit_x86_64(lit_program, false).find("movabsq $10") != std::string::npos,
            "literal baseline lowering");
    require(dump_ir(lit_program).find("field1 field0") != std::string::npos,
            "read order preserved");
    std::cout << "closed-world allocation parser/codegen tests passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
