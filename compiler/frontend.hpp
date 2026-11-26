#pragma once
#include "compiler/ir.hpp"
#include <string_view>
#include <optional>
#include "codegen/native.hpp"
namespace oresforge {
// Bootstrap subset only: pure typed i64 functions, inlined into main().
// No native ABI calls, mutable locals, recursion, control flow, or self-hosting.
Program parse_source(std::string_view source);
// Separate opt-in stage-0 runtime ABI profile: main may take 0..4 i64 args.
// Helper function bodies still statically inline; no real call frames yet.
Program parse_runtime_source(std::string_view source);
// One-entry runtime-ABI assembly session; no JIT or disk cache.
struct IncrementalBuild { std::string assembly; bool reused; };
class IncrementalSession {
  struct Entry { std::string source; Arch arch; std::string assembly; };
  std::optional<Entry> prior_;
public:
  IncrementalBuild build(std::string_view source, Arch arch, bool cold=false);
};
}
