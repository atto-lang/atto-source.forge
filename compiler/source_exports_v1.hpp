#pragma once
// Canonical source-export metadata for Forge's bounded scalar-fnc subset.
// Source export bindings are NOT linker/native symbol grants or capabilities.
// Contract: explicit-exports-v1.2026-10-26.5 (common-source #156).
#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace oresforge {
enum class SourceExportScope { File, Package, Public };
enum class SourceExportOrigin { ImplicitPub, ExplicitExport };
struct SourceExportBinding {
  std::string owner_namespace;
  std::string local_symbol_id;
  std::string local_kind;
  std::string local_name;
  std::string external_name;
  SourceExportScope scope;
  SourceExportOrigin origin;
};
struct SourceLocalFnc {
  std::string name;
  std::optional<SourceExportScope> visibility;
  bool explicit_private = false;
};
struct SourceExportRequest {
  std::string local_name;
  std::string external_name;
  std::string kind; // empty = infer from local; Forge stage-0 supports fnc
  SourceExportScope scope = SourceExportScope::Public;
};

inline bool source_export_name_valid(std::string_view name) noexcept {
  if (name.empty() || name.size() > 256) return false;
  for (std::size_t i=0; i<name.size(); ++i) {
    const unsigned char c=static_cast<unsigned char>(name[i]);
    const bool letter=(c>='A'&&c<='Z') || (c>='a'&&c<='z');
    if (!letter && c!='_' && !(i>0 && c>='0'&&c<='9')) return false;
  }
  return true;
}
[[noreturn]] inline void source_export_error(const char* category) {
  throw std::runtime_error(category);
}
inline void validate_source_exports(
    const std::vector<SourceExportBinding>& bindings) {
  if (bindings.size()>1024) source_export_error("EXPORT_SOURCE_LIMIT");
  std::unordered_map<std::string,std::size_t> seen;
  for (std::size_t i=0; i<bindings.size(); ++i) {
    const auto& b=bindings[i];
    if (b.owner_namespace!="$root" || b.local_kind!="fnc" ||
        !source_export_name_valid(b.local_name) ||
        !source_export_name_valid(b.external_name) ||
        b.local_symbol_id!="$root::fnc::"+b.local_name)
      source_export_error("EXPORT_INVALID_METADATA");
    if (!seen.emplace(b.external_name,i).second)
      source_export_error("EXPORT_DUPLICATE_METADATA");
  }
}
inline std::vector<SourceExportBinding> resolve_source_exports(
    const std::vector<SourceLocalFnc>& declarations,
    const std::vector<SourceExportRequest>& requests) {
  if (declarations.size()>1024 || requests.size()>1024)
    source_export_error("EXPORT_SOURCE_LIMIT");
  std::unordered_map<std::string,SourceLocalFnc> locals;
  for (const auto& d:declarations) {
    if (!source_export_name_valid(d.name))
      source_export_error("EXPORT_INVALID_NAME");
    if (!locals.emplace(d.name,d).second)
      source_export_error("EXPORT_DUPLICATE_LOCAL");
  }
  std::vector<SourceExportBinding> result;
  std::unordered_map<std::string,std::size_t> external;
  auto add=[&](const SourceLocalFnc& decl,std::string name,
               SourceExportScope scope,SourceExportOrigin origin) {
    if (!source_export_name_valid(name))
      source_export_error("EXPORT_INVALID_NAME");
    const auto found=external.find(name);
    if (found!=external.end()) {
      const auto& previous=result.at(found->second);
      if (previous.local_name!=decl.name)
        source_export_error("EXPORT_ALIAS_COLLISION");
      if (previous.scope!=scope)
        source_export_error("EXPORT_SCOPE_CONFLICT");
      // Equivalent bindings are idempotent, regardless of origin/order.
      return;
    }
    if (result.size()>=1024) source_export_error("EXPORT_SOURCE_LIMIT");
    external.emplace(name,result.size());
    result.push_back(SourceExportBinding{
      "$root","$root::fnc::"+decl.name,"fnc",decl.name,
      std::move(name),scope,origin});
  };
  // Sort implicit declarations, not unordered_map insertion order.
  std::vector<std::string> local_names;
  for (const auto& [name,_]:locals) local_names.push_back(name);
  std::sort(local_names.begin(),local_names.end());
  for (const auto& name:local_names) {
    const auto& d=locals.at(name);
    if (d.visibility)
      add(d,d.name,*d.visibility,SourceExportOrigin::ImplicitPub);
  }
  for (const auto& request:requests) {
    if (!request.kind.empty() && request.kind!="fnc") {
      if (locals.contains(request.local_name))
        source_export_error("EXPORT_KIND_MISMATCH");
      source_export_error("EXPORT_BACKEND_UNSUPPORTED_KIND");
    }
    if (!source_export_name_valid(request.local_name))
      source_export_error("EXPORT_INVALID_NAME");
    const auto found=locals.find(request.local_name);
    if (found==locals.end()) source_export_error("EXPORT_UNKNOWN_TARGET");
    const auto& decl=found->second;
    if (decl.explicit_private) source_export_error("EXPORT_PRIVATE_TARGET");
    if (decl.visibility && *decl.visibility!=request.scope)
      source_export_error("EXPORT_SCOPE_CONFLICT");
    add(decl,request.external_name.empty() ? decl.name :
        request.external_name,request.scope,SourceExportOrigin::ExplicitExport);
  }
  std::sort(result.begin(),result.end(),[](const auto& a,const auto& b) {
    if (a.external_name!=b.external_name) return a.external_name<b.external_name;
    return a.local_symbol_id<b.local_symbol_id;
  });
  validate_source_exports(result);
  return result;
}
} // namespace oresforge
