#include "compiler/source_utf8.hpp"
#include "compiler/call_native.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
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
constexpr std::size_t kMaxLocals = 32;
constexpr std::size_t kMaxCommentNesting = 128;
bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool valid_identifier(std::string_view name) {
  if(name.empty() || !letter(name.front()))return false;
  for(const char c:name) if(!letter(c) && !digit(c))return false;
  return true;
}
bool space(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\v' || c == '\f'; }
struct Token { std::string_view text; std::size_t offset=0; };

// Signature-resolved, bounded binding; labels never select overloads.
std::vector<std::size_t> bind_call_slots(const Node& call,const Function& fn) {
  if(call.children.size()!=fn.parameters.size())
    throw std::runtime_error("function arity mismatch");
  // Nonzero-arity calls must retain complete provenance. Erasing both
  // inventories cannot silently turn a named call into a positional call.
  if(call.argument_labels.size()!=call.children.size())
    throw std::runtime_error("invalid named call label inventory");
  std::vector<std::size_t> slots;
  std::vector<bool> assigned(fn.parameters.size(),false);
  std::size_t positional=0;
  bool seen_named=false;
  slots.reserve(call.children.size());
  for(std::size_t i=0;i<call.children.size();++i) {
    const std::string& label=call.argument_labels[i];
    std::size_t slot=0;
    if(label.empty()) {
      if(seen_named)
        throw std::runtime_error("positional argument after named argument");
      slot=positional++;
    } else {
      seen_named=true;
      const auto it=std::find(fn.parameters.begin(),fn.parameters.end(),label);
      if(it==fn.parameters.end())
        throw std::runtime_error("unknown named argument: "+label);
      slot=static_cast<std::size_t>(it-fn.parameters.begin());
    }
    if(slot>=assigned.size() || assigned[slot])
      throw std::runtime_error("duplicate named argument or positional collision");
    assigned[slot]=true;
    slots.push_back(slot);
  }
  return slots;
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
        node.argument_slots=bind_call_slots(
          node,module_.functions.at(static_cast<std::size_t>(node.callee)));
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
  std::set<std::string> local_names_;

  [[noreturn]] void fail(const char* message) const {
    throw std::runtime_error(std::string(message)+" at byte "+std::to_string(token_.offset));
  }
  void next() {
    while (at_<text_.size()) {
      const char c=text_[at_];
      if (space(c)) {++at_; continue;}
      if (c=='/' && at_+1<text_.size() && text_[at_+1]=='/') {
        at_+=2;
        while (at_<text_.size() && text_[at_]!='\n' && text_[at_]!='\r') ++at_;
        continue;
      }
      // The scalar frontend and Java accept bounded nested block comments.
      // Refuse unterminated or over-nested input before parser/IR publication.
      if (c=='/' && at_+1<text_.size() && text_[at_+1]=='*') {
        const auto start=at_;
        at_+=2;
        std::size_t depth=1;
        while(depth!=0) {
          if(at_==text_.size())
            throw std::runtime_error("unterminated block comment at byte "+
                                     std::to_string(start));
          if(at_+1<text_.size() && text_[at_]=='/' && text_[at_+1]=='*') {
            if(++depth>kMaxCommentNesting)
              throw std::runtime_error("block comment nesting too deep at byte "+
                                       std::to_string(start));
            at_+=2;
          } else if(at_+1<text_.size() && text_[at_]=='*' && text_[at_+1]=='/') {
            --depth;
            at_+=2;
          } else ++at_;
        }
        continue;
      }
      break;
    }
    const std::size_t begin=at_;
    if (at_==text_.size()) {token_={{},begin}; return;}
    const char c=text_[at_++];
    if (letter(c)) {
      while (at_<text_.size() && (letter(text_[at_])||digit(text_[at_]))) ++at_;
      if (text_.substr(begin,at_-begin)=="macro")
        throw std::runtime_error(
          "FORGE_MACRO_EXPANDER_REQUIRED: typed macro declaration requires checked compile-time lowering at byte "+
          std::to_string(begin));
    } else if (c=='!' && (at_==text_.size() || text_[at_]!='=')) {
      throw std::runtime_error(
        "FORGE_MACRO_EXPANDER_REQUIRED: typed macro invocation requires checked compile-time lowering at byte "+
        std::to_string(begin));
    } else if (digit(c)) {
      while (at_<text_.size() && digit(text_[at_])) ++at_;
    } else if ((c=='<'||c=='>'||c=='='||c=='!') &&
               at_<text_.size() && text_[at_]=='=') {
      ++at_;
    } else if (c=='-' && at_<text_.size() && text_[at_]=='>') {
      ++at_;
    } else if(c=='%' || c=='^') {
      // Do not parse these as a legacy numeric operator or silently discard
      // their operands. The i64 scalar and native-call entrypoints agree.
      throw std::runtime_error(
        std::string(c=='%' ? "legacy % operator is removed; use Integer.rem/mod"
                           : "legacy ^/^^ operators are removed; use Integer.pow/bitXor")+
        " at byte "+std::to_string(begin));
    } else if (std::string_view("()+-*{};:,<>=").find(c)==std::string_view::npos) {
      at_=begin;
      fail("invalid character");
    }
    token_={text_.substr(begin,at_-begin),begin};
  }
  bool take(std::string_view s) {
    if (token_.text!=s) return false;
    next(); return true;
  }
  void expect(std::string_view s) {if(!take(s))fail("unexpected token");}
  std::string identifier() {
    if (token_.text.empty() || !letter(token_.text[0])) fail("expected identifier");
    // Scalar and native-call frontends must agree: checked guard CFG is
    // unsupported. A local/function/parameter named guard cannot bypass it.
    if (token_.text=="guard")
      fail("GUARD_BACKEND_UNSUPPORTED: checked early-exit CFG lowering required");
    const std::string result(token_.text);
    next();
    return result;
  }
  int add(Kind kind,std::int64_t value=0,std::string name={},std::vector<int> children={}) {
    if (++node_count_>kMaxNodes || current_->nodes.size()>=kMaxNodesPerFunction)
      fail("callable AST node budget exceeded");
    const int idx=static_cast<int>(current_->nodes.size());
    // Keep every aggregate member explicit. Missing-field-initializers
    // becomes an error under strict compiler builds and can hide new IR state.
    current_->nodes.push_back({kind,value,std::move(name),std::move(children),
                               -1,{},{} });
    return idx;
  }
  void definition() {
    bool defining=false,public_=false;
    while (token_.text=="define" || token_.text=="pub") {
      if (take("define")) {if(defining)fail("duplicate define");defining=true;}
      else if (take("pub")) {if(public_)fail("duplicate pub");public_=true;}
    }
    expect("fnc");
    const std::string name=identifier();
    if (name=="int"||name=="const"||name=="return"||name=="if"||name=="else"||name=="then"||name=="guard")
      fail("reserved function name");
    if (names_.contains(name))fail("duplicate function");
    if (module_.functions.size()>=kMaxFunctions)fail("function count budget exceeded");
    const int index=static_cast<int>(module_.functions.size());
    names_[name]=index;
    module_.functions.push_back({name,{},{},-1});
    current_=&module_.functions.back();
    local_names_.clear();
    expect("(");
    if (token_.text!=")") {
      do {
        const bool type_first=take("int");
        const std::string param=identifier();
        if (param=="int"||param=="const"||param=="if"||param=="then"||param=="else"||
            param=="return"||param=="fnc"||param=="guard"||
            std::find(current_->parameters.begin(),current_->parameters.end(),param)
            !=current_->parameters.end())fail("invalid or duplicate parameter");
        if(!type_first){expect(":");expect("int");}
        current_->parameters.push_back(param);
        if(current_->parameters.size()>4)fail("function parameter budget exceeded");
        if (!take(","))break;
      } while(true);
    }
    expect(")");
    if(!take(":"))expect("->");
    expect("int");expect("{");
    // The callable subset has only checked i64; unqualified and const
    // declarations are immutable, and the initializer cannot see itself.
    std::vector<std::pair<std::string,int>> bindings;
    while(token_.text=="int" || token_.text=="const") {
      const bool inferred=take("const");
      if(!inferred || token_.text=="int")expect("int");
      const std::string local=identifier();
      if(local=="int"||local=="const"||local=="return"||local=="fnc"||
         local=="if"||local=="then"||local=="else"||
         local_names_.contains(local) ||
         std::find(current_->parameters.begin(),current_->parameters.end(),local)
            !=current_->parameters.end())
        fail("invalid or duplicate immutable local");
      expect("=");
      const int initializer=expr(0);
      expect(";");
      local_names_.insert(local);
      bindings.emplace_back(local,initializer);
      if(bindings.size()>kMaxLocals)fail("immutable local budget exceeded");
    }
    if(local_names_.contains(std::string(token_.text)))
      fail("immutable local reassignment is forbidden");
    expect("return");
    current_->result=expr(0);
    expect(";");
    // Postfix tree remains owned: local occurrences are new Local nodes,
    // and the Let initializer + body have distinct reachable parents.
    for(auto it=bindings.rbegin();it!=bindings.rend();++it)
      current_->result=add(Kind::Let,0,it->first,{it->second,current_->result});
    expect("}");
  }
  void depth(std::size_t n) {if(n>kMaxDepth)fail("callable expression nesting too deep");}
  int expr(std::size_t d) {
    depth(d);
    if (take("if")) {
      const int condition=comparison(d+1);
      expect("then");
      const int yes=expr(d+1);
      expect("else");
      const int no=expr(d+1);
      return add(Kind::If,0,{}, {condition,yes,no});
    }
    return comparison(d+1);
  }
  int comparison(std::size_t d) {
    depth(d);
    int left=sum(d+1);
    if (token_.text=="<"||token_.text=="<="||token_.text==">"||
        token_.text==">="||token_.text=="=="||token_.text=="!=") {
      const std::string op(token_.text);
      next();
      const int right=sum(d+1);
      Kind k=Kind::Equal;
      if(op=="<") k=Kind::Less;
      else if(op=="<=") k=Kind::LessEqual;
      else if(op==">") k=Kind::Greater;
      else if(op==">=") k=Kind::GreaterEqual;
      else if(op=="!=") k=Kind::NotEqual;
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
      left=add(Kind::Mul,0,{}, {left,right});
    }
    return left;
  }
  int factor(std::size_t d) {
    depth(d);
    if(take("+"))return factor(d+1);
    if(take("-")) {
      if(token_.text=="9223372036854775808"){
        next();return add(Kind::Constant,std::numeric_limits<std::int64_t>::min());
      }
      const int zero=add(Kind::Constant,0);
      const int other=factor(d+1);
      return add(Kind::Sub,0,{}, {zero,other});
    }
    if(take("(")) {
      const int x=expr(d+1);
      expect(")");
      return x;
    }
    if (token_.text=="if") return expr(d+1);
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
        if(found!=params.end())
          return add(Kind::Parameter,static_cast<std::int64_t>(found-params.begin()));
        if(local_names_.contains(name))return add(Kind::Local,0,name);
        fail("unknown parameter");
      }
      std::vector<int> args;
      std::vector<std::string> labels;
      bool named_seen=false;
      if (token_.text!=")") {
        do {
          std::string label;
          if(!token_.text.empty() && letter(token_.text.front())) {
            const auto cursor=at_;
            const auto saved=token_;
            const std::string candidate(token_.text);
            next();
            if(take(":")) {
              label=candidate;
              named_seen=true;
            } else {
              at_=cursor;
              token_=saved;
            }
          }
          if(label.empty() && named_seen)
            fail("positional argument after named argument");
          labels.push_back(std::move(label));
          args.push_back(expr(d+1));
          if(args.size()>4)fail("call argument budget exceeded");
          if(!take(","))break;
        }while(true);
      }
      expect(")");
      const int index=add(Kind::Call,0,name,std::move(args));
      current_->nodes[static_cast<std::size_t>(index)].argument_labels=std::move(labels);
      return index;
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
    case Kind::Less: return a<b;
    case Kind::LessEqual: return a<=b;
    case Kind::Greater: return a>b;
    case Kind::GreaterEqual: return a>=b;
    case Kind::Equal: return a==b;
    case Kind::NotEqual: return a!=b;
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
    return visit(f,f.result,args,depth,{});
  }
  std::int64_t visit(const Function& fn,int id,std::span<const std::int64_t> args,
                     std::size_t depth,const std::map<std::string,std::int64_t>& locals) {
    if (remaining--==0)throw std::runtime_error("callable execution budget exceeded");
    const auto& n=fn.nodes.at(static_cast<std::size_t>(id));
    if(n.kind==Kind::Constant)return n.value;
    if(n.kind==Kind::Parameter)return args[static_cast<std::size_t>(n.value)];
    if(n.kind==Kind::Local)return locals.at(n.name);
    if(n.kind==Kind::Let) {
      const auto initializer=visit(fn,n.children[0],args,depth,locals);
      auto nested=locals;
      if(!nested.emplace(n.name,initializer).second)
        throw std::runtime_error("duplicate immutable local");
      return visit(fn,n.children[1],args,depth,nested);
    }
    if(n.kind==Kind::If) {
      const auto pred=visit(fn,n.children[0],args,depth,locals);
      return visit(fn,n.children[pred ? 1u:2u],args,depth,locals);
    }
    if(n.kind==Kind::Call) {
      // Eagerly evaluate operands in lexical order, then place each value
      // in its declared formal slot; never reorder guest evaluation.
      std::vector<std::int64_t> vals(n.children.size());
      for(std::size_t i=0;i<n.children.size();++i) {
        const auto value=visit(fn,n.children[i],args,depth,locals);
        const auto slot=n.argument_slots.at(i);
        vals[slot]=value;
      }
      return evaluate(static_cast<std::size_t>(n.callee),vals,depth+1);
    }
    const auto a=visit(fn,n.children.at(0),args,depth,locals);
    const auto b=visit(fn,n.children.at(1),args,depth,locals);
    return checked(n.kind,a,b);
  }
};

const char* argument_reg(std::size_t i) {
  constexpr const char* regs[]={"%rdi","%rsi","%rdx","%rcx"};
  return regs[i];
}
struct Native {
  const Module& program;
  explicit Native(const Module& p) : program(p) {}
  std::ostringstream out;
  std::size_t label=0;
  std::string value_slot(int id) const {
    return "-"+std::to_string(8u*(static_cast<std::size_t>(id)+6u))+"(%rbp)";
  }
  std::string parameter_slot(int id) const {
    return "-"+std::to_string(8u*(static_cast<std::size_t>(id)+1u))+"(%rbp)";
  }
  void emit_node(const Function& fn,int index,
                 const std::map<std::string,int>& locals) {
    const auto& n=fn.nodes.at(static_cast<std::size_t>(index));
    // The interpreter charges one visit per evaluated AST node. Enforce
    // the same bounded work at runtime, including in exponential recursion
    // whose call depth remains well below kMaxStackDepth. %r9 is private to
    // this narrow in-module x86-64 callable ABI: only generated functions
    // call each other, none clobber the per-entry budget register.
    out<<"  subq $1, %r9\n  js .Lores_budget\n";
    if(n.kind==Kind::Constant) {
      out<<"  movabsq $"<<n.value<<", %rax\n";
    } else if(n.kind==Kind::Parameter) {
      out<<"  movq "<<parameter_slot(static_cast<int>(n.value))<<", %rax\n";
    } else if(n.kind==Kind::Local) {
      out<<"  movq "<<value_slot(locals.at(n.name))<<", %rax\n";
    } else if(n.kind==Kind::Let) {
      // The initializer retains its own frame slot; the binding is
      // immutable, and no heap allocation or global state is required.
      emit_node(fn,n.children[0],locals);
      auto nested=locals;
      if(!nested.emplace(n.name,n.children[0]).second)
        throw std::runtime_error("duplicate immutable local");
      emit_node(fn,n.children[1],nested);
    } else if(n.kind==Kind::If) {
      const auto tag=label++;
      emit_node(fn,n.children[0],locals);
      out<<"  cmpq $0, %rax\n  je .Lores_else_"<<tag<<"\n";
      emit_node(fn,n.children[1],locals);
      out<<"  jmp .Lores_end_"<<tag<<"\n"
         <<".Lores_else_"<<tag<<":\n";
      emit_node(fn,n.children[2],locals);
      out<<".Lores_end_"<<tag<<":\n";
    } else if(n.kind==Kind::Call) {
      for (int child:n.children)emit_node(fn,child,locals);
      for (std::size_t i=0;i<n.children.size();++i) {
        const auto slot=n.argument_slots.at(i);
        out<<"  movq "<<value_slot(n.children[i])<<", "<<argument_reg(slot)<<"\n";
      }
      out<<"  movq -40(%rbp), %r8\n"
         <<"  addq $1, %r8\n"
         <<"  call .Lores_fn_"<<n.callee<<"\n";
    } else {
      emit_node(fn,n.children[0],locals);
      emit_node(fn,n.children[1],locals);
      out<<"  movq "<<value_slot(n.children[0])<<", %rax\n";
      switch(n.kind) {
        case Kind::Add: out<<"  addq ";break;
        case Kind::Sub: out<<"  subq ";break;
        case Kind::Mul: out<<"  imulq ";break;
        default: out<<"  cmpq ";break;
      }
      out<<value_slot(n.children[1])<<", %rax\n";
      if(n.kind==Kind::Add||n.kind==Kind::Sub||n.kind==Kind::Mul) {
        out<<"  jo .Lores_overflow\n";
      } else {
        const char* cc="e";
        switch(n.kind) {
          case Kind::Less: cc="l";break;
          case Kind::LessEqual: cc="le";break;
          case Kind::Greater: cc="g";break;
          case Kind::GreaterEqual: cc="ge";break;
          case Kind::Equal: cc="e";break;
          case Kind::NotEqual: cc="ne";break;
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
       <<"  movq $"<<kMaxEvalSteps<<", %r9\n"
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
      emit_node(fn,fn.result,{});
      out<<"  leave\n  ret\n";
    }
    out<<".Lores_depth:\n  ud2\n"
       <<".Lores_overflow:\n  ud2\n"
       <<".Lores_budget:\n  ud2\n"
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
  // Public Module clients may mutate entry after source parsing. Never run
  // an arbitrary helper as the program entry by redirecting an in-range ID.
  // The source compiler chooses main by name; the public verifier must agree.
  if(m.functions[static_cast<std::size_t>(m.entry)].name!="main")
    throw std::runtime_error("callable entry must name main");
  if (m.functions.size()>kMaxFunctions)throw std::runtime_error("function count budget exceeded");
  std::map<std::string,std::size_t> names;
  std::size_t total=0;
  for (std::size_t fi=0;fi<m.functions.size();++fi) {
    const auto& fn=m.functions[fi];
    // Public Module clients bypass Parser; uphold the source keyword ban
    // even for helper functions that the entry never calls.
    if(!valid_identifier(fn.name) ||
       fn.name=="int" || fn.name=="const" || fn.name=="return" ||
       fn.name=="if" || fn.name=="else" || fn.name=="then" || fn.name=="guard" ||
       !names.emplace(fn.name,fi).second)
      throw std::runtime_error("duplicate or invalid function");
    if(fn.parameters.size()>4)throw std::runtime_error("function parameter budget exceeded");
    // Public Module values may be constructed or mutated without Parser.
    // Unlike the native emitter's stack slots, a Local must never alias a
    // Parameter, and duplicate parameter names must not collapse a scope.
    std::set<std::string> parameters;
    for(const auto& param:fn.parameters) {
      if(!valid_identifier(param) ||
         param=="int" || param=="const" || param=="if" ||
         param=="then" || param=="else" || param=="return" ||
         param=="fnc" || param=="guard" || !parameters.insert(param).second)
        throw std::runtime_error("invalid or duplicate callable parameter");
    }
    if(fn.nodes.empty() || fn.nodes.size()>kMaxNodesPerFunction)
      throw std::runtime_error("invalid callable function body");
    total+=fn.nodes.size();
    if(total>kMaxNodes)throw std::runtime_error("callable AST node budget exceeded");
    if(fn.result<0||static_cast<std::size_t>(fn.result)!=fn.nodes.size()-1)
      throw std::runtime_error("invalid callable return/root");
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
        case Kind::Local: case Kind::Let: break;
        default: throw std::runtime_error("unknown callable opcode");
      }
      if(n.kind!=Kind::Call &&
         (!n.argument_labels.empty() || !n.argument_slots.empty()))
        throw std::runtime_error("unexpected non-call argument bindings");
      if(n.kind!=Kind::Call && n.kind!=Kind::Local && n.kind!=Kind::Let &&
         !n.name.empty())
        throw std::runtime_error("unexpected callable symbol payload");
      // Every opcode except constants and parameter indexes must carry
      // its data solely in owned child edges, not an ignored i64 payload.
      // Forged Module objects may bypass Parser and must be fail-closed.
      if(n.kind!=Kind::Constant && n.kind!=Kind::Parameter && n.value!=0)
        throw std::runtime_error("unexpected callable numeric payload");
      if((n.kind==Kind::Let || n.kind==Kind::Local) &&
         (!valid_identifier(n.name) || n.value!=0 || n.callee!=-1))
        throw std::runtime_error("invalid callable local payload");
      if(n.kind==Kind::Let &&
         (n.name=="int"||n.name=="const"||n.name=="return"||n.name=="fnc"||
          n.name=="if"||n.name=="then"||n.name=="else"||n.name=="guard"))
        throw std::runtime_error("invalid callable local name");
      for(int arg:n.children) {
        if(arg<0 || static_cast<std::size_t>(arg)>=i)
          throw std::runtime_error("callable use before definition");
        if(++incoming[static_cast<std::size_t>(arg)]!=1)
          throw std::runtime_error("callable AST shared node");
      }
      std::size_t size=2;
      if(n.kind==Kind::Constant||n.kind==Kind::Parameter||n.kind==Kind::Local)size=0;
      else if(n.kind==Kind::If)size=3;
      else if(n.kind==Kind::Call)size=n.children.size();
      if(n.children.size()!=size)throw std::runtime_error("callable operand arity mismatch");
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
        const auto expected=bind_call_slots(
          n,m.functions[static_cast<std::size_t>(n.callee)]);
        // Every nonzero-arity call needs explicit source-owned bindings.
        // Hosts cannot erase metadata and reinterpret it as positional.
        if(n.argument_slots!=expected)
          throw std::runtime_error("callable named-binding slots mismatch");
      } else if(n.callee!=-1)throw std::runtime_error("unexpected callable target");
    }
    // Independently validate lexical scope on the owned expression tree.
    // Verification must reject a tampered local before emitting any native
    // load; the parser's declaration checks are insufficient for public IR.
    std::function<void(int,const std::set<std::string>&,std::size_t)> lexical =
      [&](int id,const std::set<std::string>& scope,std::size_t count) {
        const auto& n=fn.nodes[static_cast<std::size_t>(id)];
        if(n.kind==Kind::Local) {
          // The native emitter resolves locals to initializer frame slots,
          // not ABI parameter slots. Never accept a spoofed parameter name.
          if(parameters.contains(n.name) || !scope.contains(n.name))
            throw std::runtime_error("callable local out of scope");
          return;
        }
        if(n.kind==Kind::Let) {
          if(parameters.contains(n.name) || scope.contains(n.name))
            throw std::runtime_error("duplicate immutable local");
          if(count>=kMaxLocals)
            throw std::runtime_error("immutable local budget exceeded");
          lexical(n.children[0],scope,count);
          auto nested=scope;
          nested.insert(n.name);
          lexical(n.children[1],nested,count+1);
          return;
        }
        for(int child:n.children)lexical(child,scope,count);
      };
    lexical(fn.result,{},0);
    for(std::size_t i=0;i<fn.nodes.size();++i) {
      const unsigned expected=(i==static_cast<std::size_t>(fn.result))?0u:1u;
      if(incoming[i]!=expected)
        throw std::runtime_error("callable AST unreachable node");
    }
  }
  // Entry identity was already enforced near the top of verify(); do not
  // repeat checks with a second, inconsistent diagnostic at the end.
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
