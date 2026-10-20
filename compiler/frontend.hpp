#pragma once
#include "compiler/ir.hpp"
#include <string_view>
namespace oresforge {
// Bootstrap subset only: pure typed i64 functions, inlined into main().
// No native ABI calls, mutable locals, recursion, control flow, or self-hosting.
Program parse_source(std::string_view source);
}
