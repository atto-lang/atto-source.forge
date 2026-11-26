#include "compiler/source_utf8.hpp"
#include "compiler/call_native.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace oresforge::calls {
namespace {
constexpr std::size_t kMaxSource = 1024u * 1024u;
constexpr std::size_t kMaxFunctions = 64;
constexpr std::size_t kMaxNodes = 4096;
constexpr std::size_t kMaxNodesPerFunction = 256;
constexpr std::size_t kMaxDepth = 128;
constexpr std::size_t kMaxStackDepth = 128;
constexpr std::size_t kMaxEvalSteps = 200000;
bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool space(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\v' || c == '\f'; }
struct Token { std::string_view text; std::size_t offset=0; };

// Predicate types are statically derived from verified opcode identity.
// The native ABI remains i64-only at function boundaries.
bool boolean_kind(Kind k) {
  return k==Kind::Boolean || k==Kind::Not || k==Kind::And || k==Kind::Or || k==Kind::BooleanXor ||
         k==Kind::Less || k==Kind::LessEqual || k==Kind::Greater ||
         k==Kind::GreaterEqual || k==Kind::Equal || k==Kind::NotEqual;
}
bool reserved_predicate_word(std::string_view s) {
  return s=="true" || s=="false" || s=="ifnot" || s=="unless" ||
         s=="eq" || s=="neq" || s=="fi" || s=="guard" || s=="end";
}

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) { next(); }
  Module parse() {
    while (!token_.text.empty()) definition();
    for (std::size_t i=0; i<module_.functions.size(); ++i) {
      if (module_.functions[i].name=="main") module_.entry=static_cast<int>(i);
    }
    if (module_.entry<0) fail("missing main function");
    // Resolve every function, including unused ones. No partially linked IR.
    for (auto& fn:module_.functions) {
      for (auto& node:fn.nodes) {
        if (node.kind != Kind::Call) continue;
        const auto found=names_.find(node.name);
        if (found==names_.end()) fail("unknown function");
        node.callee=found->second;
        if (node.children.size()!=module_.functions.at(static_cast<std::size_t>(node.callee)).parameters.size())
          fail("function arity mismatch");
      }
    }
    verify(module_);
    return std::move(module_);
  }
 private:
  std::string_view text_;
  std::size_t at_=0;
  Token token_;
  Module module_;
  std::map<std::string,int> names_;
  std::size_t node_count_=0;
  Function* current_=nullptr;

  [[noreturn]] void fail(const char* message) const {
    throw std::runtime_error(std::string(message)+" at byte "+std::to_string(token_.offset));
  }
  void next() {
    while (at_<text_.size()) {
      const char c=text_[at_];
      if (space(c)) {++at_; continue;}
      if (c=='/' && at_+1<text_.size() && text_[at_+1]=='/') {
        at_+=2;
        while (at_<text_.size() && text_[at_]!='\n') ++at_;
        continue;
      }
      break;
    }
    const std::size_t begin=at_;
    if (at_==text_.size()) {token_={{},begin}; return;}
    const char c=text_[at_++];
    if (c=='@')
      throw std::runtime_error("annotations require typed target validation and AOT lowering; unsupported in Forge native-call compiler at byte "+
                               std::to_string(begin));
    if (letter(c)) {
      while (at_<text_.size() && (letter(text_[at_])||digit(text_[at_]))) ++at_;
    } else if (digit(c)) {
      while (at_<text_.size() && digit(text_[at_])) ++at_;
    } else if (c=='!' && text_.substr(begin,3)=="!eq" &&
               (begin+3==text_.size() || (!letter(text_[begin+3]) && !digit(text_[begin+3])))) {
      at_=begin+3;
    } else if ((c=='<'||c=='>'||c=='='||c=='!') &&
               at_<text_.size() && text_[at_]=='=') {
      ++at_;
    } else if ((c=='&'||c=='|') && at_<text_.size() && text_[at_]==c) {
      ++at_;
    } else if (c=='-' && at_<text_.size() && text_[at_]=='>') {
      ++at_;
    } else if (c=='^' && at_<text_.size() && text_[at_]=='^') {
      ++at_;
    } else if (std::string_view("()+-*{};:,<>=^?").find(c)==std::string_view::npos) {
      at_=begin;
      fail("invalid character");
    }
    token_={text_.substr(begin,at_-begin),begin};
    // The native-call profile has no trusted module graph, typed symbol table,
    // or verified compiler-owned std module bindings. Reject imports at the
    // token boundary, including inside unreachable declarations, instead of
    // misreporting them as malformed scalar function definitions.
    if (token_.text=="import")
      fail("source imports require verified module graph lowering");
  }
  bool take(std::string_view s) {
    if (token_.text!=s) return false;
    next(); return true;
  }
  void expect(std::string_view s) {if(!take(s))fail("unexpected token");}
  std::string identifier() {
    if (token_.text.empty() || !letter(token_.text[0])) fail("expected identifier");
    if (reserved_predicate_word(token_.text)) fail("reserved predicate word");
    const std::string result(token_.text);
    next();
    return result;
  }
  int add(Kind kind,std::int64_t value=0,std::string name={},std::vector<int> children={}) {
    if (++node_count_>kMaxNodes || current_->nodes.size()>=kMaxNodesPerFunction)
      fail("callable AST node budget exceeded");
    const int idx=static_cast<int>(current_->nodes.size());
    current_->nodes.push_back({kind,value,std::move(name),std::move(children),-1});
    return idx;
  }
  bool is_boolean(int id) const {
    return boolean_kind(current_->nodes.at(static_cast<std::size_t>(id)).kind);
  }
  void require_integer(int id) const {
    if (is_boolean(id)) fail("Boolean is not an int in the Forge callable subset");
  }
  void require_boolean(int id) const {
    if (!is_boolean(id)) fail("condition_not_bool: conditional requires bool");
  }
  void definition() {
    bool defining=false,public_=false;
    while (token_.text=="define" || token_.text=="pub") {
      if (take("define")) {if(defining)fail("duplicate define");defining=true;}
      else if (take("pub")) {if(public_)fail("duplicate pub");public_=true;}
    }
    // This independent native-call frontend must reject the same HTTP actor
    // forms as the scalar Forge parser, before any callable IR is produced.
    if (token_.text=="http")
      fail("HTTP_ACTOR_BACKEND_UNSUPPORTED: Forge native-call source has no HTTP actor scheduling or physical shutdown lowering");
    if (take("serial")) {
      if (token_.text=="http")
        fail("HTTP_ACTOR_BACKEND_UNSUPPORTED: Forge native-call source has no HTTP actor scheduling or physical shutdown lowering");
      fail("SERIAL_ACTOR_BACKEND_UNSUPPORTED: Forge native-call compiler has no source serial actor lowering");
    }
    expect("fnc");
    const std::string name=identifier();
    if (name=="int"||name=="return"||name=="if"||name=="else"||name=="then")
      fail("reserved function name");
    if (names_.contains(name))fail("duplicate function");
    if (module_.functions.size()>=kMaxFunctions)fail("function count budget exceeded");
    const int index=static_cast<int>(module_.functions.size());
    names_[name]=index;
    module_.functions.push_back({name,{},{}});
    current_=&module_.functions.back();
    expect("(");
    if (token_.text!=")") {
      do {
        const bool type_first=take("int");
        const std::string param=identifier();
        if (param=="int"||param=="if"||param=="then"||param=="else"||
            param=="return"||param=="fnc"||
            std::find(current_->parameters.begin(),current_->parameters.end(),param)
            !=current_->parameters.end())fail("invalid or duplicate parameter");
        if(!type_first) { expect(":");expect("int"); }
        current_->parameters.push_back(param);
        if(current_->parameters.size()>4)fail("function parameter budget exceeded");
        if (!take(","))break;
      } while(true);
    }
    expect(")");
    if(!take(":"))expect("->");
    expect("int");expect("{");
    current_->result=terminal_body(0);
    expect("}");
  }
  // This scalar subset has no local state, effects, or general statements.
  // Every admitted guard failure body is exactly one explicit typed return.
  // Reuse the verified lazy If tree: true continues, false returns early.
  int terminal_body(std::size_t d) {
    depth(d);
    if (!take("guard")) return terminal_return(d+1);
    int condition=logical_or(d+1);
    require_boolean(condition);
    while (take(",")) {
      const int next_check=logical_or(d+1);
      require_boolean(next_check);
      condition=add(Kind::And,0,{}, {condition,next_check});
    }
    const bool braced=take("{");
    if(!braced) expect("else");
    if(token_.text!="return" && token_.text!="^")
      fail("guard_failure_requires_return: scalar guard failure must return from callable");
    const int failure=terminal_return(d+1);
    expect(braced?"}":"end");
    const int continuation=terminal_body(d+1);
    return add(Kind::If,0,{}, {condition,continuation,failure});
  }
  int terminal_return(std::size_t d) {
    depth(d);
    if (!take("^")) expect("return");
    const int value=expr(d+1);
    require_integer(value);
    if (token_.text=="^") fail("single caret is only a return statement; integer XOR uses ^^");
    expect(";");
    return value;
  }
  void depth(std::size_t n) {if(n>kMaxDepth)fail("callable expression nesting too deep");}
  int expr(std::size_t d) {
    depth(d);
    if (token_.text=="if" || token_.text=="ifnot" || token_.text=="unless") {
      const bool negated=token_.text!="if";
      next();
      int condition=logical_or(d+1);
      require_boolean(condition);
      if (negated) condition=add(Kind::Not,0,{}, {condition});
      expect("then");
      const int yes=expr(d+1);
      expect("else");
      const int no=expr(d+1);
      // Legacy if expressions retain the unclosed stage-0 spelling.
      // Negated value forms require their explicit canonical terminator.
      if (negated) expect("fi");
      require_integer(yes);
      require_integer(no);
      return add(Kind::If,0,{}, {condition,yes,no});
    }
    const int condition=logical_or(d+1);
    if (take("?")) {
      require_boolean(condition);
      const int yes=ternary_arm(d+1);
      expect(":");
      const int no=ternary_arm(d+1);
      require_integer(yes);
      require_integer(no);
      return add(Kind::Ternary,0,{}, {condition,yes,no});
    }
    return condition;
  }
  // Direct braces commit to a single explicit result, not an aggregate
  // or callable return. Keep the verified lazy Ternary tree and native ABI.
  int ternary_arm(std::size_t d) {
    depth(d);
    if (!take("{")) return expr(d+1);
    if (!take("return") && !take("^"))
      fail("E_VALUE_ARM: ternary block requires explicit return or ^; lexical prefixes are not lowered");
    const int value=expr(d+1);
    take(";");
    if (!take("}"))
      fail("E_VALUE_ARM: single-result ternary block requires closing '}'");
    return value;
  }
  int logical_or(std::size_t d) {
    depth(d);
    int left=logical_and(d+1);
    while (take("||")) {
      require_boolean(left);
      const int right=logical_and(d+1);
      require_boolean(right);
      left=add(Kind::Or,0,{}, {left,right});
    }
    return left;
  }
  int logical_and(std::size_t d) {
    depth(d);
    int left=integer_xor(d+1);
    while (take("&&")) {
      require_boolean(left);
      const int right=integer_xor(d+1);
      require_boolean(right);
      left=add(Kind::And,0,{}, {left,right});
    }
    return left;
  }
  // Java's equality/comparison expressions bind more tightly than integer XOR.
  // No implicit conversion of predicate results to signed i64.
  int integer_xor(std::size_t d) {
    depth(d);
    int left=comparison(d+1);
    while (take("^^")) {
      const int right=comparison(d+1);
      require_integer(left); require_integer(right);
      left=add(Kind::IntegerXor,0,{}, {left,right});
    }
    return left;
  }
  int comparison(std::size_t d) {
    depth(d);
    int left=sum(d+1);
    if (token_.text=="<"||token_.text=="<="||token_.text==">"||
        token_.text==">="||token_.text=="=="||token_.text=="!="||
        token_.text=="eq"||token_.text=="neq"||token_.text=="!eq") {
      const std::string op(token_.text);
      next();
      const int right=sum(d+1);
      const bool equality=op=="eq"||op=="neq"||op=="!eq"||op=="=="||op=="!=";
      if (equality) {
        if (is_boolean(left)!=is_boolean(right)) fail("comparison operand type mismatch");
      } else {
        require_integer(left);
        require_integer(right);
      }
      Kind k=Kind::Equal;
      if(op=="<") k=Kind::Less;
      else if(op=="<=") k=Kind::LessEqual;
      else if(op==">") k=Kind::Greater;
      else if(op==">=") k=Kind::GreaterEqual;
      else if(op=="!="||op=="neq"||op=="!eq") k=Kind::NotEqual;
      left=add(k,0,{}, {left,right});
    }
    return left;
  }
  int sum(std::size_t d) {
    depth(d);
    int left=term(d+1);
    while (token_.text=="+"||token_.text=="-") {
      const bool plus=take("+");
      if (!plus)expect("-");
      const int right=term(d+1);
      require_integer(left); require_integer(right);
      left=add(plus?Kind::Add:Kind::Sub,0,{}, {left,right});
    }
    return left;
  }
  int term(std::size_t d) {
    depth(d);
    int left=factor(d+1);
    while (token_.text=="*") {
      next();
      const int right=factor(d+1);
      require_integer(left); require_integer(right);
      left=add(Kind::Mul,0,{}, {left,right});
    }
    return left;
  }
  int factor(std::size_t d) {
    depth(d);
    if(take("+")) { const int value=factor(d+1); require_integer(value); return value; }
    if(take("-")) {
      if(token_.text=="9223372036854775808"){
        next();return add(Kind::Constant,std::numeric_limits<std::int64_t>::min());
      }
      const int zero=add(Kind::Constant,0);
      const int other=factor(d+1);
      require_integer(other);
      return add(Kind::Sub,0,{}, {zero,other});
    }
    if(take("(")) {
      const int x=expr(d+1);
      expect(")");
      return x;
    }
    if (token_.text=="if" || token_.text=="ifnot" || token_.text=="unless") return expr(d+1);
    if (take("true")) return add(Kind::Boolean,1);
    if (take("false")) return add(Kind::Boolean,0);
    if (!token_.text.empty() && digit(token_.text.front())) {
      std::int64_t value=0;
      auto bytes=token_.text;
      auto parsed=std::from_chars(bytes.data(),bytes.data()+bytes.size(),value);
      if(parsed.ec!=std::errc{}||parsed.ptr!=bytes.data()+bytes.size())
        fail("invalid i64 literal");
      next();return add(Kind::Constant,value);
    }
    if (!token_.text.empty() && letter(token_.text.front())) {
      const std::string name=identifier();
      if (!take("(")) {
        auto& params=current_->parameters;
        auto found=std::find(params.begin(),params.end(),name);
        if(found==params.end())fail("unknown parameter");
        return add(Kind::Parameter,static_cast<std::int64_t>(found-params.begin()));
      }
      std::vector<int> args;
      if (token_.text!=")") {
        do {
          const int arg=expr(d+1); args.push_back(arg);
          if(args.size()>4)fail("call argument budget exceeded");
          if(!take(","))break;
        }while(true);
      }
      expect(")");
      // Xor is the named Boolean operation, never an infix ^^ alias.
      // Do not hijack ordinary i64 callables. The verifier independently
      // rejects intrinsic binding if a source function/formal shadows Xor.
      if(name=="Xor" && std::any_of(args.begin(),args.end(),
                                    [&](int arg){return is_boolean(arg);})) {
        if(args.size()!=2) fail("Boolean Xor requires two bool operands");
        if(!is_boolean(args[0])||!is_boolean(args[1]))
          fail("Boolean Xor requires two bool operands");
        return add(Kind::BooleanXor,0,{},std::move(args));
      }
      for(int arg:args) require_integer(arg);
      return add(Kind::Call,0,name,std::move(args));
    }
    fail("expected callable expression");
  }
};

std::int64_t checked(Kind k,std::int64_t a,std::int64_t b) {
  std::int64_t out=0;
  switch(k) {
    case Kind::Add:
      if(__builtin_add_overflow(a,b,&out))throw std::runtime_error("i64 add overflow");
      return out;
    case Kind::Sub:
      if(__builtin_sub_overflow(a,b,&out))throw std::runtime_error("i64 sub overflow");
      return out;
    case Kind::Mul:
      if(__builtin_mul_overflow(a,b,&out))throw std::runtime_error("i64 mul overflow");
      return out;
    case Kind::IntegerXor: return a^b;
    case Kind::Less: return a<b;
    case Kind::LessEqual: return a<=b;
    case Kind::Greater: return a>b;
    case Kind::GreaterEqual: return a>=b;
    case Kind::Equal: return a==b;
    case Kind::NotEqual: case Kind::BooleanXor: return a!=b;
    default: throw std::runtime_error("invalid binary callable opcode");
  }
}
struct Evaluator {
  const Module& m;
  std::size_t remaining=kMaxEvalSteps;
  std::int64_t evaluate(std::size_t id,std::span<const std::int64_t> args,std::size_t depth) {
    if (depth>=kMaxStackDepth)throw std::runtime_error("call depth exceeded");
    const auto& f=m.functions.at(id);
    if(args.size()!=f.parameters.size())throw std::runtime_error("runtime argument count mismatch");
    return visit(f,f.result,args,depth);
  }
  std::int64_t visit(const Function& fn,int id,std::span<const std::int64_t> args,
                     std::size_t depth) {
    if (remaining--==0)throw std::runtime_error("callable execution budget exceeded");
    const auto& n=fn.nodes.at(static_cast<std::size_t>(id));
    if(n.kind==Kind::Constant||n.kind==Kind::Boolean)return n.value;
    if(n.kind==Kind::Not)return !visit(fn,n.children[0],args,depth);
    if(n.kind==Kind::And) {
      if(!visit(fn,n.children[0],args,depth)) return 0;
      return visit(fn,n.children[1],args,depth);
    }
    if(n.kind==Kind::Or) {
      if(visit(fn,n.children[0],args,depth)) return 1;
      return visit(fn,n.children[1],args,depth);
    }
    if(n.kind==Kind::Parameter)return args[static_cast<std::size_t>(n.value)];
    if(n.kind==Kind::If||n.kind==Kind::Ternary) {
      const auto pred=visit(fn,n.children[0],args,depth);
      return visit(fn,n.children[pred ? 1u:2u],args,depth);
    }
    if(n.kind==Kind::Call) {
      std::vector<std::int64_t> vals;
      for (int arg:n.children) vals.push_back(visit(fn,arg,args,depth));
      return evaluate(static_cast<std::size_t>(n.callee),vals,depth+1);
    }
    const auto a=visit(fn,n.children.at(0),args,depth);
    const auto b=visit(fn,n.children.at(1),args,depth);
    return checked(n.kind,a,b);
  }
};

const char* argument_reg(std::size_t i) {
  constexpr const char* regs[]={"%rdi","%rsi","%rdx","%rcx"};
  return regs[i];
}
struct Native {
  const Module& program;
  explicit Native(const Module& module) : program(module) {}
  std::ostringstream out;
  std::size_t label=0;
  std::string value_slot(int id) const {
    return "-"+std::to_string(8u*(static_cast<std::size_t>(id)+6u))+"(%rbp)";
  }
  std::string parameter_slot(int id) const {
    return "-"+std::to_string(8u*(static_cast<std::size_t>(id)+1u))+"(%rbp)";
  }
  void emit_node(const Function& fn,int index) {
    const auto& n=fn.nodes.at(static_cast<std::size_t>(index));
    if(n.kind==Kind::Constant||n.kind==Kind::Boolean) {
      out<<"  movabsq $"<<n.value<<", %rax\n";
    } else if(n.kind==Kind::Parameter) {
      out<<"  movq "<<parameter_slot(static_cast<int>(n.value))<<", %rax\n";
    } else if(n.kind==Kind::Not) {
      emit_node(fn,n.children[0]);
      out<<"  xorq $1, %rax\n";
    } else if(n.kind==Kind::And||n.kind==Kind::Or) {
      const auto tag=label++;
      emit_node(fn,n.children[0]);
      out<<"  cmpq $0, %rax\n  "<<(n.kind==Kind::And?"je":"jne")
         <<" .Lores_logical_end_"<<tag<<"\n";
      emit_node(fn,n.children[1]);
      out<<".Lores_logical_end_"<<tag<<":\n";
    } else if(n.kind==Kind::If||n.kind==Kind::Ternary) {
      const auto tag=label++;
      emit_node(fn,n.children[0]);
      out<<"  cmpq $0, %rax\n  je .Lores_else_"<<tag<<"\n";
      emit_node(fn,n.children[1]);
      out<<"  jmp .Lores_end_"<<tag<<"\n"
         <<".Lores_else_"<<tag<<":\n";
      emit_node(fn,n.children[2]);
      out<<".Lores_end_"<<tag<<":\n";
    } else if(n.kind==Kind::Call) {
      for (int child:n.children)emit_node(fn,child);
      for (std::size_t i=0;i<n.children.size();++i)
        out<<"  movq "<<value_slot(n.children[i])<<", "<<argument_reg(i)<<"\n";
      out<<"  movq -40(%rbp), %r8\n"
         <<"  addq $1, %r8\n"
         <<"  call .Lores_fn_"<<n.callee<<"\n";
    } else {
      emit_node(fn,n.children[0]);
      emit_node(fn,n.children[1]);
      out<<"  movq "<<value_slot(n.children[0])<<", %rax\n";
      switch(n.kind) {
        case Kind::Add: out<<"  addq ";break;
        case Kind::Sub: out<<"  subq ";break;
        case Kind::Mul: out<<"  imulq ";break;
        case Kind::IntegerXor: out<<"  xorq ";break;
        default: out<<"  cmpq ";break;
      }
      out<<value_slot(n.children[1])<<", %rax\n";
      if(n.kind==Kind::Add||n.kind==Kind::Sub||n.kind==Kind::Mul) {
        out<<"  jo .Lores_overflow\n";
      } else if(n.kind!=Kind::IntegerXor) {
        const char* cc="e";
        switch(n.kind) {
          case Kind::Less: cc="l";break;
          case Kind::LessEqual: cc="le";break;
          case Kind::Greater: cc="g";break;
          case Kind::GreaterEqual: cc="ge";break;
          case Kind::Equal: cc="e";break;
          case Kind::NotEqual: case Kind::BooleanXor: cc="ne";break;
          default: throw std::runtime_error("invalid native comparison");
        }
        out<<"  set"<<cc<<" %al\n  movzbq %al, %rax\n";
      }
    }
    out<<"  movq %rax, "<<value_slot(index)<<"\n";
  }
  std::string build() {
    out<<".text\n.globl oresforge_entry\n.type oresforge_entry, @function\n"
       <<"oresforge_entry:\n  xorl %r8d, %r8d\n"
       <<"  jmp .Lores_fn_"<<program.entry<<"\n";
    for (std::size_t i=0;i<program.functions.size();++i) {
      const auto& fn=program.functions[i];
      const std::size_t slots=5u+fn.nodes.size();
      const std::size_t bytes=((slots*8u+15u)/16u)*16u;
      out<<".Lores_fn_"<<i<<":\n"
         <<"  cmpq $"<<kMaxStackDepth<<", %r8\n"
         <<"  jae .Lores_depth\n"
         <<"  pushq %rbp\n  movq %rsp, %rbp\n"
         <<"  subq $"<<bytes<<", %rsp\n";
      for (std::size_t arg=0;arg<4;++arg)
        out<<"  movq "<<argument_reg(arg)<<", "<<parameter_slot(static_cast<int>(arg))<<"\n";
      out<<"  movq %r8, -40(%rbp)\n";
      emit_node(fn,fn.result);
      out<<"  leave\n  ret\n";
    }
    out<<".Lores_depth:\n  ud2\n"
       <<".Lores_overflow:\n  ud2\n"
       <<".size oresforge_entry, .-oresforge_entry\n"
       <<".section .note.GNU-stack,\"\",@progbits\n";
    return out.str();
  }
};
}
Module parse(std::string_view source) {
  if (source.size()>kMaxSource)throw std::runtime_error("callable source budget exceeded");
  oresforge::source::validate_utf8(source);
  return Parser(source).parse();
}
void verify(const Module& m) {
  if(m.entry<0 || static_cast<std::size_t>(m.entry)>=m.functions.size())
    throw std::runtime_error("invalid callable main");
  if (m.functions.size()>kMaxFunctions)throw std::runtime_error("function count budget exceeded");
  std::map<std::string,std::size_t> names;
  std::size_t total=0;
  for (std::size_t fi=0;fi<m.functions.size();++fi) {
    const auto& fn=m.functions[fi];
    if(fn.name.empty() || !names.emplace(fn.name,fi).second)
      throw std::runtime_error("duplicate or invalid function");
    if(fn.parameters.size()>4)throw std::runtime_error("function parameter budget exceeded");
    if(fn.nodes.empty() || fn.nodes.size()>kMaxNodesPerFunction)
      throw std::runtime_error("invalid callable function body");
    total+=fn.nodes.size();
    if(total>kMaxNodes)throw std::runtime_error("callable AST node budget exceeded");
    if(fn.result<0||static_cast<std::size_t>(fn.result)!=fn.nodes.size()-1)
      throw std::runtime_error("invalid callable return/root");
    if (boolean_kind(fn.nodes[static_cast<std::size_t>(fn.result)].kind))
      throw std::runtime_error("callable int return cannot be Boolean");
    // This stage-0 AST must be an owned tree, not an arbitrary dataflow DAG.
    // Otherwise a node on an untaken if-branch could be reused without ever
    // initializing its stack slot. Enforce one reachable parent per child.
    std::vector<unsigned> incoming(fn.nodes.size(),0);
    for(std::size_t i=0;i<fn.nodes.size();++i) {
      const auto& n=fn.nodes[i];
      switch(n.kind) {
        case Kind::Constant: case Kind::Parameter: case Kind::Add:
        case Kind::Sub: case Kind::Mul: case Kind::Less: case Kind::LessEqual:
        case Kind::Greater: case Kind::GreaterEqual: case Kind::Equal:
        case Kind::NotEqual: case Kind::If: case Kind::Call:
        case Kind::Boolean: case Kind::Not: case Kind::Ternary:
         case Kind::And: case Kind::Or: case Kind::BooleanXor: case Kind::IntegerXor: break;
        default: throw std::runtime_error("unknown callable opcode");
      }
      if(n.kind!=Kind::Call && !n.name.empty())
        throw std::runtime_error("unexpected callable symbol payload");
      for(int arg:n.children) {
        if(arg<0 || static_cast<std::size_t>(arg)>=i)
          throw std::runtime_error("callable use before definition");
        if(++incoming[static_cast<std::size_t>(arg)]!=1)
          throw std::runtime_error("callable AST shared node");
      }
      std::size_t size=2;
      if(n.kind==Kind::Constant||n.kind==Kind::Boolean||n.kind==Kind::Parameter)size=0;
      else if(n.kind==Kind::Not)size=1;
      else if(n.kind==Kind::If||n.kind==Kind::Ternary)size=3;
      else if(n.kind==Kind::Call)size=n.children.size();
      if(n.children.size()!=size)throw std::runtime_error("callable operand arity mismatch");
      if(n.kind==Kind::Boolean && n.value!=0 && n.value!=1)
        throw std::runtime_error("invalid Boolean literal");
      const auto child_bool=[&](std::size_t pos) {
        return boolean_kind(fn.nodes.at(static_cast<std::size_t>(n.children.at(pos))).kind);
      };
      if(n.kind==Kind::Not && !child_bool(0))
        throw std::runtime_error("condition_not_bool: negation requires bool");
      if(n.kind==Kind::If||n.kind==Kind::Ternary) {
        if(!child_bool(0)) throw std::runtime_error("condition_not_bool: conditional requires bool");
        if(child_bool(1)||child_bool(2)) throw std::runtime_error("conditional branch is not int");
      } else if(n.kind==Kind::And||n.kind==Kind::Or) {
        if(!child_bool(0)||!child_bool(1))
          throw std::runtime_error("condition_not_bool: logical operands require bool");
      } else if(n.kind==Kind::BooleanXor) {
        if(!child_bool(0)||!child_bool(1))
          throw std::runtime_error("Boolean Xor requires two bool operands");
        if(std::any_of(m.functions.begin(),m.functions.end(),
                       [](const Function& f){return f.name=="Xor";}) ||
           std::find(fn.parameters.begin(),fn.parameters.end(),"Xor")!=fn.parameters.end())
          throw std::runtime_error("Boolean Xor intrinsic is shadowed by a source declaration");
      } else if(n.kind==Kind::Equal||n.kind==Kind::NotEqual) {
        if(child_bool(0)!=child_bool(1)) throw std::runtime_error("comparison operand type mismatch");
      } else if(n.kind==Kind::IntegerXor||n.kind==Kind::Add||n.kind==Kind::Sub||n.kind==Kind::Mul||
                n.kind==Kind::Less||n.kind==Kind::LessEqual||n.kind==Kind::Greater||
                n.kind==Kind::GreaterEqual) {
        if(child_bool(0)||child_bool(1)) throw std::runtime_error("Boolean is not an int");
      } else if(n.kind==Kind::Call) {
        for(std::size_t j=0;j<n.children.size();++j)
          if(child_bool(j)) throw std::runtime_error("Boolean is not an int argument");
      }
      if(n.kind==Kind::Parameter && (n.value<0 ||
         static_cast<std::uint64_t>(n.value)>=fn.parameters.size()))
        throw std::runtime_error("invalid callable parameter");
      if(n.kind==Kind::Call) {
        if(n.callee<0 || static_cast<std::size_t>(n.callee)>=m.functions.size())
          throw std::runtime_error("invalid callable target");
        if(n.children.size()!=m.functions[static_cast<std::size_t>(n.callee)].parameters.size())
          throw std::runtime_error("function arity mismatch");
        if(n.name!=m.functions[static_cast<std::size_t>(n.callee)].name)
          throw std::runtime_error("callable target/name mismatch");
      } else if(n.callee!=-1)throw std::runtime_error("unexpected callable target");
    }
    for(std::size_t i=0;i<fn.nodes.size();++i) {
      const unsigned expected=(i==static_cast<std::size_t>(fn.result))?0u:1u;
      if(incoming[i]!=expected)
        throw std::runtime_error("callable AST unreachable node");
    }
  }
  if(m.functions[static_cast<std::size_t>(m.entry)].name!="main")
    throw std::runtime_error("callable entry is not main");
}
std::int64_t interpret(const Module& m,std::span<const std::int64_t> args) {
  verify(m);
  Evaluator eval{m};
  return eval.evaluate(static_cast<std::size_t>(m.entry),args,0);
}
std::string emit_asm(const Module& m,Arch arch) {
  if(arch!=Arch::X86_64)
    throw std::runtime_error("native callable backend currently supports x86_64 only");
  verify(m);
  return Native{m}.build();
}
}
