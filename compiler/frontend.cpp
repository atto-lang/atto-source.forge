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
    expect("fnc"); expect("main"); expect("("); expect(")"); expect(":");
    expect("int"); expect("{"); expect("return");
    const int result=expr();
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
      tok_={Kind::Word,source_.substr(start,at_-start),start}; return;
    }
    if (std::isdigit(c)) {
      ++at_;
      while (at_<source_.size() &&
        std::isdigit(static_cast<unsigned char>(source_[at_]))) ++at_;
      tok_={Kind::Number,source_.substr(start,at_-start),start}; return;
    }
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
    if (program_.instructions.size()>=static_cast<std::size_t>(std::numeric_limits<int>::max()))
      error("too many instructions");
    int index=static_cast<int>(program_.instructions.size());
    program_.instructions.push_back({op,value,a,b});
    return index;
  }
  int expr() {
    int left=term();
    while (tok_.text=="+" || tok_.text=="-") {
      bool plus=tok_.text=="+";
      next(); int right=term();
      left=add(plus?Op::Add:Op::Sub,0,left,right);
    }
    return left;
  }
  int term() {
    int left=factor();
    while (tok_.text=="*" || tok_.text=="/") {
      bool mult=tok_.text=="*";
      next(); int right=factor();
      left=add(mult?Op::Mul:Op::Div,0,left,right);
    }
    return left;
  }
  int factor() {
    if (take("-")) { int zero=add(Op::Constant,0); int value=factor(); return add(Op::Sub,0,zero,value); }
    if (take("+")) return factor();
    if (take("(")) { int value=expr(); expect(")"); return value; }
    if (tok_.kind!=Kind::Number) error("expected integer");
    std::int64_t n=0;
    const char* begin=tok_.text.data();
    auto parsed=std::from_chars(begin,begin+tok_.text.size(),n);
    if (parsed.ec!=std::errc{} || parsed.ptr!=begin+tok_.text.size()) error("invalid i64");
    next(); return add(Op::Constant,n);
  }
};
}
Program parse_source(std::string_view source) { return Parser(source).parse(); }
}
