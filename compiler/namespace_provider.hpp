#pragma once
#include "compiler/frontend.hpp"
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <set>

namespace oresforge {
// Trusted-host closed scalar provider profile. The caller must first
// authenticate the complete manifest/inventory and choose an explicit namespace.
// This function does not resolve imports or establish package authority.
inline std::string compile_namespace_provider(std::string_view input,
                                              std::string_view selected_namespace,
                                              std::string_view symbol, Arch arch, std::size_t expected_arity=0) {
  if(input.empty() || input.size()>4096 || input.find('\0')!=std::string_view::npos)
    throw std::runtime_error("E_NAMESPACE_PROFILE: source budget");
  if(selected_namespace.size()>128 || symbol.size()>128)
    throw std::runtime_error("E_NAMESPACE_PROFILE: identifier budget");
  static const std::regex pattern(
      R"(^namespace ([A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*);\npub fnc ([A-Za-z_][A-Za-z0-9_]*)\(([^\n()]*)\): int \{ return ([^\n{};]+); \}\n$)");
  const std::string source(input);
  std::smatch m;
  if(!std::regex_match(source,m,pattern) || m[1].str()!=selected_namespace || selected_namespace.find('.')!=std::string_view::npos ||
     m[3].str()!=symbol || symbol=="main")
    throw std::runtime_error("E_NAMESPACE_PROFILE: explicit public provider required");
  // Split only the bounded, exact typed parameter list. The frontend still
  // validates the original function and body, including names and types.
  std::string params=m[4].str(), args, native_params;
  std::set<std::string> names;
  static const std::regex param(R"(^int ([A-Za-z_][A-Za-z0-9_]{0,127})$)");
  std::size_t pos=0;
  while(pos<params.size()){
    auto end=params.find(", ",pos);
    std::string part=params.substr(pos,end==std::string::npos?end:end-pos);
    std::smatch p;
    if(!std::regex_match(part,p,param)||!names.insert(p[1].str()).second)
      throw std::runtime_error("E_NAMESPACE_PROFILE: exact integer parameters required");
    if(!args.empty()){args+=", ";native_params+=", ";}
    args+=p[1].str();native_params+=p[1].str()+": int";
    if(names.size()>4)throw std::runtime_error("E_NAMESPACE_ARITY: runtime parameter budget");
    if(end==std::string::npos)break;
    pos=end+2;
    if(pos==params.size())throw std::runtime_error("E_NAMESPACE_PROFILE: trailing parameter");
  }
  if(expected_arity!=names.size())
    throw std::runtime_error("E_NAMESPACE_ARITY: incompatible selected call");
  static const std::regex expression(R"(^[A-Za-z_0-9+*() -]{1,1024}$)");
  if(!std::regex_match(m[5].str(),expression))
    throw std::runtime_error("E_NAMESPACE_PROFILE: closed integer expression required");
  // The stage-0 parser currently requires 'use namespace' and a single
  // identifier and name-first typed parameters. Translate only verified
  // header spelling; preserve the original declaration identity and body.
  // No host evaluation. Dotted namespaces remain outside this profile.
  std::string lowered=source;
  lowered.replace(static_cast<std::size_t>(m.position(4)),static_cast<std::size_t>(m.length(4)),native_params);
  return emit_runtime_asm(parse_runtime_source("use "+lowered+"fnc main("+native_params+"): int { return "+
                           std::string(symbol)+"("+args+"); }\n"),arch);
}
}
