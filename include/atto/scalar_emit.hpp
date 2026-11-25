#pragma once
// Deterministic C++20 source emission from a validated restricted .atto AST.
// Source bytes are never concatenated into output: only checked AST data.
#include "atto/scalar_parser.hpp"
#include <limits>
#include <optional>
#include <string>

namespace atto::forge::scalar {
inline constexpr std::size_t max_emitted_bytes = 64 * 1024;
[[nodiscard]] inline std::optional<std::string> emit_cpp(const Parsed& p) {
  if (!validate_ast(p)) return std::nullopt;
  std::string code = R"(#include <atto/checked_i64.hpp>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <optional>
)";
  if (!p.trace_steps.empty()) {
    code += R"(extern "C" std::uint64_t ores_native_telemetry_span_begin(const char*, std::uint64_t);
extern "C" void ores_native_telemetry_span_end(std::uint64_t);
namespace {
struct SpanScope {
  const std::uint64_t token;
  ~SpanScope() { ores_native_telemetry_span_end(token); }
};
}
)";
  }
  code += "int main() {\n";
  std::size_t open_spans = 0;
  if (!p.trace_steps.empty()) {
    code += "  atto::forge::i64 answer = 0;\n";
    for (const auto& operation : p.trace_steps) {
      if (!operation) {
        code += "  }\n";
        --open_spans;
        continue;
      }
      ++open_spans;
      code += "  {\n  const SpanScope span{ores_native_telemetry_span_begin(\"";
      constexpr char hex[] = "0123456789abcdef";
      for (unsigned char byte : *operation) {
        code += "\\x";
        code += hex[byte >> 4];
        code += hex[byte & 15];
      }
      code += "\", " + std::to_string(operation->size()) + ")};\n";
    }
  }
  auto var = [](std::size_t n) { return "v" + std::to_string(n); };
  for (std::size_t i = 0; i < p.nodes.size(); ++i) {
    const auto& n = p.nodes[i];
    if (n.op == Op::literal) {
      const auto value = n.value == std::numeric_limits<i64>::min()
          ? "std::numeric_limits<atto::forge::i64>::min()"
          : std::to_string(n.value);
      code += "  std::optional<atto::forge::i64> " + var(i) + "{" + value + "};\n";
    } else {
      if (n.left >= i || (n.op != Op::neg && n.op != Op::bnot && n.right >= i)) return std::nullopt;
      std::string call;
      if (n.op == Op::shl || n.op == Op::lshr || n.op == Op::ashr) {
        code += "  if (*" + var(n.right) + " < 0 || *" + var(n.right) +
            " > 63) { std::fputs(\"E_SHIFT_DISTANCE\\n\", stderr); return 72; }\n";
      }
      switch (n.op) {
        case Op::bnot:
          call = "bit_not(*" + var(n.left) + ")";
          break;
        case Op::neg:
          call = "checked_neg(*" + var(n.left) + ")";
          break;
        case Op::add:
          call = "checked_add(*" + var(n.left) + ", *" + var(n.right) + ")";
          break;
        case Op::sub:
          call = "checked_sub(*" + var(n.left) + ", *" + var(n.right) + ")";
          break;
        case Op::shl:
          call = "shl_i64(*" + var(n.left) + ", *" + var(n.right) + ")";
          break;
        case Op::lshr:
          call = "lshr_i64(*" + var(n.left) + ", *" + var(n.right) + ")";
          break;
        case Op::ashr:
          call = "ashr_i64(*" + var(n.left) + ", *" + var(n.right) + ")";
          break;
        case Op::band:
          call = "bit_and(*" + var(n.left) + ", *" + var(n.right) + ")";
          break;
        case Op::bor:
          call = "bit_or(*" + var(n.left) + ", *" + var(n.right) + ")";
          break;
        case Op::bxor:
          call = "bit_xor(*" + var(n.left) + ", *" + var(n.right) + ")";
          break;
        case Op::mul:
          call = "checked_mul(*" + var(n.left) + ", *" + var(n.right) + ")";
          break;
        case Op::div:
        case Op::rem:
          code += "  if (*" + var(n.right) +
              " == 0) { std::fputs(\"E_DIVZERO\\n\", stderr); return 71; }\n";
          call = (n.op == Op::div ? "checked_div(*" : "checked_rem(*") +
              var(n.left) + ", *" + var(n.right) + ")";
          break;
        default:
          return std::nullopt;
      }
      code += "  auto " + var(i) + " = atto::forge::" + call + ";\n";
      code += "  if (!" + var(i) +
          ") { std::fputs(\"E_OVERFLOW\\n\", stderr); return 70; }\n";
    }
    if (code.size() > max_emitted_bytes) return std::nullopt;
  }
  if (p.trace_steps.empty()) {
    code += "  std::printf(\"%lld\\n\", static_cast<long long>(*" + var(p.root) + "));\n";
  } else {
    code += "  answer = *" + var(p.root) + ";\n";
    for (std::size_t i = 0; i < open_spans; ++i) code += "  }\n";
    code += "  std::printf(\"%lld\\n\", static_cast<long long>(answer));\n";
  }
  code += "  return 0;\n}\n";
  if (code.size() > max_emitted_bytes) return std::nullopt;
  return code;
}
[[nodiscard]] inline std::optional<std::string> emit_cpp_source(
    std::string_view source) {
  const auto parsed = parse_source(source);
  if (!parsed.ok()) return std::nullopt;
  return emit_cpp(parsed);
}
}  // namespace atto::forge::scalar
