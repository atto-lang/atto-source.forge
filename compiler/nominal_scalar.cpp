#include "compiler/nominal_scalar.hpp"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace oresforge::nominal {
namespace {
constexpr std::size_t kMaxSourceBytes = 1024u * 1024u;
constexpr std::size_t kMaxTokens = 4096;
bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool valid_identifier(std::string_view word) {
  if (word.empty() || word.size() > 128) return false;
  const auto c = static_cast<unsigned char>(word.front());
  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
    return false;
  for (const unsigned char ch : word.substr(1))
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          digit(ch) || ch == '_')) return false;
  return true;
}
struct Token {
  std::string_view text{};
  std::size_t offset = 0;
};
class Parser {
 public:
  explicit Parser(std::string_view source) : source_(source) {
    if (source.size() > kMaxSourceBytes)
      throw std::runtime_error("nominal source exceeds 1 MiB limit");
    next();
  }
  ScalarClass parse() {
    // This is a deliberate opt-in compiler profile. There is no generic
    // fallback to the scalar expression compiler or an actor-state bypass.
    take("pub");
    expect("define");
    take("pub");
    expect("class");
    result_.owner = identifier();
    expect("as");
    expect("let");
    expect("int");
    result_.field = identifier();
    expect("=");
    result_.initializer = integer();
    expect(";");
    bool saw_writer = false, saw_reader = false;
    while (token_.text != "end") {
      if (token_.text.empty()) fail("unterminated nominal class");
      take("pub");
      const std::string name = identifier();
      expect("(");
      if (take("mut")) {
        expect("self");
        std::string parameter;
        if (take(")")) {
          expect("(");
          expect("int");
          parameter = identifier();
          expect(")");
        } else {
          expect(",");
          expect("int");
          parameter = identifier();
          expect(")");
        }
        if (saw_writer) fail("unsupported second mutable method");
        if (parameter == "self" || parameter == result_.field)
          fail("invalid or shadowed mutable receiver parameter");
        expect(":");
        expect("void");
        expect("{");
        expect("self");expect(".");expect(result_.field);
        expect("=");expect("self");expect(".");expect(result_.field);
        expect("+");expect(parameter);expect(";");
        if (take("return")) expect(";");
        expect("}");
        result_.writer = name;
        saw_writer = true;
      } else {
        // Unmarked zero-value methods have an implicit shared self receiver.
        if (token_.text == "self") next();
        expect(")");
        if (saw_reader) fail("unsupported second read method");
        if (!take(":")) expect("->");
        expect("int");
        expect("{");
        expect("return");expect("self");expect(".");
        expect(result_.field);expect(";");
        expect("}");
        result_.reader = name;
        saw_reader = true;
      }
    }
    expect("end");
    if (!saw_writer || !saw_reader)
      fail("bounded class requires one mutable writer and one reader");

    // Return/binding/receiver spelling never creates distinct overload slots.
    // Our two supported methods have different value arities (writer/1,
    // reader/0); general duplicate checking stays in the nominal compiler.
    take("pub");
    take("define");
    take("pub");
    expect("fnc");
    expect("main");
    expect("(");expect(")");
    if (!take(":")) expect("->");
    expect("int");
    expect("{");
    expect("let");expect("mut");expect(result_.owner);
    result_.binding = identifier();
    if (result_.binding == "self") fail("invalid self binding");
    expect("=");expect("new");expect(result_.owner);
    expect("(");expect(")");expect(";");
    expect(result_.binding);expect(".");expect(result_.writer);
    expect("(");
    result_.argument = integer();
    expect(")");expect(";");
    expect("return");expect(result_.binding);expect(".");
    expect(result_.reader);expect("(");expect(")");expect(";");
    expect("}");
    if (!token_.text.empty()) fail("unsupported trailing nominal source");
    return result_;
  }
 private:
  std::string_view source_;
  std::size_t index_ = 0;
  std::size_t seen_ = 0;
  Token token_{};
  ScalarClass result_{};

  [[noreturn]] void fail(const char* why) const {
    throw std::runtime_error(std::string(why) + " at byte " +
                             std::to_string(token_.offset));
  }
  void next() {
    if (++seen_ > kMaxTokens) fail("nominal token budget exceeded");
    for (;;) {
      if (index_ == source_.size()) {
        token_ = {{}, index_};return;
      }
      const auto c = static_cast<unsigned char>(source_[index_]);
      if (c == ' ' || c == '\n' || c == '\r' || c == '\t' ||
          c == '\f' || c == '\v') {++index_;continue;}
      if (source_.substr(index_,2) == "//") {
        index_ += 2;
        while (index_ < source_.size() &&
               source_[index_] != '\r' && source_[index_] != '\n')
          ++index_;
        continue;
      }
      if (source_.substr(index_,2) == "/*") {
        const std::size_t begin = index_;
        index_ += 2;
        std::size_t depth = 1;
        while (depth > 0) {
          if (index_ == source_.size())
            throw std::runtime_error("unterminated nominal comment at byte " +
                                     std::to_string(begin));
          if (source_.substr(index_,2) == "/*") {
            if (++depth > 32) fail("nominal comment nesting limit exceeded");
            index_ += 2;
          } else if (source_.substr(index_,2) == "*/") {
            --depth;index_ += 2;
          } else ++index_;
        }
        continue;
      }
      break;
    }
    const std::size_t begin = index_;
    const auto c = static_cast<unsigned char>(source_[index_++]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
      while (index_ < source_.size()) {
        const auto v = static_cast<unsigned char>(source_[index_]);
        if (!((v >= 'a' && v <= 'z') || (v >= 'A' && v <= 'Z') ||
              digit(v) || v == '_')) break;
        ++index_;
      }
    } else if (digit(c)) {
      while (index_ < source_.size() &&
             digit(static_cast<unsigned char>(source_[index_]))) ++index_;
    } else if (c == '-' && index_ < source_.size() && source_[index_] == '>') {
      ++index_;
    } else if (std::string_view("()+-=.;:,{}").find(static_cast<char>(c))
                   == std::string_view::npos) {
      throw std::runtime_error("invalid nominal source character at byte " +
                               std::to_string(begin));
    }
    token_ = {source_.substr(begin,index_-begin),begin};
  }
  bool take(std::string_view word) {
    if (token_.text != word) return false;
    next();return true;
  }
  void expect(std::string_view word) {
    if (!take(word)) fail("unexpected nominal token");
  }
  std::string identifier() {
    if (!valid_identifier(token_.text)) fail("invalid nominal identifier");
    std::string name(token_.text);
    next();return name;
  }
  std::int64_t integer() {
    bool negative = take("-");
    if (token_.text.empty() ||
        !digit(static_cast<unsigned char>(token_.text.front())))
      fail("expected nominal i64 literal");
    std::string literal;
    if (negative) literal.push_back('-');
    literal.append(token_.text);
    std::int64_t value = 0;
    const auto res = std::from_chars(literal.data(),
                                     literal.data()+literal.size(),value);
    if (res.ec != std::errc{} ||
        res.ptr != literal.data()+literal.size())
      fail("nominal i64 literal out of range");
    next();return value;
  }
};
void validate(const ScalarClass& p) {
  if (!valid_identifier(p.owner) || !valid_identifier(p.field) ||
      !valid_identifier(p.binding) || !valid_identifier(p.writer) ||
      !valid_identifier(p.reader))
    throw std::runtime_error("invalid nominal scalar class metadata");
}
} // namespace

ScalarClass parse(std::string_view source) {return Parser(source).parse();}

std::int64_t interpret(const ScalarClass& p) {
  validate(p);
  std::int64_t updated = 0;
  if (__builtin_add_overflow(p.initializer,p.argument,&updated))
    throw std::runtime_error("i64 add overflow");
  return updated;
}

std::string emit_x86_64_asm(const ScalarClass& p) {
  validate(p);
  // One fixed stack slot is the class's value field, no heap, GC or vtable.
  // The verified writer executes before the verified shared reader. The
  // native addition is checked at execution, not constant-folded away.
  return ".text\n.globl oresforge_entry\n.type oresforge_entry, @function\n"
         "oresforge_entry:\n"
         "  subq $16, %rsp\n"
         "  movabsq $" + std::to_string(p.initializer) + ", %rax\n"
         "  movq %rax, (%rsp)\n"
         "  movabsq $" + std::to_string(p.argument) + ", %rdx\n"
         "  movq (%rsp), %rax\n"
         "  addq %rdx, %rax\n"
         "  jo .Loresforge_nominal_overflow\n"
         "  movq %rax, (%rsp)\n"
         "  movq (%rsp), %rax\n"
         "  addq $16, %rsp\n"
         "  ret\n"
         ".Loresforge_nominal_overflow:\n"
         "  ud2\n"
         ".size oresforge_entry, .-oresforge_entry\n";
}
} // namespace oresforge::nominal
