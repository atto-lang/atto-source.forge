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
  // Lexer identifies proposed collection syntax but never interprets its
  // values in the stage-0 scalar compiler.
  auto question = tokenize("int.parse(x)?");
  assert(question.ok());
  bool saw_question = false;
  for (const auto& token : question.tokens)
    saw_question = saw_question || token.kind == Kind::question;
  assert(saw_question);
  const auto quoted_effect = tokenize(R"("trap Result<int,Error> ?")");
  assert(quoted_effect.ok() && quoted_effect.tokens[0].kind == Kind::quoted);

  const std::string entry_source =
      R"(let e = "foo" => 5 (as Entry<string,int>);)";
  auto collection = tokenize(entry_source);
  assert(collection.ok());
  bool saw_fat_arrow = false, saw_quoted = false;
  for (const auto& token : collection.tokens) {
    saw_fat_arrow = saw_fat_arrow || token.kind == Kind::fat_arrow;
    saw_quoted = saw_quoted || token.kind == Kind::quoted;
  }
  assert(saw_fat_arrow && saw_quoted);
  auto static_record = tokenize(R"(r["content-type"] = 3;)");
  assert(static_record.ok());
  bool bracket_seen = false;
  for (const auto& token : static_record.tokens)
    bracket_seen = bracket_seen || token.kind == Kind::lbracket;
  assert(bracket_seen);
  auto quoted_arrow = tokenize(R"("Entry => Map[foo]")");
  assert(quoted_arrow.ok());
  assert(quoted_arrow.tokens[0].kind == Kind::quoted);
  assert(quoted_arrow.tokens[1].kind == Kind::end);
  assert(tokenize(R"("unfinished)").error == LexError::unterminated_quoted);
  assert(tokenize(std::string("\"", 1) + std::string(4097, 'x') + "\"").error
         == LexError::quoted_too_large);
  assert(tokenize(R"("escaped \" => Entry")").ok());

}
