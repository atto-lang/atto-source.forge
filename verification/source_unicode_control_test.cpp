#include "compiler/frontend.hpp"
#include "compiler/source_utf8.hpp"
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <iostream>

int main() {
  try {
    constexpr std::string_view safe = "pub fnc main(): int { return 42; }";
    oresforge::source::validate_utf8("/* café */ pub fnc main(): int { return 42; }");
    (void)oresforge::parse_source(safe);
    constexpr std::array<std::string_view,15> forbidden{{
      "\\xC2\\x85", "\\xD8\\x9C", "\\xE2\\x80\\x8E",
      "\\xE2\\x80\\x8F", "\\xE2\\x80\\xA8", "\\xE2\\x80\\xA9",
      "\\xE2\\x80\\xAA", "\\xE2\\x80\\xAB", "\\xE2\\x80\\xAC",
      "\\xE2\\x80\\xAD", "\\xE2\\x80\\xAE", "\\xE2\\x81\\xA6",
      "\\xE2\\x81\\xA7", "\\xE2\\x81\\xA8", "\\xE2\\x81\\xA9"
    }};
    std::size_t rejected = 0;
    for (const auto value : forbidden) {
      for (const auto prefix : {"/* hidden", "// hidden"}) {
        std::string source(prefix);
        source.append(value);
        source += std::string(prefix[1]=='*' ? " */ " : "\\n ") + std::string(safe);
        try {
          (void)oresforge::parse_source(source);
        } catch (const std::exception& error) {
          if (std::string_view(error.what()).find("forbidden Unicode control") == std::string_view::npos)
            throw;
          ++rejected;
          continue;
        }
        throw std::runtime_error("unsafe Unicode control admitted in source");
      }
    }
    if (rejected!=forbidden.size()*2) throw std::runtime_error("incomplete control coverage");
    std::cout << "FORGE_SOURCE_UNICODE_CONTROL_PASS negative=" << rejected << "\\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FORGE_SOURCE_UNICODE_CONTROL_FAIL " << error.what() << "\\n";
    return 1;
  }
}
