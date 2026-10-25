#pragma once
#include "compiler/ir.hpp"
#include <string_view>
namespace oresforge {
// Bootstrap subset only: pure typed i64 functions, inlined into main().
// No native ABI calls, mutable locals, recursion, control flow, or self-hosting.
Program parse_source(std::string_view source);
// Separate opt-in stage-0 runtime ABI profile: main may take 0..4 i64 args.
// Helper function bodies still statically inline; no real call frames yet.
Program parse_runtime_source(std::string_view source);
}
