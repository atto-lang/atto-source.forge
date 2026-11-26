#pragma once
#include "compiler/frontend.hpp"
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oresforge {
// Trusted-host closed scalar provider profile. The caller must first
// authenticate the complete manifest/inventory and choose an explicit namespace.
// This function does not resolve imports or establish package authority.
inline std::string compile_namespace_provider(std::string_view input,
                                              std::string_view selected_namespace,
                                              std::string_view symbol, Arch arch) {
  if(input.empty() || input.size()>65536 || input.find('\0')!=std::string_view::npos)
    throw std::runtime_error("E_NAMESPACE_PROFILE: source budget");
  if(selected_namespace.size()>512 || symbol.size()>128)
    throw std::runtime_error("E_NAMESPACE_PROFILE: identifier budget");
  static const std::regex pattern(
      R"(^namespace ([A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*);\npub fnc ([A-Za-z_][A-Za-z0-9_]*)\(([^\n()]*)\): int \{ return (-?(0|[1-9][0-9]{0,18})); \}\n$)");
  const std::string source(input);
  std::smatch m;
  if(!std::regex_match(source,m,pattern) || m[1].str()!=selected_namespace ||
     m[3].str()!=symbol || symbol=="main")
    throw std::runtime_error("E_NAMESPACE_PROFILE: explicit public provider required");
  if(!m[4].str().empty())
    throw std::runtime_error("E_NAMESPACE_ARITY: zero-argument provider required");
  // Preserve the actual original provider declaration and body. The synthetic
  // entry invokes it through the existing checked compiler; no host evaluation.
  return emit_runtime_asm(parse_runtime_source(source+"fnc main(): int { return "+
                           std::string(symbol)+"(); }\n"),arch);
}
}
