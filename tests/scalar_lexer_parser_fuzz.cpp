#include "atto/scalar_parser.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
using namespace atto::forge::scalar;
int main() {
  std::uint32_t x = 0x12cafd95u;
  const std::string alphabet = "abcdefghijklmnopqrstuvwxyz0123456789/*=!\n\r\t<>+(){};: _";
  for (int run = 0; run < 10000; ++run) {
    x = x * 1664525u + 1013904223u;
    std::string s;
    const auto len = static_cast<std::size_t>(x % 290u);
    for (std::size_t i = 0; i < len; ++i) {
      x = x * 1664525u + 1013904223u;
      if ((x & 31u) == 0u) s.push_back(static_cast<char>(0xc3));
      else s.push_back(alphabet[x % alphabet.size()]);
    }
    const auto lexed = tokenize(s);
    if (lexed.ok()) {
      assert(!lexed.tokens.empty());
      assert(lexed.tokens.back().kind == Kind::end);
      assert(lexed.tokens.back().offset == s.size());
      for (const auto& tok : lexed.tokens) {
        assert(tok.offset <= s.size());
        assert(tok.length <= s.size() - tok.offset);
      }
    } else {
      assert(lexed.error_offset <= s.size());
    }
    const auto parsed = parse_source(s);
    if (parsed.ok()) assert(validate_ast(parsed));
  }
  std::cout << "10000 deterministic parser+lexer fuzz inputs passed\n";
}
