#include "compiler/frontend.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
std::string load(const std::string& root, std::string_view name) {
  const std::string path = root + "/" + std::string(name);
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("missing .atto guest fixture: " + path);
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

void accept(const std::string& root, std::string_view name) {
  try { (void)oresforge::parse_source(load(root, name)); }
  catch (const std::exception& e) {
    throw std::runtime_error("positive scalar source rejected: " +
                             std::string(name) + ": " + e.what());
  }
}
void reject(const std::string& root, std::string_view name) {
  try { (void)oresforge::parse_source(load(root, name)); }
  catch (const std::exception& e) {
    const std::string error(e.what());
    if (error.find("Option/Result/trap requires verified tagged-sum lowering") !=
            std::string::npos &&
        error.find("at byte ") != std::string::npos) return;
    throw std::runtime_error("wrong rejection diagnostic: " +
                             std::string(name) + ": " + error);
  }
  throw std::runtime_error("unsupported guest source accepted: " +
                           std::string(name));
}
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    accept(argv[1], "comment-control.atto");
    accept(argv[1], "scalar-control.atto");
    for (const char* file : std::array{
      "trap-expression.atto", "trap-function.atto",
      "option-some.atto", "option-none.atto",
      "result-ok.atto", "result-err.atto",
      "constructor-some.atto", "constructor-none.atto",
      "constructor-ok.atto", "constructor-err.atto"
    }) reject(argv[1], file);
    std::cout << "FORGE_TRAP_SUM_FAIL_CLOSED positive=2 negative=10\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FORGE_TRAP_SUM_FAIL " << e.what() << '\n';
    return 1;
  }
}
