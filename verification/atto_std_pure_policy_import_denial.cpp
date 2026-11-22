// Actual Forge C++ frontend regression: never reinterpret a source-only
// std/* policy file as a linked guest module. Run with CTest, not Node.
#include "compiler/frontend.hpp"
#include "compiler/call_native.hpp"
#include "compiler/ir.hpp"
#include <array>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
template <typename Parse>
void require_import_rejected(Parse parse, std::string_view prefix,
                             std::string_view context, bool stable_message) {
  std::string source(prefix);
  source += "\npub fnc main(): int { return 42; }\n";
  try {
    (void)parse(source);
  } catch (const std::exception& error) {
    if (stable_message &&
        std::string_view(error.what()).find(
          "source imports require verified module graph lowering")
            == std::string_view::npos) {
      throw std::runtime_error(std::string(context) +
                               ": unexpected frontend diagnostic: " + error.what());
    }
    return;
  }
  throw std::runtime_error(std::string(context) +
                           ": compiler silently ignored an unlinked import");
}
}

int main() {
  try {
    constexpr std::string_view cases[] = {
      "import module path from \"std/path\";",
      "import module path from \"std/path.atto\";",
      "import module sync from \"atto.std/sync\";",
      "import module telemetry from \"ores.std/telemetry.atto\";",
      "import module missing from \"std/unknown\";",
      "import module path from \"../stdlib/std/path.atto\";",
      "import module path from \"std/../path\";",
      "import module path from \"std/path\";\n"
         "pub fnc never_used(): int { return 1; }"
    };
    const auto scalar = [](std::string_view text) {
      return oresforge::parse_source(text);
    };
    const auto runtime = [](std::string_view text) {
      return oresforge::parse_runtime_source(text);
    };
    const auto callable = [](std::string_view text) {
      return oresforge::calls::parse(text);
    };
    for (const auto source : cases) {
      require_import_rejected(scalar, source, "scalar entry", true);
      require_import_rejected(runtime, source, "runtime entry", true);
      // Native callable stage is independent and must reject imports too.
      require_import_rejected(callable, source, "callable entry", false);
    }
    // Positive compiler controls: strings inside comments are not imports.
    constexpr std::string_view clean =
      "// import module ignored from \"std/path\";\n"
      "/* import module ignored from \"atto.std/sync\"; */\n"
      "pub fnc main(): int { return 42; }\n";
    // The callable stage currently admits line comments, but not the scalar
    // frontend's nested block-comment syntax. Keep its positive control
    // within the grammar it actually supports.
    constexpr std::string_view clean_callable =
      "// import module ignored from \\"std/path\\";\\n"
      "pub fnc main(): int { return 42; }\\n";
    if (oresforge::interpret(oresforge::parse_source(clean)) != 42 ||
        oresforge::interpret(oresforge::parse_runtime_source(clean)) != 42 ||
        oresforge::calls::interpret(oresforge::calls::parse(clean_callable), {}) != 42)
      throw std::runtime_error("commented import positive compiler control failed");
    std::cout << "FORGE_STD_NATIVE_FRONTEND_IMPORT_DENIAL=EXECUTED"
              << "|negative_cases=" << std::size(cases)
              << "|frontends=3|positive_controls=3|linked_std_guest=NOT_EXECUTED\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FORGE_STD_IMPORT_ADMISSION_FAILED: " << e.what() << '\n';
    return 1;
  }
}
