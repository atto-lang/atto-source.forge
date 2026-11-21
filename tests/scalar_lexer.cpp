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
  const auto bits = tokenize("~1 & 2 | 3 ^^ 4");
  assert(bits.ok());
  assert(bits.tokens[0].kind == Kind::tilde);
  assert(bits.tokens[2].kind == Kind::bit_and);
  assert(bits.tokens[4].kind == Kind::bit_or);
  assert(bits.tokens[6].kind == Kind::bit_xor);
  assert(bits.tokens[6].length == 2);
  const auto caret = tokenize("fnc main(): int { ^ 42; }");
  assert(caret.ok());
  bool saw_caret = false;
  for (const auto& t : caret.tokens) {
    if (t.kind == Kind::caret_return) { saw_caret = true; assert(t.length == 1); }
  }
  assert(saw_caret);
  const auto bullet = tokenize("int\xE2\x80\xA2 main()\nbegin\n^ 42;\nend.");
  assert(bullet.ok());
  bool saw_bullet = false, saw_period = false;
  for (const auto& t : bullet.tokens) {
    if (t.kind == Kind::left_arrow && t.length == 3) saw_bullet = true;
    if (t.kind == Kind::period) saw_period = true;
  }
  assert(saw_bullet && saw_period);
  const auto split = tokenize("1 ^ /* not contiguous */ ^ 2");
  assert(split.ok());
  for (const auto& t : split.tokens) assert(t.kind != Kind::bit_xor);
  assert(tokenize("int\xE2\x80\xA3 main()").error == LexError::unsupported_byte);
  assert(tokenize("\xC3\xA9").error == LexError::unsupported_byte);
  assert(tokenize(std::string(max_source_bytes + 1, 'a')).error == LexError::source_too_large);
  assert(tokenize(std::string(max_tokens + 1, '+')).error == LexError::token_limit);
  assert(tokenize(std::string(max_tokens, '+')).ok());
  const auto directions = tokenize("c << 42; (c >>); c <<< 3; c >>> 2;");
  assert(directions.ok());
  int writes = 0, reads = 0, shifts = 0;
  for (const auto& token : directions.tokens) {
    if (token.kind == Kind::io_write) ++writes;
    if (token.kind == Kind::io_read) ++reads;
    if (token.kind == Kind::shl || token.kind == Kind::lshr) ++shifts;
  }
  assert(writes == 1 && reads == 1 && shifts == 2);
  assert(tokenize("(> 1,2)").ok());
  assert(tokenize("a = b").ok());
  assert(tokenize("a < b").ok());
  assert(tokenize("// c << 42\n 7").ok());

}
