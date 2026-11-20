#include "atto/scalar_lexer.hpp"
#include <cassert>
#include <string>
#include <string_view>
using namespace atto::forge::scalar;

int main() {
  const std::string source = "pub fnc main(): int { return -9223372036854775808 + 3 * (8/2)%7; }";
  auto l = tokenize(source);
  assert(l.ok());
  assert(l.tokens.size() > 20);
  assert(l.tokens[0].kind == Kind::name);
  assert(source.substr(l.tokens[0].offset, l.tokens[0].length) == "pub");
  assert(l.tokens[1].kind == Kind::name);
  assert(l.tokens[2].kind == Kind::name);
  assert(l.tokens.back().kind == Kind::end);
  for (const auto& t : l.tokens) assert(t.offset + t.length <= source.size());
  assert(tokenize("\r\n fnc main () : int { return 0; }\t").ok());
  assert(tokenize("").ok());
  auto signatures = tokenize("fnc main() -> int {} int <- main() {}");
  assert(signatures.ok());
  bool saw_arrow = false, saw_left_arrow = false;
  for (const auto& token : signatures.tokens) {
    saw_arrow = saw_arrow || token.kind == Kind::arrow;
    saw_left_arrow = saw_left_arrow || token.kind == Kind::left_arrow;
  }
  assert(saw_arrow && saw_left_arrow);
  auto comments = tokenize("/* prefix */ 3 // line\n + /* block\nline */ 4 / 2");
  assert(comments.ok());
  assert(comments.tokens.size() == 6);
  assert(comments.tokens[0].kind == Kind::integer);
  assert(comments.tokens[1].kind == Kind::plus);
  assert(comments.tokens[3].kind == Kind::slash);
  const auto unterminated = tokenize("1 + /* not closed");
  assert(unterminated.error == LexError::unterminated_comment);
  assert(unterminated.error_offset == 4);
  assert(tokenize("// trailing comment without newline").ok());
  assert(tokenize("//" + std::string(max_source_bytes - 2, '/')).ok());
  assert(tokenize("/* */").tokens.size() == 1);
  assert(!tokenize("fnc main(){return @;}").ok());
  const std::string nul("1\0" "2", 3);
  auto bad = tokenize(std::string_view(nul));
  assert(bad.error == LexError::unsupported_byte && bad.error_offset == 1);
  // The Forge numeric shifts are spelled <<< and >>>. Two-character
  // directional channel I/O is a different syntax family and cannot be
  // accidentally interpreted as a shift or malformed one-byte tokens.
  const std::string directions = "c << 42; (c >>); c <<< 3; c >>> 2;";
  const auto ops_io = tokenize(directions);
  assert(ops_io.ok());
  int io_writes = 0, io_reads = 0, numeric_shifts = 0;
  for (const auto& op : ops_io.tokens) {
    if (op.kind == Kind::io_write) {
      ++io_writes;
      assert(directions.substr(op.offset, op.length) == "<<");
    }
    if (op.kind == Kind::io_read) {
      ++io_reads;
      assert(directions.substr(op.offset, op.length) == ">>");
    }
    if (op.kind == Kind::shl || op.kind == Kind::lshr) ++numeric_shifts;
  }
  assert(io_writes == 1 && io_reads == 1 && numeric_shifts == 2);
  assert(tokenize("c < < 42;").error == LexError::unsupported_byte);
  assert(tokenize("c > >;").error == LexError::unsupported_byte);
  assert(tokenize("// c << 42\n 7").ok());
  assert(tokenize("/* c >> */ 7").ok());
  const auto bits = tokenize("~1 & 2 | 3 ^ 4");
  assert(bits.ok());
  assert(bits.tokens[0].kind == Kind::tilde);
  assert(bits.tokens[2].kind == Kind::bit_and);
  assert(bits.tokens[4].kind == Kind::bit_or);
  assert(bits.tokens[6].kind == Kind::bit_xor);
  assert(tokenize("\xC3\xA9").error == LexError::unsupported_byte);
  assert(tokenize(std::string(max_source_bytes + 1, 'a')).error == LexError::source_too_large);
  assert(tokenize(std::string(max_tokens + 1, '+')).error == LexError::token_limit);
  assert(tokenize(std::string(max_tokens, '+')).ok());
}
