#include "compiler/nominal_scalar.hpp"
#include "compiler/source_input.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

// Deliberately separate opt-in bounded nominal profile. Do not change the
// current forge_scalar compiler or advertise general class/actor support.
int main(int argc, char** argv) {
  try {
    std::cout.exceptions(std::ios::badbit | std::ios::failbit);
    const bool run = argc == 3 && std::string_view(argv[1]) == "run-class";
    const bool emit = argc == 4 && std::string_view(argv[1]) == "emit-class-asm";
    if (!run && !emit) {
      std::cerr << "usage: forge_nominal run-class input.atto | emit-class-asm x86_64 input.atto\n";
      return 2;
    }
    if (emit && std::string_view(argv[2]) != "x86_64")
      throw std::runtime_error("bounded nominal class codegen supports only x86_64 ELF");
    const auto* path = argv[run ? 2 : 3];
    const auto ext = std::filesystem::path(path).extension();
    if (ext != ".atto" && ext != ".ores")
      throw std::runtime_error("expected .atto input (legacy .ores accepted)");
    const auto program = oresforge::nominal::parse(
        read_regular_source(path, 1024u * 1024u, "nominal source exceeds 1 MiB limit"));
    if (run) std::cout << oresforge::nominal::interpret(program) << '\n';
    else std::cout << oresforge::nominal::emit_x86_64_asm(program);
    std::cout.flush();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "forge_nominal: " << e.what() << '\n';
    return 1;
  }
}
