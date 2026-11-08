#include "compiler/frontend.hpp"
#include "compiler/ir.hpp"
#include <stdexcept>
#include <string_view>

int main() {
  using namespace oresforge;
  // Forge stage-0 can only execute a straight-line verified scalar expression.
  // It MUST reject loops, recursion and source-injected checkpoint pretenses
  // until a post-optimization safepoint pass and hard tenant quotas exist.
  for (const char* source : {
      "fnc main():int{while(true){return 1;}return 2;}",
      "fnc main():int{for(;;){return 1;}return 2;}",
      "fnc main():int{loop{return 1;}return 2;}",
      "fnc main():int{critical{while(true){}}return 1;}",
      "fnc unused():int{while(true){}} fnc main():int{return 1;}",
      "fnc main():int{return rt cooperate;}",
      "fnc main():int{return main();}"
  }) {
    bool rejected = false;
    try {
      auto p=parse_source(source);
      (void)interpret(p);
    } catch (const std::exception&) { rejected=true; }
    if (!rejected) throw std::runtime_error(
        "Forge admitted uninstrumented guest loop or recursive callable");
  }
  if (interpret(parse_source("fnc main():int{return 42;}"))!=42)
    throw std::runtime_error("Forge rejected supported straight-line guest");
}
