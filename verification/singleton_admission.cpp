#include "compiler/frontend.hpp"
#include "compiler/call_native.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr std::string_view diagnostic = "ORES-SINGLETON-UNSUPPORTED";
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template<class Parse> void check_profile(Parse parse) {
  const std::string main = "fnc main() -> int { return 42; }";
  for (const std::string_view prefix : std::array<std::string_view, 4>{
       "define ", "define pub ", "pub define ", ""}) {
    for (bool after_main : {false, true}) {
      const std::string declaration = std::string(prefix) +
          "singleton Settings as end\n";
      const std::string source = after_main ? main + declaration : declaration + main;
      bool rejected = false;
      try { parse(source); }
      catch (const std::runtime_error& e) {
        rejected = true;
        const std::string message = e.what();
        check(message.find(diagnostic) != std::string::npos,
              "singleton received an unclassified diagnostic");
        check(message.find("at byte " + std::to_string(source.find("singleton")))
                  != std::string::npos, "singleton diagnostic has wrong offset");
      }
      check(rejected, "unsupported singleton was admitted");
    }
  }
  // Admission is grammatical, not a substring ban over source text.
  parse("// define singleton Settings as end\n" + main);
  parse("fnc singleton() -> int { return 42; } fnc main() -> int { return singleton(); }");
  parse("fnc singleton_count() -> int { return 42; } fnc main() -> int { return singleton_count(); }");
}
}
int main() {
  try {
    check_profile([](std::string_view source) { (void)oresforge::parse_source(source); });
    check_profile([](std::string_view source) { (void)oresforge::parse_runtime_source(source); });
    check_profile([](std::string_view source) { (void)oresforge::calls::parse(source); });
    const auto source = "fnc main() -> int { return 42; }";
    check(oresforge::interpret(oresforge::parse_source(source)) == 42, "scalar regression");
    check(oresforge::interpret(oresforge::parse_runtime_source(source)) == 42, "runtime regression");
    check(oresforge::calls::interpret(oresforge::calls::parse(source), {}) == 42, "calls regression");
    std::cout << "PASS: 24 classified singleton rejections, 9 lexical controls, 3 execution controls\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
