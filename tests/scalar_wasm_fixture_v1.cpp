#include "atto/scalar_wasm_v1.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using atto::forge::sandbox::compile_scalar_wasm;
static std::string program(const std::string& expr) {
  return "pub fnc main(): int { return " + expr + "; }";
}
int main(int argc, char** argv) {
  assert(argc == 2);
  fs::create_directories(argv[1]);
  const std::vector<std::string> inputs = {
    program("(4 + 5) * 6 + 1"),
    program("-1 >>> 1"),
    program("-9223372036854775808"),
    program("1 | 4 & 5"),
    program("~0"),
    program("-8 ashr 2"),
    program("1 <<< 63"),
  };
  for (std::size_t i=0; i<inputs.size(); ++i) {
    const auto module = compile_scalar_wasm(inputs[i]);
    assert(module && module->size() > 8 && module->size() < 16384);
    std::ofstream file(fs::path(argv[1]) / ("tenant"+std::to_string(i)+".wasm"), std::ios::binary);
    assert(file);
    file.write(reinterpret_cast<const char*>(module->data()), static_cast<std::streamsize>(module->size()));
    assert(file.good());
  }
  // Fail closed on overflow, invalid syntax, unsupported capabilities,
  // excessive source and malformed AST handled by the shared parser.
  assert(!compile_scalar_wasm(program("9223372036854775807 + 1")));
  assert(!compile_scalar_wasm(program("1 / 0")));
  assert(!compile_scalar_wasm("pub susp fnc main(): int { return 2; }"));
  assert(!compile_scalar_wasm("pub fnc main(): int { return fs.read(1); }"));
  assert(!compile_scalar_wasm(std::string(16385, ' ')));
}
