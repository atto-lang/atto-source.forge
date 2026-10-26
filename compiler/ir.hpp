#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace oresforge {
enum class Op { Constant, Parameter, Add, Sub, Mul, Div, Return };
struct Instruction {
  Op op;
  std::int64_t value = 0;
  int a = -1;
  int b = -1;
};
enum class SourceExportScope { File, Package, Public };
enum class SourceExportOrigin { ImplicitPub, ExplicitExport };
struct SourceExportBinding {
  std::string owner_namespace;
  std::string local_symbol_id;
  std::string local_name;
  std::string external_name;
  SourceExportScope scope = SourceExportScope::Public;
  SourceExportOrigin origin = SourceExportOrigin::ExplicitExport;
};

struct Program {
  std::vector<Instruction> instructions;
  // Stage-0 source-interface metadata. The bootstrap only understands fnc
  // families; this never changes native assembly symbol exposure.
  std::vector<SourceExportBinding> source_export_bindings;
  // Stage-0 native ABI: up to four signed i64 arguments, in source order.
  std::size_t parameter_count = 0;
};
void verify(const Program&);
std::int64_t interpret(const Program&);
std::int64_t interpret(const Program&,std::span<const std::int64_t> args);
std::string dump_ir(const Program&);
}
