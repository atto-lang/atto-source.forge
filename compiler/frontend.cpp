#include "compiler/source_utf8.hpp"
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
    const auto previousToken = tok_.text;
    while (at_<source_.size()) {
      const auto c=static_cast<unsigned char>(source_[at_]);
      if (ascii_space(c)) { ++at_; continue; }
      if (source_[at_]=='/' && at_+1<source_.size() && source_[at_+1]=='/') {
        at_+=2;
        while (at_<source_.size() && source_[at_]!='\n' && source_[at_]!='\r') ++at_;
        continue;
      }
      // Match Java/.NET's bounded nested block-comment treatment: enum
      // words inside comments are not source declarations or method slots.
      if (source_[at_]=='/' && at_+1<source_.size() && source_[at_+1]=='*') {
        const auto start=at_;
        at_+=2;
        std::size_t depth=1;
        while (depth) {
          if (at_==source_.size())
            throw std::runtime_error("unterminated block comment at byte "+
                                     std::to_string(start));
          if (at_+1<source_.size() && source_[at_]=='/' && source_[at_+1]=='*') {
            if (++depth>128)
              throw std::runtime_error("block comment nesting too deep at byte "+
                                       std::to_string(start));
            at_+=2;
          } else if (at_+1<source_.size() && source_[at_]=='*' && source_[at_+1]=='/') {
            --depth;
            at_+=2;
          } else ++at_;
        }
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
      if (tok_.text=="any" || tok_.text=="unknown" ||
          tok_.text=="Any" || tok_.text=="Unknown")
        error("any/unknown requires tagged opaque-value ABI and runtime type descriptors");
      // The bootstrap parser cannot resolve any Java/Graal import forms.
      // Reject with a stable diagnostic rather than emitting an unrelated
      // scalar result from a source that depends on external modules.
      if (tok_.text=="import") error("source imports require verified module graph lowering");
      // Scalar bootstrap lacks nominal enum declarations and compiler-owned
      // prebound virtual method slots. Never attempt arity-only fallback.
      if (tok_.text=="enum") error("nominal enum overloads require compiler-bound dispatch");
      // Fail closed on named language features before the scalar grammar
      // gives a misleading generic syntax error (or ever learns to skip them).
      // These require typed method dispatch and ownership-checked closure IR.
      if (tok_.text=="abstract")
        error("private abstract hooks require typed native vtables and superclass-only dispatch");
      if (tok_.text=="actor" && previousToken=="virtual")
        error("VIRTUAL_ACTOR_BACKEND_UNSUPPORTED: logical key and activation fencing required");
      if (tok_.text=="class" || tok_.text=="actor")
        error("class/actor methods require typed native lowering");
      if (tok_.text=="lex" || tok_.text=="nlex")
        error("lexical captures require checked closure lowering");
      if (tok_.text=="cpu" || tok_.text=="gpu" || tok_.text=="quantum" || tok_.text=="nb")
        error("execution region requires verified CPU/GPU Task lowering; unsupported execution domain: typed effect graph required; no implicit CPU fallback");
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
      // Forge is a checked i64 bootstrap: do not lex 1.25 or 1e10 as
      // integer tokens followed by unrelated punctuation/identifiers.
      bool unsupported_real = false;
      if (at_+1<source_.size() && source_[at_]=='.' &&
          ascii_digit(static_cast<unsigned char>(source_[at_+1])))
        unsupported_real=true;
      if (at_+1<source_.size() &&
          (source_[at_]=='e' || source_[at_]=='E') &&
          (ascii_digit(static_cast<unsigned char>(source_[at_+1])) ||
           ((source_[at_+1]=='+' || source_[at_+1]=='-') &&
            at_+2<source_.size() &&
            ascii_digit(static_cast<unsigned char>(source_[at_+2])))))
        unsupported_real=true;
      if (at_<source_.size() && source_[at_]=='d' &&
          (at_+1==source_.size() ||
           (!ascii_alnum(static_cast<unsigned char>(source_[at_+1])) &&
            source_[at_+1]!='_')))
        unsupported_real=true;
      if (unsupported_real)
        throw std::runtime_error("floating-point/decimal literal requires typed "
                                 "numeric lowering at byte "+std::to_string(start));
      tok_={Kind::Number,source_.substr(start,at_-start),start}; return;
    }
    // Recognize the contiguous operator, but defer execution until named
    // callable and tagged value lowering are actually implemented.
    if (source_[at_]=='|' && at_+1<source_.size() && source_[at_+1]=='>') {
      at_+=2;
      tok_={Kind::Punct,source_.substr(start,2),start}; return;
    }
    // Plain |args| closures are unsupported just like lex/nlex closures.
    if (c=='|')
      throw std::runtime_error("lexical captures require checked closure lowering at byte "+
                               std::to_string(start));
    if (source_[at_]=='-' && at_+1<source_.size() && source_[at_+1]=='>') {
      at_+=2; tok_={Kind::Punct,source_.substr(start,2),start}; return;
    }
    if (source_.substr(at_,3)=="...")
      error("tuple-v2 explicit spread requires single-evaluation checked lowering");
    // Canonical AttoLang exposes named Integer.rem/mod/pow/bitXor APIs,
    // never the legacy percent or caret operators. Report the actual byte
    // offset before generic punctuation handling, including unreachable
    // helper definitions: these characters cannot become silent aliases.
    if (c=='%')
      throw std::runtime_error("legacy % operator is removed; use Integer.rem/mod at byte "+
                               std::to_string(start));
    if (c=='^')
      throw std::runtime_error("legacy ^/^^ operators are removed; use Integer.pow/bitXor at byte "+
                               std::to_string(start));
    if (std::string_view("()+-*/{};:,?").find(static_cast<char>(c)) ==
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
    if (tok_.text=="guard")
      error("GUARD_BACKEND_UNSUPPORTED: guard is reserved until checked CFG and ownership lowering");
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
    if (tok_.text=="state") {
      next();
      if (tok_.text=="container")
        error("STATE_CONTAINER_BACKEND_UNSUPPORTED: protected heap region and critical guard lowering required");
      error("unexpected declaration after state");
    }
    if (tok_.text=="virtual") {
      next();
      if (tok_.text=="actor")
        error("VIRTUAL_ACTOR_BACKEND_UNSUPPORTED: logical key and activation fencing required");
      error("unexpected declaration after virtual");
    }
    expect("fnc");
    const std::string name=identifier();
    if (name=="int" || name=="fnc" || name=="return")
      error("reserved function name");
    expect("(");
    Function fn;
    if (tok_.text!=")") {
      do {
        if (tok_.text=="tuple" || tok_.text=="structural" || tok_.text=="(")
          error("tuple-v2 parameter requires typed ownership/ABI lowering");
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
    if (tok_.text=="tuple" || tok_.text=="structural" || tok_.text=="(")
      error("tuple-v2 return requires typed ABI lowering");
    expect("int"); expect("{");
    if (tok_.text=="guard")
      error("GUARD_BACKEND_UNSUPPORTED: early-exit CFG and ownership lowering required");
    expect("return");
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
    if (tok_.text=="?")
      error("ternary expression requires bool typing and CFG branch-merge lowering");
    if (tok_.text=="|>")
      error("value pipeline requires named callable lowering; unsupported in Forge i64 bootstrap");
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
    if (tok_.text=="rt") {
      next();
      const std::string operation(tok_.text);
      if (operation=="ref" || operation=="unref" || operation=="deref" || operation=="raw")
        error("pointer-style runtime operation is not valid Oreslang source; use explicit ownership operations");
      if (operation=="borrow") {
        next();
        if (tok_.text=="mut")
          error("legacy 'rt borrow mut' is not valid Oreslang; use 'rt mut borrow value'");
        error("runtime ownership operation requires typed lowering in Forge");
      }
      if (operation=="mut") {
        next();
        if (tok_.text=="share")
          error("native rt mut share requires verified SharedMutex<T> capability, guarded lowering and allocation-domain proof; refusing unsynchronized alias");
        if (tok_.text!="borrow")
          error("'rt mut' only supports borrowing; use 'rt mut borrow value'");
        error("runtime ownership operation requires typed lowering in Forge");
      }
      error("runtime ownership operation requires typed lowering in Forge");
    }
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
      // The Forge scalar interpreter/JIT has no ETS table authority or pg
      // ActorGroup/mailbox integration. Reject explicit namespace access
      // instead of accepting it as an unbound scalar variable.
      if ((name=="ets" || name=="pg") && tok_.text==".")
        error("Forge ETS/pg guest APIs require authenticated isolate, table and mailbox lowering");
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
  oresforge::source::validate_utf8(source);
  return Parser(source).parse();
}
Program parse_runtime_source(std::string_view source) {
  if (source.size() > 1024 * 1024)
    throw std::runtime_error("source exceeds 1 MiB bootstrap limit");
  oresforge::source::validate_utf8(source);
  return Parser(source,true).parse();
}
}

