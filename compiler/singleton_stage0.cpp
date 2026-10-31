#include "compiler/singleton_stage0.hpp"
#include "compiler/frontend.hpp"
#include "compiler/source_utf8.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oresforge {
namespace {

constexpr std::size_t kMaxSourceBytes = 1024u * 1024u;
constexpr std::size_t kMaxModuleIdentityBytes = 4096;
constexpr std::size_t kMaxDeclarationNameBytes = 256;

constexpr bool ascii_space(unsigned char c) {
  return c==' ' || c=='\t' || c=='\n' || c=='\r' || c=='\v' || c=='\f';
}
constexpr bool ascii_alpha(unsigned char c) {
  return (c>='A' && c<='Z') || (c>='a' && c<='z');
}
constexpr bool ascii_digit(unsigned char c) { return c>='0' && c<='9'; }
constexpr bool ascii_alnum(unsigned char c) {
  return ascii_alpha(c) || ascii_digit(c);
}

struct Token {
  std::string_view text;
  std::size_t begin = 0;
  std::size_t end = 0;
  bool identifier = false;
};

class Lexer final {
 public:
  explicit Lexer(std::string_view source) : source_(source) {}

  Token next() {
    skip_space_and_comments();
    const std::size_t begin = at_;
    if (at_ == source_.size()) return {{}, begin, begin, false};

    unsigned char c = static_cast<unsigned char>(source_[at_]);
    if (ascii_alpha(c) || c == '_') {
      ++at_;
      while (at_ < source_.size()) {
        c = static_cast<unsigned char>(source_[at_]);
        if (!ascii_alnum(c) && c != '_') break;
        ++at_;
      }
      return {source_.substr(begin, at_ - begin), begin, at_, true};
    }

    // The stage-0 singleton shell has no punctuation-bearing body grammar. We
    // still tokenize one byte at a time so diagnostics report the first
    // unsupported body byte deterministically instead of skipping it.
    ++at_;
    return {source_.substr(begin, 1), begin, at_, false};
  }

 private:
  std::string_view source_;
  std::size_t at_ = 0;

  [[noreturn]] void fail(const char* message, std::size_t offset) const {
    throw std::runtime_error(
        std::string(message) + " at byte " + std::to_string(offset));
  }

  void skip_space_and_comments() {
    for (;;) {
      while (at_ < source_.size() &&
             ascii_space(static_cast<unsigned char>(source_[at_])))
        ++at_;
      if (at_ + 1 >= source_.size() || source_[at_] != '/') return;

      if (source_[at_ + 1] == '/') {
        at_ += 2;
        while (at_ < source_.size() &&
               source_[at_] != '\n' && source_[at_] != '\r')
          ++at_;
        continue;
      }

      if (source_[at_ + 1] != '*') return;
      const std::size_t start = at_;
      at_ += 2;
      std::size_t depth = 1;
      while (depth != 0) {
        if (at_ == source_.size()) fail("unterminated block comment", start);
        if (at_ + 1 < source_.size() &&
            source_[at_] == '/' && source_[at_ + 1] == '*') {
          if (++depth > 128) fail("block comment nesting too deep", start);
          at_ += 2;
        } else if (at_ + 1 < source_.size() &&
                   source_[at_] == '*' && source_[at_ + 1] == '/') {
          --depth;
          at_ += 2;
        } else {
          ++at_;
        }
      }
    }
  }
};

[[noreturn]] void fail(const char* category, const char* message,
                       std::size_t offset) {
  throw std::runtime_error(
      std::string(category) + ": " + message +
      " at byte " + std::to_string(offset));
}

std::uint64_t stable_id(std::string_view domain, std::string_view value) {
  // FNV-1a is used only for deterministic stage-0 compiler identity. This is
  // not a security capability or a final wire identity. The full compiler must
  // replace this bootstrap mapping before parity promotion.
  std::uint64_t hash = UINT64_C(1469598103934665603);
  auto mix = [&](unsigned char byte) {
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= UINT64_C(1099511628211);
  };
  for (unsigned char c : domain) mix(c);
  mix(0xff);
  for (unsigned char c : value) mix(c);
  return hash == 0 ? 1 : hash;
}

} // namespace

SingletonStage0Plan compile_singleton_stage0(
    std::string_view source,
    std::string_view module_identity) {
  ::oresforge::source::validate_utf8(source);
  if (source.size() > kMaxSourceBytes)
    throw std::runtime_error("singleton stage0 source exceeds byte budget");
  if (module_identity.empty() || module_identity.size() > kMaxModuleIdentityBytes)
    throw std::invalid_argument("invalid resolved module identity");

  Lexer lexer(source);
  Token token = lexer.next();
  const std::size_t declaration_start = token.begin;
  bool defined = false;
  bool public_ = false;

  while (token.text == "define" || token.text == "pub") {
    if (token.text == "define") {
      if (defined) fail("ORES-SINGLETON-STAGE0-SYNTAX",
                        "duplicate define modifier", token.begin);
      defined = true;
    } else {
      if (public_) fail("ORES-SINGLETON-STAGE0-SYNTAX",
                        "duplicate pub modifier", token.begin);
      public_ = true;
    }
    token = lexer.next();
  }

  if (!defined)
    fail("ORES-SINGLETON-STAGE0-SYNTAX",
         "stage0 requires an explicit define singleton declaration",
         declaration_start);
  if (token.text != "singleton")
    fail("ORES-SINGLETON-STAGE0-SYNTAX",
         "expected singleton declaration", token.begin);

  token = lexer.next();
  if (!token.identifier)
    fail("ORES-SINGLETON-STAGE0-SYNTAX",
         "expected singleton declaration name", token.begin);
  if (token.text.size() > kMaxDeclarationNameBytes)
    fail("ORES-SINGLETON-STAGE0-SYNTAX",
         "singleton declaration name exceeds byte budget", token.begin);
  const std::string declaration_name(token.text);

  token = lexer.next();
  if (token.text != "as")
    fail("ORES-SINGLETON-STAGE0-SYNTAX",
         "expected 'as' after singleton declaration name", token.begin);

  token = lexer.next();
  bool body_nonempty = false;
  std::size_t first_body_offset = token.begin;
  while (token.text != "end") {
    if (token.text.empty())
      fail("ORES-SINGLETON-STAGE0-SYNTAX",
           "unterminated singleton declaration", token.begin);
    body_nonempty = true;
    if (token.identifier && token.text == declaration_name)
      fail("ORES-SINGLETON-PREPUBLICATION-ESCAPE",
           "singleton initializer references its receiver before publication",
           token.begin);
    token = lexer.next();
  }
  const std::size_t declaration_end = token.end;

  if (body_nonempty)
    fail("ORES-SINGLETON-STAGE0-BODY-UNSUPPORTED",
         "singleton body/member syntax is not frozen for Forge stage0",
         first_body_offset);

  std::string remainder;
  remainder.reserve(source.size() - (declaration_end - declaration_start));
  remainder.append(source.substr(0, declaration_start));
  remainder.append(source.substr(declaration_end));

  SingletonStage0Plan plan;
  plan.declaration_name = declaration_name;
  plan.declaration_id = {
      stable_id("oreslang.module.v1", module_identity),
      stable_id("oreslang.singleton.v1", declaration_name)
  };
  plan.initializer_program = parse_source(remainder);
  return plan;
}

} // namespace oresforge
