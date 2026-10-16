#include "compiler/frontend.hpp"
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
namespace oresforge {
namespace {
// Shared parser admission includes callers that bypass the CLI file reader.
// Reject invalid Unicode scalar encodings and embedded NUL before tokenization.
void validate_source_utf8(std::string_view text) {
  for (std::size_t i=0; i<text.size();) {
    const auto lead=static_cast<unsigned char>(text[i]);
    if (lead==0) throw std::runtime_error("source contains NUL byte");
    if (lead<=0x7f) { ++i; continue; }
    std::size_t following=0;
    unsigned char low=0x80, high=0xbf;
    if (lead>=0xc2 && lead<=0xdf) following=1;
    else if (lead>=0xe0 && lead<=0xef) {
      following=2;
      if (lead==0xe0) low=0xa0; // overlong three-byte scalar
      if (lead==0xed) high=0x9f; // UTF-16 surrogate
    } else if (lead>=0xf0 && lead<=0xf4) {
      following=3;
      if (lead==0xf0) low=0x90; // overlong four-byte scalar
      if (lead==0xf4) high=0x8f; // beyond U+10FFFF
    } else throw std::runtime_error("source is not valid UTF-8");
    if (text.size()-i<=following)
      throw std::runtime_error("source is not valid UTF-8");
    for (std::size_t j=1; j<=following; ++j) {
      const auto b=static_cast<unsigned char>(text[i+j]);
      if (b<(j==1?low:0x80) || b>(j==1?high:0xbf))
        throw std::runtime_error("source is not valid UTF-8");
    }
    i+=following+1;
  }
}
enum class Kind { Word, Number, Punct, End };
struct Token { Kind kind; std::string_view text; std::size_t offset; };
class Parser {
 public:
  explicit Parser(std::string_view text): source_(text) { next(); }
  Program parse() {
    bool defined=false, public_=false;
    while (tok_.text=="define" || tok_.text=="pub") {
      if (tok_.text=="define") {
        if (defined) error("duplicate define");
        defined=true;
      } else {
        if (public_) error("duplicate pub");
        public_=true;
      }
      next();
    }
    expect("fnc"); expect("main"); expect("("); expect(")");
    if (!take(":")) expect("->");
    expect("int"); expect("{"); expect("return");
    const int result=expr(0);
    expect(";"); expect("}");
    if (tok_.kind!=Kind::End) error("trailing code");
    add(Op::Return,0,result);
    verify(program_);
    return std::move(program_);
  }
 private:
  std::string_view source_;
  std::size_t at_=0;
  Token tok_{Kind::End,{},0};
  Program program_;
  static constexpr std::size_t kMaxExpressionDepth = 128;
  static constexpr std::size_t kMaxInstructions = 100000;
  static void check_depth(std::size_t depth) {
    if (depth > kMaxExpressionDepth) throw std::runtime_error("expression nesting too deep");
  }
  [[noreturn]] void error(const char* s) const {
    throw std::runtime_error(std::string(s)+" at byte "+std::to_string(tok_.offset));
  }
  void next() {
    while (at_<source_.size()) {
      unsigned char c=static_cast<unsigned char>(source_[at_]);
      if (std::isspace(c)) { ++at_; continue; }
      if (source_[at_]=='/' && at_+1<source_.size() && source_[at_+1]=='/') {
        at_+=2;
        while (at_<source_.size() && source_[at_]!='\n') ++at_;
        continue;
      }
      break;
    }
    const auto start=at_;
    if (at_==source_.size()) { tok_={Kind::End,{},start}; return; }
    unsigned char c=static_cast<unsigned char>(source_[at_]);
    if (std::isalpha(c) || c=='_') {
      ++at_;
      while (at_<source_.size()) {
        c=static_cast<unsigned char>(source_[at_]);
        if (!std::isalnum(c) && c!='_') break;
        ++at_;
      }
      tok_={Kind::Word,source_.substr(start,at_-start),start};
      // The bootstrap parser cannot resolve any Java/Graal import forms.
      // Reject with a stable diagnostic rather than emitting an unrelated
      // scalar result from a source that depends on external modules.
      if (tok_.text=="import") error("source imports require verified module graph lowering");
      // Fail closed on named language features before the scalar grammar
      // gives a misleading generic syntax error (or ever learns to skip them).
      // These require typed method dispatch and ownership-checked closure IR.
      if (tok_.text=="class" || tok_.text=="actor")
        error("class/actor methods require typed native lowering");
      if (tok_.text=="lex" || tok_.text=="nlex")
        error("lexical captures require checked closure lowering");
      return;
    }
    if (std::isdigit(c)) {
      ++at_;
      while (at_<source_.size() &&
        std::isdigit(static_cast<unsigned char>(source_[at_]))) ++at_;
      tok_={Kind::Number,source_.substr(start,at_-start),start}; return;
    }
    if (source_[at_]=='-' && at_+1<source_.size() && source_[at_+1]=='>') {
      at_+=2;
      tok_={Kind::Punct,source_.substr(start,2),start}; return;
    }
    // Plain |args| closures are unsupported just like lex/nlex closures.
    if (c=='|') error("lexical captures require checked closure lowering");
    if (std::string_view("()+-*/{};:").find(static_cast<char>(c)) == std::string_view::npos)
      error("invalid character");
    ++at_;
    tok_={Kind::Punct,source_.substr(start,1),start};
  }
  bool take(std::string_view value) {
    if (tok_.text!=value) return false;
    next(); return true;
  }
  void expect(std::string_view value) {
    if (!take(value)) error("unexpected token");
  }
  int add(Op op, std::int64_t value=0, int a=-1, int b=-1) {
    if (program_.instructions.size()>=kMaxInstructions)
      error("too many instructions");
    int index=static_cast<int>(program_.instructions.size());
    program_.instructions.push_back({op,value,a,b});
    return index;
  }
  int expr(std::size_t depth) {
    check_depth(depth);
    int left=term(depth+1);
    while (tok_.text=="+" || tok_.text=="-") {
      bool plus=tok_.text=="+";
      next(); int right=term(depth+1);
      left=add(plus?Op::Add:Op::Sub,0,left,right);
    }
    return left;
  }
  int term(std::size_t depth) {
    check_depth(depth);
    int left=factor(depth+1);
    while (tok_.text=="*" || tok_.text=="/") {
      bool mult=tok_.text=="*";
      if (!mult) error("division requires float result lowering; unsupported in the i64 source subset");
      next(); int right=factor(depth+1);
      left=add(mult?Op::Mul:Op::Div,0,left,right);
    }
    return left;
  }
  int factor(std::size_t depth) {
    check_depth(depth);
    if (take("-")) {
      // The signed i64 minimum is valid, though its positive magnitude
      // cannot be represented as an i64. Admit only this exact negative lexeme.
      if (tok_.kind==Kind::Number && tok_.text=="9223372036854775808") {
        next();
        return add(Op::Constant,std::numeric_limits<std::int64_t>::min());
      }
      int zero=add(Op::Constant,0);
      int value=factor(depth+1);
      return add(Op::Sub,0,zero,value);
    }
    if (take("+")) return factor(depth+1);
    if (take("(")) { int value=expr(depth+1); expect(")"); return value; }
    if (tok_.kind!=Kind::Number) error("expected integer");
    std::int64_t n=0;
    const char* begin=tok_.text.data();
    auto parsed=std::from_chars(begin,begin+tok_.text.size(),n);
    if (parsed.ec!=std::errc{} || parsed.ptr!=begin+tok_.text.size()) error("invalid i64");
    next(); return add(Op::Constant,n);
  }
};
}
Program parse_source(std::string_view source) {
  if (source.size() > 1024 * 1024) throw std::runtime_error("source exceeds 1 MiB bootstrap limit");
  validate_source_utf8(source);
  return Parser(source).parse();
}
}
