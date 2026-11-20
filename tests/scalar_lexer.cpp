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
  // Nested ownership-looking comments cannot leak executable tokens past
  // the first */. The byte offset for the next real token is preserved.
  const std::string nested =
      "/* outer /* ref!item deref!h unref!h */ still outer */ 42";
  auto inert = tokenize(nested);
  assert(inert.ok());
  assert(inert.tokens.size() == 2);
  assert(inert.tokens[0].kind == Kind::integer);
  assert(inert.tokens[0].offset == nested.find("42"));
  assert(tokenize("/* outer /* ref!item */").error == LexError::unterminated_comment);
  std::string comments64, comments65;
  for (int n = 0; n < 64; ++n) comments64 += "/*";
  for (int n = 0; n < 64; ++n) comments64 += "*/";
  comments65 = "/*" + comments64 + "*/";
  assert(tokenize(comments64 + "42").ok());
  const auto overdepth = tokenize(comments65);
  assert(overdepth.error == LexError::comment_nesting_limit);
  assert(overdepth.error_offset == 128);
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
  const auto bits = tokenize("~1 & 2 | 3 ^ 4");
  assert(bits.ok());
  assert(bits.tokens[0].kind == Kind::tilde);
  assert(bits.tokens[2].kind == Kind::bit_and);
  assert(bits.tokens[4].kind == Kind::bit_or);
  assert(bits.tokens[6].kind == Kind::bit_xor);
  // The native stage-0 lexer cannot lower move/borrow or checked handle
  // effects. These spellings must be denied with an ownership-specific
  // diagnostic before the compiler attempts to emit a C++ guest.
  for (const std::string_view src : {
       "copy!42", "take!item", "borrow! mut item", "mut borrow!item",
       "share!item", "pin!item", "drop!item", "ref!item",
       "ref!(item)", "deref!handle", "unref!handle", "ref!eqfoo" }) {
    const auto result = tokenize(src);
    assert(result.error == LexError::ownership_bang_unsupported);
    assert(result.error_offset < src.size());
  }
  // Comments and non-ownership punctuation must not be misclassified.
  assert(tokenize("/* ref!item deref!item */ 42").ok());
  assert(tokenize("// unref!item\n42").ok());
  for (const std::string_view src : {
       "ref !item", "ref!=item", "deref!eq item", "unref!eq item",
       "copy!eq item", "other!item" }) {
    assert(tokenize(src).error == LexError::unsupported_byte);
  }
  assert(tokenize("\xC3\xA9").error == LexError::unsupported_byte);
  assert(tokenize(std::string(max_source_bytes + 1, 'a')).error == LexError::source_too_large);
  assert(tokenize(std::string(max_tokens + 1, '+')).error == LexError::token_limit);
  assert(tokenize(std::string(max_tokens, '+')).ok());
}
