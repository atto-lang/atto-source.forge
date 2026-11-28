#include "compiler/frontend.hpp"
#include "compiler/readonly_native.hpp"
#include "../shared/oreslang/backend/CountedReadyGuest.hpp"
#include "compiler/call_native.hpp"
#include "compiler/source_input.hpp"
#include "compiler/ir.hpp"
#include "compiler/optimization_cli.hpp"
#include "compiler/alloc_experiment.hpp"
#include "codegen/native.hpp"
#include <array>
#include <charconv>
#include <cstdint>
#include <vector>
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
    auto optimization = oresforge::cli::parse_optimization_options(argc, argv);
    argc = static_cast<int>(optimization.positional.size());
    argv = optimization.positional.data();
    const bool alloc_asm = argc == 4 && std::string(argv[1]) == "emit-alloc-asm";
    oresforge::cli::enforce_optimization_support(optimization, alloc_asm);
    if (argc == 3 && std::string(argv[1]) == "ir-alloc") {
      const auto model = oresforge::alloc_experiment::parse(read_file(argv[2]));
      std::cout << oresforge::alloc_experiment::dump_ir(model);
      std::cout.flush();
      return 0;
    }
    if (alloc_asm) {
      const auto arch = oresforge::parse_arch(argv[2]);
      if (arch != oresforge::Arch::X86_64)
        throw std::runtime_error("allocation native profile supports only x86_64");
      const auto model = oresforge::alloc_experiment::parse(read_file(argv[3]));
      std::cout << oresforge::alloc_experiment::emit_x86_64(
          model, optimization.escape == "safe");
      std::cout.flush();
      return 0;
    }
    if (argc==3 && std::string(argv[1])=="--counted-ready-v1") {
      auto code=oreslang::counted_ready::compile(read_regular_source(argv[2],65536,"counted source exceeds limit"));
      std::cout<<code;
      std::cout.flush();
      return 0;
    }
    // Check the actual stage-0 scalar guest without publishing a result.
    // This subset is pure and zero-argument, so checked interpretation also
    // rejects constant arithmetic overflow, unlike an IR-only syntax check.
    if (argc==3 && std::string(argv[1])=="check") {
      auto program=oresforge::parse_source(read_file(argv[2]));
      (void)oresforge::interpret(program);
      return 0;
    }
    if (argc==3 && (std::string(argv[1])=="run" || std::string(argv[1])=="ir")) {
      auto program=oresforge::parse_source(read_file(argv[2]));
      if (std::string(argv[1])=="run") std::cout<<oresforge::interpret(program)<<'\n';
      else {
        // Today all admitted source expressions are compile-time i64 values.
        // Structural IR verification alone misses overflow: do not publish
        // known-invalid scalar IR just because the dump path skips execution.
        // Replace with typed constant-fold validation before supporting
        // nonconstant programs, rather than evaluating runtime effects.
        (void)oresforge::interpret(program);
        std::cout<<oresforge::dump_ir(program);
      }
      std::cout.flush();
      return 0;
    }
    // A separate stage-0 callable profile: true SysV native calls and lazy
    // runtime branches. Legacy scalar and runtime-entry modes remain intact.
    if (argc>=3 && (std::string(argv[1])=="run-calls" || std::string(argv[1])=="run-result-calls" || std::string(argv[1])=="run-option-calls" || std::string(argv[1])=="run-effect-calls")) {
      auto program=oresforge::calls::parse(read_file(argv[2]));
      std::vector<std::int64_t> args;
      for (int i=3;i<argc;++i) {
        const std::string_view text(argv[i]);
        std::int64_t value=0;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
          throw std::runtime_error("invalid callable i64 argument");
        args.push_back(value);
      }
      if(std::string(argv[1])=="run-effect-calls") {
        const auto result=oresforge::calls::interpret_effect(program,args);
        std::cout<<result.value<<' '<<result.tag<<' '<<static_cast<std::uint64_t>(result.effect)<<'\n';
      } else if(std::string(argv[1])=="run-result-calls") {
        const auto result=oresforge::calls::interpret_result(program,args);
        std::cout<<result.value<<' '<<static_cast<std::uint64_t>(result.status)<<'\n';
      } else if(std::string(argv[1])=="run-option-calls") {
        const auto result=oresforge::calls::interpret_option(program,args);
        std::cout<<result.value<<' '<<result.tag<<'\n';
      } else std::cout<<oresforge::calls::interpret(program,args)<<'\n';
      std::cout.flush();
      return 0;
    }
    if (argc==4 && std::string(argv[1])=="emit-call-asm") {
      const auto arch=oresforge::parse_arch(argv[2]);
      auto program=oresforge::calls::parse(read_file(argv[3]));
      std::cout<<oresforge::calls::emit_asm(program,arch);
      std::cout.flush();
      return 0;
    }
    if (argc>=3 && std::string(argv[1])=="run-runtime") {
      auto program=oresforge::parse_runtime_source(read_file(argv[2]));
      std::vector<std::int64_t> args;
      for (int i=3;i<argc;++i) {
        std::int64_t value=0;
        const std::string_view arg(argv[i]);
        auto result=std::from_chars(arg.data(),arg.data()+arg.size(),value);
        if (result.ec!=std::errc{} || result.ptr!=arg.data()+arg.size())
          throw std::runtime_error("invalid runtime i64 argument");
        args.push_back(value);
      }
      std::cout<<oresforge::interpret(program,args)<<'\n';
      std::cout.flush();
      return 0;
    }
    if (argc==3 && std::string(argv[1])=="ir-runtime") {
      auto program=oresforge::parse_runtime_source(read_file(argv[2]));
      std::cout<<oresforge::dump_ir(program);
      std::cout.flush();
      return 0;
    }
    if (argc==4 && std::string(argv[1])=="emit-readonly-asm") {
      const auto source=read_file(argv[3]);
      const auto assembly=oresforge::readonly_native::compile(source,oresforge::parse_arch(argv[2]));
      std::cout<<assembly;
      std::cout.flush();
      return 0;
    }
    if (argc==4 && std::string(argv[1])=="emit-runtime-asm") {
      auto target=oresforge::parse_arch(argv[2]);
      auto program=oresforge::parse_runtime_source(read_file(argv[3]));
      std::cout<<oresforge::emit_runtime_asm(program,target);
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
    std::cerr<<"usage: oresforge check <file.atto|file.ores> | run|ir <file.atto|file.ores> | emit-asm <arch> <file.atto|file.ores> | run-runtime <file.atto|file.ores> [i64-args...] | ir-runtime <file.atto|file.ores> | emit-runtime-asm <arch> <file.atto|file.ores> | run-calls|run-result-calls|run-option-calls|run-effect-calls <file.atto|file.ores> [i64-args...] | emit-readonly-asm <arch> <file.atto> | emit-call-asm x86_64 <file.atto|file.ores> | ir-alloc <file.atto|file.ores> | emit-alloc-asm x86_64 <file.atto|file.ores>\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr<<"oresforge: "<<e.what()<<'\n';
    return 1;
  }
}
