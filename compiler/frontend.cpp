#include "compiler/frontend.hpp"
#include <algorithm>
#include <charconv>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
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
// Grammar admission is deliberately ASCII-only. C-library isalpha/isspace
// change across locales, which is incompatible with reproducible bootstraps.
// UTF-8 remains accepted in comments but not identifiers/spacing.
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
enum class Kind { Word, Number, Punct, End };
struct Token { Kind kind; std::string_view text; std::size_t offset; };

// This stage-0 subset has pure i64 return expressions only. Inlining is
// semantically equivalent to invoking such a function, but NOT native ABI
// calls, stack frames, mutable locals, recursion, or dynamic dispatch.
struct Expr {
  enum class Tag { Constant, Parameter, Negative, Add, Sub, Mul, Call };
  Tag tag;
  std::int64_t value = 0;
  std::string name;
  std::size_t offset = 0;
  std::vector<std::unique_ptr<Expr>> children;
  std::size_t depth = 1;
};
using ExprPtr = std::unique_ptr<Expr>;
struct Function {
  std::vector<std::string> parameters;
  ExprPtr body;
};

class Parser {
 public:
  explicit Parser(std::string_view text,bool runtime_entry=false): source_(text),runtime_entry_(runtime_entry) { next(); }
  Program parse() {
    while (tok_.kind != Kind::End) definition();
    if (!functions_.contains("main")) error("missing main function");
    if (!runtime_entry_ && !functions_.at("main").parameters.empty())
      error("main must have zero parameters");
    if (runtime_entry_ && functions_.at("main").parameters.size()>4)
      error("runtime entry parameter budget exceeded");
    // Validate all definitions, including unreachable ones. Memoized DFS is
    // linear in the call graph rather than exponential for diamond imports.
    std::unordered_set<std::string> complete;
    std::vector<std::string> checking;
    std::vector<std::string> names;
    names.reserve(functions_.size());
    for (const auto& entry : functions_) names.push_back(entry.first);
    std::sort(names.begin(),names.end());
    for (const auto& name : names)
      validate_calls(name,checking,complete);
    std::vector<std::string> active;
    const auto& main=functions_.at("main");
    std::unordered_map<std::string,int> bindings;
    if (runtime_entry_) {
      program_.parameter_count=main.parameters.size();
      for (std::size_t i=0; i<main.parameters.size(); ++i)
        bindings.emplace(main.parameters[i],add(Op::Parameter,
                                               static_cast<std::int64_t>(i)));
    }
    const int result=lower(*main.body,bindings,active,0);
    add(Op::Return,0,result);
    verify(program_);
    return std::move(program_);
  }
 private:
  std::string_view source_;
  bool runtime_entry_ = false;
  std::size_t at_=0;
  Token tok_{Kind::End,{},0};
  Program program_;
  std::unordered_map<std::string,Function> functions_;
  std::size_t node_count_=0;
  static constexpr std::size_t kMaxExpressionDepth=128;
  static constexpr std::size_t kMaxCallDepth=64;
  static constexpr std::size_t kMaxInstructions=100000;
  static constexpr std::size_t kMaxNodes=100000;
  static void check_depth(std::size_t depth) {
    if (depth>kMaxExpressionDepth) throw std::runtime_error("expression nesting too deep");
  }
  [[noreturn]] void error(const char* s) const {
    throw std::runtime_error(std::string(s)+" at byte "+std::to_string(tok_.offset));
  }
  void next() {
    while (at_<source_.size()) {
      const auto c=static_cast<unsigned char>(source_[at_]);
      if (ascii_space(c)) { ++at_; continue; }
      if (source_[at_]=='/' && at_+1<source_.size() && source_[at_+1]=='/') {
        at_+=2;
        while (at_<source_.size() && source_[at_]!='\n') ++at_;
        continue;
      }
      break;
    }
    const auto start=at_;
    if (at_==source_.size()) { tok_={Kind::End,{},start}; return; }
    auto c=static_cast<unsigned char>(source_[at_]);
    if (ascii_alpha(c) || c=='_') {
      ++at_;
      while (at_<source_.size()) {
        c=static_cast<unsigned char>(source_[at_]);
        if (!ascii_alnum(c) && c!='_') break;
        ++at_;
      }
      tok_={Kind::Word,source_.substr(start,at_-start),start};
      if (tok_.text=="import") error("source imports require verified module graph lowering");
      if (tok_.text=="cpu" || tok_.text=="gpu" || tok_.text=="nb")
        error("execution region requires verified CPU/GPU Task lowering; no implicit CPU fallback");
      if (tok_.text=="task") {
        auto look=at_;
        while (look<source_.size() &&
               ascii_space(static_cast<unsigned char>(source_[look]))) ++look;
        if (look<source_.size() && source_[look]=='{')
          error("execution task requires scheduler-aware Future/continuation lowering; no implicit inline execution");
      }
      return;
    }
    if (ascii_digit(c)) {
      ++at_;
      while (at_<source_.size() &&
        ascii_digit(static_cast<unsigned char>(source_[at_]))) ++at_;
      tok_={Kind::Number,source_.substr(start,at_-start),start}; return;
    }
    if (source_[at_]=='-' && at_+1<source_.size() && source_[at_+1]=='>') {
      at_+=2; tok_={Kind::Punct,source_.substr(start,2),start}; return;
    }
    if (std::string_view("()+-*/{};:,").find(static_cast<char>(c)) ==
        std::string_view::npos) error("invalid character");
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
  std::string identifier() {
    if (tok_.kind!=Kind::Word) error("expected identifier");
    std::string name(tok_.text);
    next(); return name;
  }
  ExprPtr node(Expr::Tag tag, std::vector<ExprPtr> children={},
               std::int64_t value=0, std::string name={},
               std::size_t offset=0) {
    if (++node_count_>kMaxNodes) error("expression AST budget exceeded");
    std::size_t depth=1;
    for (const auto& child : children) {
      depth=std::max(depth,child->depth+1);
    }
    check_depth(depth);
    auto e=std::make_unique<Expr>();
    e->tag=tag; e->children=std::move(children); e->value=value;
    e->name=std::move(name); e->depth=depth; e->offset=offset;
    return e;
  }
  ExprPtr binary(Expr::Tag op,ExprPtr a,ExprPtr b) {
    std::vector<ExprPtr> v;
    v.push_back(std::move(a));
    v.push_back(std::move(b));
    return node(op,std::move(v));
  }
  void definition() {
    bool defined=false, public_=false;
    while (tok_.text=="define" || tok_.text=="pub") {
      if (take("define")) { if (defined) error("duplicate define"); defined=true; }
      else if (take("pub")) { if (public_) error("duplicate pub"); public_=true; }
    }
    expect("fnc");
    const std::string name=identifier();
    if (name=="int" || name=="fnc" || name=="return")
      error("reserved function name");
    expect("(");
    Function fn;
    if (tok_.text!=")") {
      do {
        const std::string parameter=identifier();
        if (parameter=="int" || parameter=="fnc" || parameter=="return" ||
            std::find(fn.parameters.begin(),fn.parameters.end(),parameter)!=fn.parameters.end())
          error("invalid or duplicate parameter");
        expect(":"); expect("int");
        fn.parameters.push_back(parameter);
        if (fn.parameters.size()>64) error("too many parameters");
        if (!take(",")) break;
      } while (true);
    }
    expect(")");
    if (!take(":")) expect("->");
    expect("int"); expect("{"); expect("return");
    fn.body=expression(0);
    expect(";"); expect("}");
    if (functions_.contains(name)) error("duplicate function");
    functions_.emplace(name,std::move(fn));
  }
  ExprPtr expression(std::size_t depth) {
    check_depth(depth);
    auto a=term(depth+1);
    while (tok_.text=="+" || tok_.text=="-") {
      bool plus=tok_.text=="+";
      next();
      a=binary(plus?Expr::Tag::Add:Expr::Tag::Sub,
               std::move(a),term(depth+1));
    }
    return a;
  }
  ExprPtr term(std::size_t depth) {
    check_depth(depth);
    auto a=factor(depth+1);
    while (tok_.text=="*" || tok_.text=="/") {
      if (tok_.text=="/")
        error("division requires float result lowering; unsupported in the i64 source subset");
      next();
      a=binary(Expr::Tag::Mul,std::move(a),factor(depth+1));
    }
    return a;
  }
  ExprPtr factor(std::size_t depth) {
    check_depth(depth);
    if (take("-")) {
      if (tok_.kind==Kind::Number && tok_.text=="9223372036854775808") {
        next();
        return node(Expr::Tag::Constant,{},std::numeric_limits<std::int64_t>::min());
      }
      std::vector<ExprPtr> v;
      v.push_back(factor(depth+1));
      return node(Expr::Tag::Negative,std::move(v));
    }
    if (take("+")) return factor(depth+1);
    if (take("(")) {
      auto e=expression(depth+1); expect(")"); return e;
    }
    if (tok_.kind==Kind::Number) {
      std::int64_t n=0;
      const char* begin=tok_.text.data();
      const auto parsed=std::from_chars(begin,begin+tok_.text.size(),n);
      if (parsed.ec!=std::errc{} || parsed.ptr!=begin+tok_.text.size())
        error("invalid i64");
      next(); return node(Expr::Tag::Constant,{},n);
    }
    if (tok_.kind==Kind::Word) {
      const std::size_t offset=tok_.offset;
      if (tok_.text=="match" || tok_.text=="switch")
        error("expected integer");
      const std::string name=identifier();
      if (!take("(")) return node(Expr::Tag::Parameter,{},0,name,offset);
      std::vector<ExprPtr> args;
      if (tok_.text!=")") {
        do {
          args.push_back(expression(depth+1));
          if (args.size()>64) error("too many call arguments");
          if (!take(",")) break;
        } while (true);
      }
      expect(")");
      return node(Expr::Tag::Call,std::move(args),0,name);
    }
    error("expected integer");
  }
  int add(Op op,std::int64_t value=0,int a=-1,int b=-1) {
    if (program_.instructions.size()>=kMaxInstructions)
      error("too many instructions");
    const int idx=static_cast<int>(program_.instructions.size());
    program_.instructions.push_back({op,value,a,b}); return idx;
  }
  void walk_calls(const Expr& e,const Function& owner,
                  std::vector<std::string>& active,
                  std::unordered_set<std::string>& complete) const {
    if (e.tag==Expr::Tag::Parameter &&
        std::find(owner.parameters.begin(),owner.parameters.end(),e.name)==
        owner.parameters.end())
      throw std::runtime_error("expected integer at byte "+std::to_string(e.offset));
    if (e.tag==Expr::Tag::Call) {
      auto found=functions_.find(e.name);
      if (found==functions_.end())
        throw std::runtime_error("unknown function: "+e.name);
      if (found->second.parameters.size()!=e.children.size())
        throw std::runtime_error("function arity mismatch: "+e.name);
      validate_calls(e.name,active,complete);
    }
    for (const auto& child:e.children)
      walk_calls(*child,owner,active,complete);
  }
  void validate_calls(const std::string& name,
                      std::vector<std::string>& active,
                      std::unordered_set<std::string>& complete) const {
    if (std::find(active.begin(),active.end(),name)!=active.end())
      throw std::runtime_error("recursive calls not supported: "+name);
    if (complete.contains(name)) return;
    if (active.size()>=kMaxCallDepth)
      throw std::runtime_error("function call depth exceeded");
    active.push_back(name);
    const auto& fn=functions_.at(name);
    walk_calls(*fn.body,fn,active,complete);
    active.pop_back();
    complete.insert(name);
  }
  int lower(const Expr& e,const std::unordered_map<std::string,int>& env,
            std::vector<std::string>& active,std::size_t call_depth) {
    if (call_depth>kMaxCallDepth)
      throw std::runtime_error("function call depth exceeded");
    switch (e.tag) {
      case Expr::Tag::Constant: return add(Op::Constant,e.value);
      case Expr::Tag::Parameter: {
        auto found=env.find(e.name);
        if (found==env.end()) error("expected integer");
        return found->second;
      }
      case Expr::Tag::Negative: {
        const int zero=add(Op::Constant,0);
        const int x=lower(*e.children.at(0),env,active,call_depth);
        return add(Op::Sub,0,zero,x);
      }
      case Expr::Tag::Add: case Expr::Tag::Sub: case Expr::Tag::Mul: {
        const int a=lower(*e.children.at(0),env,active,call_depth);
        const int b=lower(*e.children.at(1),env,active,call_depth);
        const Op op=e.tag==Expr::Tag::Add?Op::Add:
                    e.tag==Expr::Tag::Sub?Op::Sub:Op::Mul;
        return add(op,0,a,b);
      }
      case Expr::Tag::Call: {
        auto found=functions_.find(e.name);
        if (found==functions_.end()) throw std::runtime_error("unknown function: "+e.name);
        const auto& fn=found->second;
        if (fn.parameters.size()!=e.children.size())
          throw std::runtime_error("function arity mismatch: "+e.name);
        if (std::find(active.begin(),active.end(),e.name)!=active.end())
          throw std::runtime_error("recursive calls not supported: "+e.name);
        if (call_depth>=kMaxCallDepth)
          throw std::runtime_error("function call depth exceeded");
        std::unordered_map<std::string,int> bound;
        for (std::size_t i=0; i<e.children.size(); ++i)
          bound.emplace(fn.parameters[i],lower(*e.children[i],env,active,call_depth));
        active.push_back(e.name);
        const int result=lower(*fn.body,bound,active,call_depth+1);
        active.pop_back();
        return result;
      }
    }
    throw std::runtime_error("unknown expression tag");
  }
};
}
Program parse_source(std::string_view source) {
  if (source.size() > 1024 * 1024) throw std::runtime_error("source exceeds 1 MiB bootstrap limit");
  validate_source_utf8(source);
  return Parser(source).parse();
}
Program parse_runtime_source(std::string_view source) {
  if (source.size() > 1024 * 1024)
    throw std::runtime_error("source exceeds 1 MiB bootstrap limit");
  validate_source_utf8(source);
  return Parser(source,true).parse();
}
}
