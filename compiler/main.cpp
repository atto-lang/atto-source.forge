#include "compiler/frontend.hpp"
#include "compiler/ir.hpp"
#include "codegen/native.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
namespace {
std::string read_file(const char* path) {
  std::ifstream f(path,std::ios::binary);
  if (!f) throw std::runtime_error(std::string("cannot open ")+path);
  constexpr std::size_t kMaxSourceBytes = 1024 * 1024;
  std::array<char,8192> buffer{};
  std::string text;
  while (f) {
    f.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
    auto count=f.gcount();
    if (count <= 0) break;
    if (static_cast<std::size_t>(count)>kMaxSourceBytes-text.size())
      throw std::runtime_error("source exceeds 1 MiB bootstrap limit");
    text.append(buffer.data(),static_cast<std::size_t>(count));
  }
  if (f.bad()) throw std::runtime_error("read failed");
  return text;
}
}
int main(int argc,char** argv) {
  try {
    if (argc==3 && (std::string(argv[1])=="run" || std::string(argv[1])=="ir")) {
      auto program=oresforge::parse_source(read_file(argv[2]));
      if (std::string(argv[1])=="run") std::cout<<oresforge::interpret(program)<<'\n';
      else std::cout<<oresforge::dump_ir(program);
      return 0;
    }
    if (argc==4 && std::string(argv[1])=="emit-asm") {
      auto target=oresforge::parse_arch(argv[2]);
      auto program=oresforge::parse_source(read_file(argv[3]));
      std::cout<<oresforge::emit_asm(program,target);
      return 0;
    }
    std::cerr<<"usage: oresforge run|ir <file.ores> OR oresforge emit-asm x86_64|arm64 <file.ores>\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr<<"oresforge: "<<e.what()<<'\n';
    return 1;
  }
}
