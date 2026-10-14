#pragma once
#include "compiler/ir.hpp"
#include <string_view>
namespace oresforge {
// Bootstrap subset only: [define] [pub] fnc main() : int { return <expression>; }
Program parse_source(std::string_view source);
}
