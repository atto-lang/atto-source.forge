#pragma once
#include "compiler/ir.hpp"
#include <string>
#include <string_view>
namespace oresforge {
enum class Arch { X86_64, ARM64 };
Arch parse_arch(std::string_view);
std::string emit_asm(const Program&,Arch);
// Fail-closed runtime-argument native lowering retained for callers without
// an Oreslang effect frame. Integer overflow traps.
std::string emit_runtime_asm(const Program&,Arch);

// Effect-aware internal ABI for a caller that owns a guest handler frame.
// x86-64: rax=value, rdx=EffectKind, rcx=error code.
// AArch64: x0=value, x1=EffectKind, x2=error code.
// Overflow returns Throw/65537 instead of issuing a host trap.
std::string emit_runtime_effect_asm(const Program&,Arch);
}
