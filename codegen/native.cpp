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
  return out.str();
}
// Linux System V x86-64 and AAPCS64 integer-argument ABI only.
// Every SSA result owns an 8-byte stack slot. No calls, frame pointer,
// heap allocation, register allocator, or generated helper-function ABI yet.
namespace {
std::string emit_runtime_asm_impl(const Program& p,Arch arch,bool effect_abi) {
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
        out<<"  movq "<<slot(ins.a)<<", %rax\n";
        if (effect_abi)
          out<<"  xorl %edx, %edx\n  xorl %ecx, %ecx\n";
        out<<"  addq $"<<bytes<<", %rsp\n  ret\n";
      } else if (ins.op==Op::Abs) {
        const auto label=".Loresforge_abs_ok_"+std::to_string(i);
        out<<"  movq "<<slot(ins.a)<<", %rax\n"
           <<"  testq %rax, %rax\n"
           <<"  jns "<<label<<"\n"
           <<"  negq %rax\n"
           <<"  jo .Loresforge_overflow\n"
           <<label<<":\n"
           <<"  movq %rax, "<<slot(static_cast<int>(i))<<"\n";
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
    out<<".Loresforge_overflow:\n";
    if (effect_abi) {
      out<<"  xorq %rax, %rax\n"
         <<"  movl $1, %edx\n"
         <<"  movl $65537, %ecx\n"
         <<"  addq $"<<bytes<<", %rsp\n"
         <<"  ret\n";
    } else {
      out<<"  ud2\n";
    }
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
        if (effect_abi)
          out<<"  mov x1, #0\n  mov x2, #0\n";
        adjust(false);
        out<<"  ret\n";
      } else if (ins.op==Op::Abs) {
        const auto label=".Loresforge_abs_ok_"+std::to_string(i);
        out<<"  ldr x9, "<<slot(ins.a)<<"\n"
           <<"  mov x11, x9\n"
           <<"  cmp x9, #0\n"
           <<"  b.ge "<<label<<"\n"
           <<"  negs x11, x9\n"
           <<"  b.vs .Loresforge_overflow\n"
           <<label<<":\n"
           <<"  str x11, "<<slot(static_cast<int>(i))<<"\n";
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
    out<<".Loresforge_overflow:\n";
    if (effect_abi) {
      out<<"  mov x0, #0\n"
         <<"  mov x1, #1\n"
         <<"  movz x2, #1\n"
         <<"  movk x2, #1, lsl #16\n";
      adjust(false);
      out<<"  ret\n";
    } else {
      out<<"  brk #0\n";
    }
  }
  out<<".size oresforge_entry, .-oresforge_entry\n";
  return out.str();
}
} // namespace

std::string emit_runtime_asm(const Program& p,Arch arch) {
  return emit_runtime_asm_impl(p,arch,false);
}
std::string emit_runtime_effect_asm(const Program& p,Arch arch) {
  return emit_runtime_asm_impl(p,arch,true);
}

}
