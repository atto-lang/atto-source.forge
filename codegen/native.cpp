#include "codegen/native.hpp"
#include <bit>
#include <algorithm>
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
  if (arch != Arch::X86_64 && arch != Arch::ARM64)
    throw std::runtime_error("unsupported architecture");
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
  // The native x86-64 scalar emitter targets GNU/ELF assembly. Without
  // this note, a linked object can request an executable process stack.
  if (arch==Arch::X86_64)
    out<<".section .note.GNU-stack,\"\",@progbits\n";
  return out.str();
}
// Linux System V x86-64 and AAPCS64 integer-argument ABI only.
// Every SSA result owns an 8-byte stack slot. No calls, frame pointer,
// heap allocation, register allocator, or generated helper-function ABI yet.
std::string emit_runtime_asm(const Program& p,Arch arch) {
  if (arch != Arch::X86_64 && arch != Arch::ARM64)
    throw std::runtime_error("unsupported architecture");
  verify(p);
  if (p.instructions.size()>4096)
    throw std::runtime_error("runtime native instruction budget exceeded");
  for (const auto& ins:p.instructions)
    if (ins.op==Op::Div)
      throw std::runtime_error("runtime i64 division lowering not supported");
  const std::size_t bytes=((p.instructions.size()*8u+15u)/16u)*16u;
  if (bytes>32768u) throw std::runtime_error("runtime native frame budget exceeded");
  std::ostringstream out;
  out<<".text\n.globl oresforge_entry\n.type oresforge_entry, @function\noresforge_entry:\n";
  if (arch==Arch::X86_64) {
    out<<"  subq $"<<bytes<<", %rsp\n";
    constexpr const char* params[]={"%rdi","%rsi","%rdx","%rcx"};
    for (std::size_t i=0;i<p.instructions.size();++i) {
      const auto& ins=p.instructions[i];
      const auto slot=[&](int index) {
        return std::to_string(static_cast<std::size_t>(index)*8u)+"(%rsp)";
      };
      if (ins.op==Op::Constant) {
        out<<"  movabs $"<<ins.value<<", %rax\n"
           <<"  movq %rax, "<<slot(static_cast<int>(i))<<"\n";
      } else if (ins.op==Op::Parameter) {
        out<<"  movq "<<params[static_cast<std::size_t>(ins.value)]
           <<", "<<slot(static_cast<int>(i))<<"\n";
      } else if (ins.op==Op::Return) {
        out<<"  movq "<<slot(ins.a)<<", %rax\n"
           <<"  addq $"<<bytes<<", %rsp\n  ret\n";
      } else {
        out<<"  movq "<<slot(ins.a)<<", %rax\n";
        switch(ins.op) {
          case Op::Add: out<<"  addq ";break;
          case Op::Sub: out<<"  subq ";break;
          case Op::Mul: out<<"  imulq ";break;
          default: throw std::runtime_error("unexpected runtime opcode");
        }
        out<<slot(ins.b)<<", %rax\n"
           <<"  jo .Loresforge_overflow\n"
           <<"  movq %rax, "<<slot(static_cast<int>(i))<<"\n";
      }
    }
    out<<".Loresforge_overflow:\n  ud2\n";
  } else {
    // AArch64 load/store unsigned scaled offsets reach up to 32760 bytes.
    // Split SP adjustments into 16-byte multiples representable as immediates.
    auto adjust=[&](bool down){
      std::size_t remaining=bytes;
      while(remaining) {
        const auto part=std::min<std::size_t>(remaining,4080u);
        out<<(down?"  sub sp, sp, #":"  add sp, sp, #")<<part<<"\n";
        remaining-=part;
      }
    };
    auto slot=[&](int index){
      return "[sp, #"+std::to_string(static_cast<std::size_t>(index)*8u)+"]";
    };
    adjust(true);
    for (std::size_t i=0;i<p.instructions.size();++i) {
      const auto& ins=p.instructions[i];
      if (ins.op==Op::Constant) {
        const auto bits=std::bit_cast<std::uint64_t>(ins.value);
        out<<"  movz x11, #"<<(bits&0xffffULL)<<"\n";
        for (unsigned shift=16;shift<64;shift+=16)
          out<<"  movk x11, #"<<((bits>>shift)&0xffffULL)<<", lsl #"<<shift<<"\n";
        out<<"  str x11, "<<slot(static_cast<int>(i))<<"\n";
      } else if (ins.op==Op::Parameter) {
        out<<"  str x"<<ins.value<<", "<<slot(static_cast<int>(i))<<"\n";
      } else if (ins.op==Op::Return) {
        out<<"  ldr x0, "<<slot(ins.a)<<"\n";
        adjust(false);
        out<<"  ret\n";
      } else {
        out<<"  ldr x9, "<<slot(ins.a)<<"\n"
           <<"  ldr x10, "<<slot(ins.b)<<"\n";
        if (ins.op==Op::Add) out<<"  adds x11, x9, x10\n  b.vs .Loresforge_overflow\n";
        else if (ins.op==Op::Sub) out<<"  subs x11, x9, x10\n  b.vs .Loresforge_overflow\n";
        else if (ins.op==Op::Mul) {
          out<<"  mul x11, x9, x10\n"
             <<"  smulh x12, x9, x10\n"
             <<"  asr x13, x11, #63\n"
             <<"  cmp x12, x13\n"
             <<"  b.ne .Loresforge_overflow\n";
        } else throw std::runtime_error("unexpected runtime opcode");
        out<<"  str x11, "<<slot(static_cast<int>(i))<<"\n";
      }
    }
    out<<".Loresforge_overflow:\n  brk #0\n";
  }
  out<<".size oresforge_entry, .-oresforge_entry\n";
  // Runtime-argument native paths need the same ELF stack contract as
  // compile-time scalar emission; no implicit executable process stack.
  if (arch == Arch::X86_64)
    out<<".section .note.GNU-stack,\"\",@progbits\n";
  return out.str();
}

}
