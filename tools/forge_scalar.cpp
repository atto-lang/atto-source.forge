#include "atto/scalar_emit.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using atto::forge::scalar::max_source_bytes;
using atto::forge::scalar::emit_cpp_source;
static int deny(const char* msg) { std::cerr << msg << '\n'; return 2; }

int main(int argc, char** argv) {
  if (argc != 4 || std::string(argv[2]) != "-o")
    return deny("E_USAGE: forge_scalar input.atto -o output.cpp");
  const fs::path input(argv[1]), output(argv[3]);
  if (input.extension() != ".atto" || output.extension() != ".cpp")
    return deny("E_PATH: expected .atto and .cpp");
  std::error_code ec;
  if (!fs::is_regular_file(input, ec) || ec)
    return deny("E_SOURCE: not a regular input file");
  ec.clear();
  const auto in_absolute = fs::absolute(input, ec);
  if (ec) return deny("E_PATH: invalid input");
  ec.clear();
  const auto out_absolute = fs::absolute(output, ec);
  if (ec || in_absolute.lexically_normal() == out_absolute.lexically_normal())
    return deny("E_PATH: source/output alias");
  ec.clear();
  if (fs::exists(output, ec)) {
    if (ec) return deny("E_PATH: invalid output");
    ec.clear();
    if (fs::equivalent(input, output, ec) && !ec)
      return deny("E_PATH: hard-link alias");
  }
  ec.clear();
  if (fs::is_symlink(output, ec) || ec)
    return deny("E_PATH: symlink output rejected");
  const fs::path temporary = output.string() + ".part";
  ec.clear();
  if (fs::is_symlink(temporary, ec) || ec)
    return deny("E_PATH: symlink temp rejected");
  ec.clear();
  const auto size = fs::file_size(input, ec);
  if (ec || size > max_source_bytes) {
    ec.clear(); fs::remove(output, ec);
    return deny("E_SOURCE_LIMIT");
  }
  std::ifstream file(input, std::ios::binary);
  if (!file) return deny("E_SOURCE: read failed");
  std::string source(static_cast<std::size_t>(size), '\0');
  if (!source.empty()) file.read(source.data(), static_cast<std::streamsize>(size));
  if (!file || file.peek() != std::char_traits<char>::eof()) {
    ec.clear(); fs::remove(output, ec);
    return deny("E_SOURCE: changing or truncated input");
  }
  const auto generated = emit_cpp_source(source);
  if (!generated) {
    ec.clear(); fs::remove(output, ec);
    return deny("E_SYNTAX: unsupported .atto scalar source");
  }
  std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
  if (!out) return deny("E_OUTPUT: open failed");
  out.write(generated->data(), static_cast<std::streamsize>(generated->size()));
  out.flush();
  const bool written = static_cast<bool>(out);
  out.close();
  if (!written || !out) {
    ec.clear(); fs::remove(temporary, ec);
    return deny("E_OUTPUT: incomplete write");
  }
  ec.clear();
  fs::rename(temporary, output, ec);
  if (ec) {
    ec.clear(); fs::remove(temporary, ec);
    return deny("E_OUTPUT: publication failed");
  }
  return 0;
}
