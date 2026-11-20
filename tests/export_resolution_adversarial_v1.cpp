#include "compiler/source_exports_v1.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace oresforge;
namespace {
template <class F>
void rejects(F&& f, std::string_view category) {
  try { f(); }
  catch (const std::exception& e) {
    assert(std::string_view(e.what()).find(category)!=std::string_view::npos);
    return;
  }
  assert(false && "invalid export metadata admitted");
}
std::string signature(const std::vector<SourceExportBinding>& items) {
  std::string result;
  for (const auto& b : items) {
    result+=b.local_symbol_id+'|'+b.external_name+'|'+
      std::to_string(static_cast<int>(b.scope))+'|'+
      std::to_string(static_cast<int>(b.origin))+';';
  }
  return result;
}
}
int main() {
  const std::vector<SourceLocalFnc> declarations = {
    {"main",SourceExportScope::Public,false},
    {"worker",std::nullopt,false},
    {"local",SourceExportScope::Package,false}
  };
  const std::vector<SourceExportRequest> requests = {
    {"worker","Worker","fnc",SourceExportScope::Public},
    {"worker","Worker","fnc",SourceExportScope::Public},
    {"main","","fnc",SourceExportScope::Public},
    {"local","","fnc",SourceExportScope::Package}
  };
  const auto expected=resolve_source_exports(declarations,requests);
  assert(expected.size()==3);
  assert(expected[0].external_name=="Worker");
  assert(expected[1].external_name=="local");
  assert(expected[2].external_name=="main");
  assert(expected[2].origin==SourceExportOrigin::ImplicitPub);
  assert(expected[1].origin==SourceExportOrigin::ImplicitPub);
  std::mt19937 engine(0xC0FFEE);
  auto permutation=requests;
  for (int i=0; i<300; ++i) {
    std::shuffle(permutation.begin(),permutation.end(),engine);
    assert(signature(resolve_source_exports(declarations,permutation))==signature(expected));
  }
  auto bad=expected;
  bad[0].scope=static_cast<SourceExportScope>(200);
  rejects([&]{validate_source_exports(bad);},"EXPORT_INVALID_METADATA");
  bad=expected;
  bad[0].origin=static_cast<SourceExportOrigin>(200);
  rejects([&]{validate_source_exports(bad);},"EXPORT_INVALID_METADATA");
  bad=expected;
  bad[0].origin=SourceExportOrigin::ImplicitPub;
  rejects([&]{validate_source_exports(bad);},"EXPORT_INVALID_METADATA");
  bad=expected;
  bad[0].local_symbol_id="not-canonical";
  rejects([&]{validate_source_exports(bad);},"EXPORT_INVALID_METADATA");
  bad=expected;
  bad.push_back(bad[0]);
  rejects([&]{validate_source_exports(bad);},"EXPORT_DUPLICATE_METADATA");
  auto bad_declarations=declarations;
  bad_declarations[0].visibility=static_cast<SourceExportScope>(55);
  rejects([&]{(void)resolve_source_exports(bad_declarations,{});},"EXPORT_INVALID_VISIBILITY");
  bad_declarations=declarations;
  bad_declarations[0].explicit_private=true;
  rejects([&]{(void)resolve_source_exports(bad_declarations,{});},"EXPORT_INVALID_VISIBILITY");
  auto bad_requests=requests;
  bad_requests[0].scope=static_cast<SourceExportScope>(99);
  rejects([&]{(void)resolve_source_exports(declarations,bad_requests);},"EXPORT_UNKNOWN_SCOPE");
  rejects([&]{(void)resolve_source_exports(declarations,
    {{"worker","main","fnc",SourceExportScope::Public}});},
    "EXPORT_ALIAS_COLLISION");
  rejects([&]{(void)resolve_source_exports(declarations,
    {{"main","","fnc",SourceExportScope::File}});},
    "EXPORT_SCOPE_CONFLICT");
  rejects([&]{(void)resolve_source_exports(declarations,
    {{"worker","","fnc",SourceExportScope::Public},
     {"worker","","fnc",SourceExportScope::Package}});},
    "EXPORT_SCOPE_CONFLICT");
  rejects([&]{(void)resolve_source_exports(declarations,
    {{"missing","","fnc",SourceExportScope::Public}});},
    "EXPORT_UNKNOWN_TARGET");
  rejects([&]{(void)resolve_source_exports(declarations,
    {{"worker","","class",SourceExportScope::Public}});},
    "EXPORT_KIND_MISMATCH");
  rejects([&]{(void)resolve_source_exports(declarations,
    {{"worker","5invalid","fnc",SourceExportScope::Public}});},
    "EXPORT_INVALID_NAME");
  rejects([&]{(void)resolve_source_exports(declarations,
    {{"worker",std::string(257,'x'),"fnc",SourceExportScope::Public}});},
    "EXPORT_INVALID_NAME");
  rejects([&]{(void)resolve_source_exports(
    {{"secret",std::nullopt,true}},{{"secret","","fnc",SourceExportScope::Public}});},
    "EXPORT_PRIVATE_TARGET");
  rejects([&]{(void)resolve_source_exports({{"dup",std::nullopt,false},
    {"dup",std::nullopt,false}},{});},"EXPORT_DUPLICATE_LOCAL");
  std::vector<SourceLocalFnc> max_decls;
  for (int i=0;i<1024;++i)
    max_decls.push_back({"n"+std::to_string(i),SourceExportScope::Public,false});
  assert(resolve_source_exports(max_decls,{}).size()==1024);
  max_decls.push_back({"overflow",std::nullopt,false});
  rejects([&]{(void)resolve_source_exports(max_decls,{});},"EXPORT_SOURCE_LIMIT");
}
