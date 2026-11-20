#include "compiler/frontend.hpp"
#include "compiler/ir.hpp"
#include "codegen/native.hpp"
#include <array>
#include <cstdint>
#include <cassert>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace oresforge;

static void deny(std::string_view code,std::string_view input) {
  for (bool runtime : {false,true}) {
    try {
      (void)(runtime ? parse_runtime_source(input) : parse_source(input));
    } catch (const std::exception& e) {
      assert(std::string_view(e.what()).find(code)!=std::string_view::npos);
      continue;
    }
    throw std::runtime_error(std::string("unsupported export admitted: ")+
                             std::string(code));
  }
}
static const SourceExportBinding* binding(const Program& p,
                                          std::string_view external) {
  for (const auto& b:p.source_export_bindings)
    if (b.external_name==external) return &b;
  return nullptr;
}
int main() {
  const std::string base =
    "fnc helper():int{return 42;} pub fnc main():int{return helper();}";
  const auto original=parse_source(base);
  assert(interpret(original)==42);
  assert(original.source_export_bindings.size()==1);
  assert(binding(original,"main")!=nullptr);

  // Exactly identical exports are idempotent, independent of order/typed
  // spelling, and explicit export of inline pub is redundant.
  const auto before=parse_source(
    "export fnc helper; export pub fnc helper; "
    "export fnc main; export fnc main; "+base);
  const auto after=parse_source(
    base+" export fnc helper; export fnc helper; export pub main;");
  assert(before.source_export_bindings.size()==2);
  assert(binding(before,"helper") && binding(after,"helper"));
  assert(binding(before,"main") && binding(after,"main"));
  assert(interpret(before)==42 && interpret(after)==42);
  assert(dump_ir(before)==dump_ir(original));
  assert(emit_asm(before,Arch::X86_64)==emit_asm(original,Arch::X86_64));
  assert(emit_asm(after,Arch::ARM64)==emit_asm(original,Arch::ARM64));

  const auto alias=parse_source(
    "export fnc helper as public_helper; "+base);
  assert(binding(alias,"public_helper"));
  assert(binding(alias,"helper")==nullptr);
  assert(alias.source_export_bindings.size()==2);
  assert(binding(alias,"public_helper")->local_symbol_id=="$root::fnc::helper");
  assert(binding(alias,"public_helper")->scope==SourceExportScope::Public);
  assert(emit_asm(alias,Arch::X86_64)==emit_asm(original,Arch::X86_64));
  assert(emit_asm(alias,Arch::ARM64)==emit_asm(original,Arch::ARM64));
  assert(emit_asm(alias,Arch::X86_64).find("public_helper")==std::string::npos);

  // Trailing group comma and untyped inference are both allowed.
  const auto grouped=parse_source(
    "export pub fnc {helper,main,}; "+base);
  assert(grouped.source_export_bindings.size()==2);
  const auto scoped=parse_source(
    "export[pkg] fnc helper; fnc helper():int{return 42;} "
    "fnc main():int{return helper();}");
  assert(binding(scoped,"helper") &&
         binding(scoped,"helper")->scope==SourceExportScope::Package);
  assert(emit_asm(scoped,Arch::X86_64)==emit_asm(original,Arch::X86_64));
  const auto inline_scoped=parse_source(
    "pub[pkg] fnc helper():int{return 42;} "
    "export pub[pkg] fnc helper; fnc main():int{return helper();}");
  assert(inline_scoped.source_export_bindings.size()==1);
  assert(binding(inline_scoped,"helper")->scope==SourceExportScope::Package);
  assert(interpret(inline_scoped)==42);

  // Runtime-argument and main zero-argument profiles agree on export rules.
  const auto runtime=parse_runtime_source(
    "export pub fnc helper; fnc helper(x:int):int{return x;} "
    "fnc main(v:int):int{return helper(v);}");
  assert(interpret(runtime,std::array<std::int64_t,1>{42})==42);
  assert(binding(runtime,"helper"));
  assert(emit_runtime_asm(runtime,Arch::X86_64).find("helper")==std::string::npos);

  deny("EXPORT_ALIAS_COLLISION",
    "export fnc a as api; export fnc b as api; "
    "fnc a():int{return 1;} fnc b():int{return 2;}"
    "fnc main():int{return a();}");
  deny("EXPORT_SCOPE_CONFLICT",
    "export fnc helper; export[pkg] fnc helper; "+base);
  deny("EXPORT_SCOPE_CONFLICT",
    "export[pkg] fnc main; "+base);
  deny("EXPORT_SCOPE_CONFLICT",
    "pub[pkg] fnc helper():int{return 42;} "
    "export fnc helper; fnc main():int{return helper();}");
  deny("EXPORT_PRIVATE_TARGET",
    "private fnc helper():int{return 42;} export fnc helper; "
    "fnc main():int{return helper();}");
  deny("EXPORT_UNKNOWN_TARGET",
    "export fnc absent; fnc main():int{return 42;}");
  deny("EXPORT_KIND_MISMATCH",
    "export class helper; "+base);
  deny("EXPORT_BACKEND_UNSUPPORTED_KIND",
    "export pub actor Undeclared; fnc main():int{return 42;}");
  deny("EXPORT_EMPTY_GROUP",
    "export {}; fnc main():int{return 42;}");
  deny("EXPORT_MALFORMED_LIST",
    "export {,main}; fnc main():int{return 42;}");
  deny("EXPORT_MALFORMED_LIST",
    "export {main,,main}; fnc main():int{return 42;}");
  deny("EXPORT_MALFORMED_LIST",
    "export {main main}; fnc main():int{return 42;}");
  deny("EXPORT_SEMICOLON_REQUIRED",
    "export fnc main fnc main():int{return 42;}");
  deny("EXPORT_UNKNOWN_SCOPE",
    "export[project] fnc main; fnc main():int{return 42;}");
  deny("EXPORT_QUALIFIED_TARGET_UNSUPPORTED",
    "export fnc ns.member; fnc main():int{return 42;}");
  deny("EXPORT_CALLABLE_SIGNATURE_UNSUPPORTED",
    "export fnc main(int); fnc main():int{return 42;}");
  deny("EXPORT_LITERAL_ALIAS_UNSUPPORTED",
    "export fnc main as 'literal'; fnc main():int{return 42;}");
  deny("EXPORT_SYMBOL_IDENTITY_TARGET_REQUIRES_SYMBOL_SOURCE",
    "export symbol ready as #ready; fnc main():int{return 42;}");
  deny("EXPORT_UNKNOWN_DECLARATION_KIND",
    "export string ready as 'literal'; fnc main():int{return 42;}");
  deny("EXPORT_REEXPORT_UNSUPPORTED",
    "export * from package; fnc main():int{return 42;}");
  deny("DUPLICATE_VISIBILITY",
    "pub private fnc main():int{return 42;}");
  deny("DUPLICATE_VISIBILITY",
    "pub pub fnc main():int{return 42;}");
  // Exports remain bounded and cannot be forged as authority through direct IR.
  auto forged=original;
  forged.source_export_bindings.push_back(SourceExportBinding{
    "$root","$root::fnc::helper","fnc","helper","main",
    SourceExportScope::Public,SourceExportOrigin::ExplicitExport});
  try { verify(forged); throw std::runtime_error("forged IR admitted"); }
  catch (const std::runtime_error& e) {
    assert(std::string_view(e.what()).find("EXPORT_DUPLICATE_METADATA")!=
           std::string_view::npos);
  }
  // A pre-existing bare Program with no export metadata remains valid.
  Program bare;
  bare.instructions={{Op::Constant,42},{Op::Return,0,0,-1}};
  assert(interpret(bare)==42);
}
