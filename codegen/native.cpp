#include "codegen/native.hpp"
#include <bit>
#include <cstdint>
#include <sstream>
#include <stdexcept>
namespace oresforge {
Arch parse_arch(std::string_view name) {
  if (name=="x86_64" || name=="amd64") return Arch::X86_64;
  if (name=="arm64" || name=="aarch64") return Arch::ARM64;
  throw std::runtime_error("unsupported architecture");
}
std::string emit_asm(const Program& p,Arch arch) {
  // Entire supported source subset is constant arithmetic. Evaluate with
  // checked semantics first; do not emit unchecked native arithmetic.
  const std::int64_t value=interpret(p);
  std::ostringstream out;
  out<<".text\n.globl oresforge_entry\noresforge_entry:\n";
  if (arch==Arch::X86_64) {
    out<<"  movabs $"<<value<<", %rax\n";
  } else {
    std::uint64_t bits=std::bit_cast<std::uint64_t>(value);
    out<<"  movz x0, #"<<(bits&0xffffULL)<<"\n";
    for (unsigned shift=16;shift<64;shift+=16)
      out<<"  movk x0, #"<<((bits>>shift)&0xffffULL)<<", lsl #"<<shift<<"\n";
  }
  out<<"  ret\n";
  return out.str();
}
}
