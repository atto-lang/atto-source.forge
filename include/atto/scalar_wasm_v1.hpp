#pragma once
// Stage-0 restricted scalar .atto -> validated WebAssembly module.
// SECURITY SCOPE: pure, statically evaluable scalar expressions ONLY.
// No arbitrary guest native code, syscalls, imports, host pointers, actors,
// or untrusted JIT code. The exported peek/poke helpers are test-only probes.
#include "atto/scalar_eval.hpp"
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>
#include <tuple>

namespace atto::forge::sandbox {
using Bytes = std::vector<std::uint8_t>;
namespace detail {
inline void uleb(Bytes& out, std::uint64_t value) {
  do {
    auto byte = static_cast<std::uint8_t>(value & 0x7fU);
    value >>= 7U;
    if (value) byte |= 0x80U;
    out.push_back(byte);
  } while (value);
}
inline void sleb64(Bytes& out, std::int64_t value) {
  std::uint64_t bits = static_cast<std::uint64_t>(value);
  const bool negative = value < 0;
  for (int n = 0; n < 10; ++n) {
    auto byte = static_cast<std::uint8_t>(bits & 0x7fU);
    bits >>= 7U;
    if (negative) bits |= UINT64_C(0xfe00000000000000);
    const bool done = (bits == 0 && !(byte & 0x40U)) ||
                      (bits == UINT64_MAX && (byte & 0x40U));
    if (!done) byte |= 0x80U;
    out.push_back(byte);
    if (done) return;
  }
}
inline void name(Bytes& out, std::string_view text) {
  uleb(out, text.size());
  out.insert(out.end(), text.begin(), text.end());
}
inline void section(Bytes& module, std::uint8_t id, const Bytes& payload) {
  module.push_back(id);
  uleb(module, payload.size());
  module.insert(module.end(), payload.begin(), payload.end());
}
inline void emit_node(Bytes& out, const atto::forge::scalar::Parsed& p,
                      std::size_t index) {
  using atto::forge::scalar::Op;
  const auto& node = p.nodes[index];
  if (node.op == Op::literal) {
    out.push_back(0x42); // i64.const
    sleb64(out, node.value);
    return;
  }
  if (node.op == Op::neg) {
    out.push_back(0x42); sleb64(out, 0);
    emit_node(out, p, node.left);
    out.push_back(0x7d); // i64.sub
    return;
  }
  if (node.op == Op::bnot) {
    emit_node(out, p, node.left);
    out.push_back(0x42); sleb64(out, -1);
    out.push_back(0x85); // i64.xor
    return;
  }
  emit_node(out, p, node.left);
  emit_node(out, p, node.right);
  switch (node.op) {
    case Op::add: out.push_back(0x7c); break;
    case Op::sub: out.push_back(0x7d); break;
    case Op::mul: out.push_back(0x7e); break;
    case Op::div: out.push_back(0x7f); break;
    case Op::rem: out.push_back(0x81); break;
    case Op::band: out.push_back(0x83); break;
    case Op::bor: out.push_back(0x84); break;
    case Op::bxor: out.push_back(0x85); break;
    case Op::shl: out.push_back(0x86); break;
    case Op::lshr: out.push_back(0x88); break;
    case Op::ashr: out.push_back(0x87); break;
    default: break; // excluded by validate_ast; never reached
  }
}
inline void body(Bytes& code, const Bytes& instructions) {
  Bytes payload{0x00}; // zero local declarations
  payload.insert(payload.end(), instructions.begin(), instructions.end());
  payload.push_back(0x0b); // end
  uleb(code, payload.size());
  code.insert(code.end(), payload.begin(), payload.end());
}
} // namespace detail

// Source code contains only constant scalar main(); evaluate first so checked
// Atto arithmetic never silently inherits WebAssembly's wrapping behavior.
// The generated Wasm still executes the scalar expression at runtime.
// Accepting arbitrary Wasm blobs or external imports is NOT implemented.
[[nodiscard]] inline std::optional<Bytes> compile_scalar_wasm(std::string_view source) {
  using namespace atto::forge::scalar;
  if (source.size() > max_source_bytes) return std::nullopt;
  const auto parsed = parse_source(source);
  if (!validate_ast(parsed) || !evaluate(parsed).ok()) return std::nullopt;
  Bytes mod{0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00};
  // Types: main() -> i64; peek(i32) -> i32; poke(i32,i32) -> void.
  Bytes type{0x03, 0x60,0x00,0x01,0x7e,
                  0x60,0x01,0x7f,0x01,0x7f,
                  0x60,0x02,0x7f,0x7f,0x00};
  detail::section(mod, 1, type);
  detail::section(mod, 3, Bytes{0x03,0x00,0x01,0x02});
  // A private, non-shared, fixed-size 64 KiB linear memory per instance.
  detail::section(mod, 5, Bytes{0x01,0x01,0x01,0x01});
  Bytes exports{0x04};
  for (const auto& [name, kind, index] : {
         std::tuple<std::string_view,std::uint8_t,std::uint8_t>{"main",0,0},
         {"memory",2,0}, {"peek",0,1}, {"poke",0,2}}) {
    detail::name(exports, name);
    exports.push_back(kind);
    exports.push_back(index);
  }
  detail::section(mod, 7, exports);
  Bytes code{0x03};
  Bytes main;
  detail::emit_node(main, parsed, parsed.root);
  detail::body(code, main);
  // Test-only memory probes: bounds enforced by the Wasm engine.
  detail::body(code, Bytes{0x20,0x00,0x2d,0x00,0x00}); // peek
  detail::body(code, Bytes{0x20,0x00,0x20,0x01,0x3a,0x00,0x00}); // poke
  detail::section(mod, 10, code);
  return mod;
}
} // namespace atto::forge::sandbox
