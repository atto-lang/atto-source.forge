#pragma once
// Strict stage-0 allocation source profile. Closed grammar is the only
// producer of this MIR: no user-authored completeness certificate is accepted.
#include "compiler/escape_analysis.hpp"
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <sstream>

namespace oresforge::alloc_experiment {

enum class InputKind { Arg0, Arg1, Literal };
struct Input {
  InputKind kind = InputKind::Literal;
  std::int64_t literal = 0;
};
// Complete, typed, closed-world single-object MIR. No pointers, calls,
// exceptional edges, control flow or suspension are admitted by this profile.
class Program;
std::string dump_ir(const Program&);
std::string emit_x86_64(const Program&, bool safe);
class Program {
  friend class Parser;
  friend std::string dump_ir(const Program&);
  friend std::string emit_x86_64(const Program&, bool safe);
  Program() = default;
  std::array<Input, 2> initializers{};
  std::array<unsigned, 2> reads{0, 1};
 public:
  Program(const Program&) = default;
};

class Parser {
 public:
  explicit Parser(std::string_view code) {
    if (code.size() > 1024 * 1024) throw std::runtime_error("allocation source budget exceeded");
    for (std::size_t i = 0; i < code.size();) {
      const unsigned char ch = static_cast<unsigned char>(code[i]);
      if (ch == 0 || ch > 127) throw std::runtime_error("unsupported allocation source character");
      if (std::isspace(ch)) { ++i; continue; }
      if (code[i] == '/' && i + 1 < code.size() && code[i+1] == '/') {
        i += 2;
        while (i < code.size() && code[i] != '\n' && code[i] != '\r') ++i;
        continue;
      }
      if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_') {
        auto start = i++;
        while (i < code.size()) {
          const unsigned char x = static_cast<unsigned char>(code[i]);
          if (!((x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') ||
                (x >= '0' && x <= '9') || x == '_')) break;
          ++i;
        }
        tokens_.emplace_back(code.substr(start, i - start));
      } else if (ch >= '0' && ch <= '9') {
        auto start = i++;
        while (i < code.size() && code[i] >= '0' && code[i] <= '9') ++i;
        tokens_.emplace_back(code.substr(start, i - start));
      } else if (std::string_view("{}():;,.=+-").find(static_cast<char>(ch)) != std::string_view::npos) {
        tokens_.emplace_back(1, static_cast<char>(ch)); ++i;
      } else throw std::runtime_error("unsupported allocation source token");
      if (tokens_.size() > 128) throw std::runtime_error("allocation token budget exceeded");
    }
  }
  Program parse() {
    // Deliberately bounded subset; struct and all uses are consumed through EOF.
    expect("define"); expect("struct"); expect("Point"); expect("as");
    expect("int"); expect("x"); expect("int"); expect("y"); expect("end");
    expect("define"); expect("pub"); expect("fnc"); expect("main"); expect("(");
    const std::string first = identifier(); expect(":"); expect("int"); expect(",");
    const std::string second = identifier(); expect(":"); expect("int"); expect(")");
    if (first == second || first == "p" || second == "p")
      throw std::runtime_error("duplicate or shadowed allocation parameter");
    expect(":"); expect("int"); expect("{");
    expect("let"); const std::string local = identifier();
    if (local == first || local == second)
      throw std::runtime_error("allocation local shadows parameter");
    expect("="); expect("Point"); expect("{"); expect("x"); expect(":");
    Program program;
    program.initializers[0] = input(first, second); expect(",");
    expect("y"); expect(":"); program.initializers[1] = input(first, second);
    expect("}"); expect(";"); expect("return");
    program.reads[0] = field(local); expect("+"); program.reads[1] = field(local);
    expect(";"); expect("}");
    if (cursor_ != tokens_.size()) throw std::runtime_error("unexpected trailing allocation source");
    verify(program);
    return program;
  }
 private:
  std::vector<std::string> tokens_;
  std::size_t cursor_ = 0;
  static void verify(const Program& p) {
    if (p.reads[0] > 1 || p.reads[1] > 1)
      throw std::runtime_error("unverified allocation read field");
  }
  std::string pop() {
    if (cursor_ == tokens_.size()) throw std::runtime_error("unexpected end of allocation source");
    return tokens_[cursor_++];
  }
  void expect(std::string_view wanted) {
    if (pop() != wanted) throw std::runtime_error("unsupported allocation source grammar");
  }
  std::string identifier() {
    auto token = pop();
    if (token.empty() || !((token[0] >= 'a' && token[0] <= 'z') ||
                           (token[0] >= 'A' && token[0] <= 'Z') || token[0] == '_'))
      throw std::runtime_error("expected allocation identifier");
    return token;
  }
  Input input(const std::string& first, const std::string& second) {
    const std::string t = pop();
    if (t == first) return {InputKind::Arg0, 0};
    if (t == second) return {InputKind::Arg1, 0};
    const std::string digits = t == "-" ? "-" + pop() : t;
    std::int64_t value = 0;
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size())
      throw std::runtime_error("invalid allocation initializer");
    return {InputKind::Literal, value};
  }
  unsigned field(const std::string& local) {
    expect(local); expect(".");
    const auto t = pop();
    if (t == "x") return 0;
    if (t == "y") return 1;
    throw std::runtime_error("unsupported allocation field read");
  }
};

inline Program parse(std::string_view source) { return Parser(source).parse(); }

inline std::string dump_ir(const Program& p) {
  std::ostringstream out;
  out << "alloc Point[2] (closed-world verified)\n";
  for (unsigned i = 0; i < 2; ++i) {
    out << "store.field " << (i ? 'y' : 'x') << " <- ";
    switch (p.initializers[i].kind) {
      case InputKind::Arg0: out << "arg0"; break;
      case InputKind::Arg1: out << "arg1"; break;
      case InputKind::Literal: out << p.initializers[i].literal; break;
    }
    out << '\n';
  }
  out << "ret add.i64.checked field" << p.reads[0] << " field" << p.reads[1] << '\n';
  return out.str();
}

// Linux System V x86-64 only. Baseline calls malloc/free; safe scalarizes.
// The high-level Point allocation has no exposed identity or alloc-failure
// recovery in this intentionally restricted source-language subset.
inline std::string emit_x86_64(const Program& p, bool safe) {
  // Only Parser can construct this source profile, and Parser accepts every
  // token through EOF. Its closed grammar admits only two field stores and
  // two field reads; hence we can construct this complete effect graph here.
  // A future generic MIR builder must NOT set completeness on this basis.
  if (safe) {
    escape::Program effect_graph;
    escape::Allocation object;
    object.name = "p";
    object.type = escape::ValueKind::NominalRecord;
    object.size_bytes = 16;
    object.alignment = 8;
    object.fields = 2;
    object.allocator_effect_observable = false; // No allocation hooks in profile.
    effect_graph.allocations.push_back(object);
    effect_graph.uses = {{0, escape::UseKind::FieldWrite, 0},
                         {0, escape::UseKind::FieldWrite, 1},
                         {0, escape::UseKind::FieldRead, p.reads[0]},
                         {0, escape::UseKind::FieldRead, p.reads[1]}};
    effect_graph.complete_effect_graph = true;
    const auto decisions = escape::analyze(effect_graph);
    if (decisions.size() != 1 ||
        decisions[0].storage != escape::Storage::ScalarCandidate)
      throw std::runtime_error("allocation MIR failed safe scalarization proof");
  }
  std::ostringstream out;
  out << ".text\n.globl oresforge_alloc_entry\n.type oresforge_alloc_entry, @function\n"
         "oresforge_alloc_entry:\n";
  auto source = [&](Input input, const char* dst) {
    switch(input.kind) {
      case InputKind::Arg0: out << "  movq %rdi, " << dst << "\n"; break;
      case InputKind::Arg1: out << "  movq %rsi, " << dst << "\n"; break;
      case InputKind::Literal: out << "  movabsq $" << input.literal << ", " << dst << "\n"; break;
    }
  };
  if (safe) {
    source(p.initializers[0], "%r10");
    source(p.initializers[1], "%r11");
    out << "  movq " << (p.reads[0] ? "%r11" : "%r10") << ", %rax\n"
        << "  addq " << (p.reads[1] ? "%r11" : "%r10") << ", %rax\n"
           "  jo .Loresforge_alloc_overflow\n  ret\n"
           ".Loresforge_alloc_overflow:\n  ud2\n";
  } else {
    // Three callee-saved pushes align the stack for PLT calls.
    out << "  pushq %rbx\n  pushq %r12\n  pushq %r13\n";
    source(p.initializers[0], "%r12");
    source(p.initializers[1], "%r13");
    out << "  movl $16, %edi\n  call malloc@PLT\n"
           "  testq %rax, %rax\n  je .Loresforge_alloc_failure\n"
           "  movq %rax, %rbx\n"
           "  movq %r12, 0(%rbx)\n  movq %r13, 8(%rbx)\n"
           "  movq " << (p.reads[0] ? "8(%rbx)" : "0(%rbx)") << ", %rax\n"
        << "  addq " << (p.reads[1] ? "8(%rbx)" : "0(%rbx)") << ", %rax\n"
           "  jo .Loresforge_alloc_overflow_free\n"
           "  movq %rax, %r12\n  movq %rbx, %rdi\n"
           "  call free@PLT\n  movq %r12, %rax\n"
           "  popq %r13\n  popq %r12\n  popq %rbx\n  ret\n"
           ".Loresforge_alloc_overflow_free:\n"
           "  movq %rbx, %rdi\n  call free@PLT\n"
           ".Loresforge_alloc_failure:\n  ud2\n";
  }
  out << ".size oresforge_alloc_entry, .-oresforge_alloc_entry\n"
         ".section .note.GNU-stack,\"\",@progbits\n";
  return out.str();
}
} // namespace oresforge::alloc_experiment
