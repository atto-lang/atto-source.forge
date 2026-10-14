#pragma once
#include "compiler/ir.hpp"
#include <string>
#include <string_view>
namespace oresforge {
enum class Arch { X86_64, ARM64 };
Arch parse_arch(std::string_view);
std::string emit_asm(const Program&,Arch);
}
