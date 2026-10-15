#include "compiler/frontend.hpp"
#include "compiler/source_input.hpp"
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
  return read_regular_source(path, 1024u * 1024u,
                             "source exceeds 1 MiB bootstrap limit");
}
}
int main(int argc,char** argv) {
  try {
    // Report failed publication (e.g. read-only stdout) as compilation failure.
    std::cout.exceptions(std::ios::badbit | std::ios::failbit);
    if (argc==3 && (std::string(argv[1])=="run" || std::string(argv[1])=="ir")) {
      auto program=oresforge::parse_source(read_file(argv[2]));
      if (std::string(argv[1])=="run") std::cout<<oresforge::interpret(program)<<'\n';
      else std::cout<<oresforge::dump_ir(program);
      std::cout.flush();
      return 0;
    }
    if (argc==4 && std::string(argv[1])=="emit-asm") {
      auto target=oresforge::parse_arch(argv[2]);
      auto program=oresforge::parse_source(read_file(argv[3]));
      std::cout<<oresforge::emit_asm(program,target);
      std::cout.flush();
      return 0;
    }
    std::cerr<<"usage: oresforge run|ir <file.ores> OR oresforge emit-asm x86_64|arm64 <file.ores>\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr<<"oresforge: "<<e.what()<<'\n';
    return 1;
  }
}
