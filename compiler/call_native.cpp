#include "compiler/source_utf8.hpp"
#include "compiler/call_native.hpp"
#include "contracts/v1/native-error-slot-v1.h"
#include <algorithm>
#include <charconv>
#include <cstdint>
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
bool letter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
bool space(char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\v' || c == '\f'; }
struct Token { std::string_view text; std::size_t offset=0; };

class Parser {
 public:
  explicit Parser(std::string_view text,bool library=false)
      : text_(text), library_(library) { next(); }
  Parser(std::string_view text,Module imported,std::string expected_import)
      : text_(text), linked_(true), expected_import_(std::move(expected_import)) {
    if(imported.module_name.empty())throw std::runtime_error("linked library lacks module identity");
    imported_binding_=imported.module_name;
    for(const auto& name:imported.exports)
      imported_public_.insert(imported_binding_+"."+name);
    for(auto& fn:imported.functions) {
      fn.name=imported_binding_+"."+fn.name;
      for(auto& node:fn.nodes)
        if(node.kind==Kind::Call)node.name=imported_binding_+"."+node.name;
      if(names_.contains(fn.name))throw std::runtime_error("duplicate linked function");
      names_[fn.name]=static_cast<int>(module_.functions.size());
      module_.functions.push_back(std::move(fn));
    }
    next();
  }
  Module parse() {
    if (library_) {
      bool defining=false, public_=false;
      while (token_.text=="define" || token_.text=="pub") {
        if (take("define")) { if (defining) fail("duplicate define"); defining=true; }
        else if (take("pub")) { if (public_) fail("duplicate pub"); public_=true; }
      }
      if (!defining) fail("library module requires define");
      expect("module");
      module_.module_name=identifier();
      expect("as");
      while (token_.text!="end") {
        if (token_.text.empty()) fail("unterminated module");
        definition();
      }
      expect("end");
      if (!token_.text.empty()) fail("unexpected trailing token");
    } else if (linked_) {
      expect("import"); expect("module");
      const std::string binding=identifier();
      if(binding!=imported_binding_)fail("import binding does not match library module");
      expect("from");
      const std::string specifier=string_literal();
      if(specifier!=expected_import_)fail("import specifier does not match supplied library");
      expect(";");
      while (!token_.text.empty()) definition();
      for (std::size_t i=0; i<module_.functions.size(); ++i) {
        if (module_.functions[i].name=="main") module_.entry=static_cast<int>(i);
      }
      if (module_.entry<0) fail("missing main function");
    } else {
      while (!token_.text.empty()) definition();
      for (std::size_t i=0; i<module_.functions.size(); ++i) {
        if (module_.functions[i].name=="main") module_.entry=static_cast<int>(i);
      }
      if (module_.entry<0) fail("missing main function");
    }
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
    if (!library_) verify(module_);
    return std::move(module_);
  }
 private:
  std::string_view text_;
  bool library_ = false;
  bool linked_ = false;
  std::string expected_import_;
  std::string imported_binding_;
  std::set<std::string> imported_public_;
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
    if (c=='"') {
      bool closed=false;
      while(at_<text_.size()) {
        const char q=text_[at_++];
        if(q=='\\')throw std::runtime_error(
            "callable import string escapes unsupported at byte "+std::to_string(begin));
        if(q=='"') {closed=true;break;}
        if(q=='\n'||q=='\r')throw std::runtime_error(
            "unterminated callable import string at byte "+std::to_string(begin));
      }
      if(!closed)throw std::runtime_error(
          "unterminated callable import string at byte "+std::to_string(begin));
    } else if (letter(c)) {
      while (at_<text_.size() && (letter(text_[at_])||digit(text_[at_]))) ++at_;
    } else if (digit(c)) {
      while (at_<text_.size() && digit(text_[at_])) ++at_;
    } else if ((c=='<'||c=='>'||c=='='||c=='!') &&
               at_<text_.size() && text_[at_]=='=') {
      ++at_;
    } else if (c=='-' && at_<text_.size() && text_[at_]=='>') {
      ++at_;
    } else if (std::string_view("()+-*{};:,<>=.").find(c)==std::string_view::npos) {
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
    const std::string result(token_.text);
    next();
    return result;
  }
  std::string string_literal() {
    if(token_.text.size()<2 || token_.text.front()!='"' || token_.text.back()!='"')
      fail("expected quoted import specifier");
    const std::string result(token_.text.substr(1,token_.text.size()-2));
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
  void definition() {
    bool defining=false,public_=false;
    while (token_.text=="define" || token_.text=="pub") {
      if (take("define")) {if(defining)fail("duplicate define");defining=true;}
      else if (take("pub")) {if(public_)fail("duplicate pub");public_=true;}
    }
    expect("fnc");
    const std::string name=identifier();
    if (name=="int"||name=="return"||name=="if"||name=="else"||name=="then"||
        name=="do"||name=="fi"||name=="module"||name=="as"||name=="end"||
        name=="try"||name=="catch"||name=="finally"||name=="is"||name=="type")
      fail("reserved function name");
    if (names_.contains(name))fail("duplicate function");
    if (module_.functions.size()>=kMaxFunctions)fail("function count budget exceeded");
    const int index=static_cast<int>(module_.functions.size());
    names_[name]=index;
    module_.functions.push_back({name,{},{}});
    current_=&module_.functions.back();
    if(public_)module_.exports.push_back(name);
    expect("(");
    if (token_.text!=")") {
      do {
        std::string param;
        if (take("int")) {
          param=identifier();
        } else {
          param=identifier();
          expect(":"); expect("int");
        }
        if (param=="int"||param=="if"||param=="then"||param=="else"||
            param=="do"||param=="fi"||param=="module"||param=="as"||param=="end"||
            param=="try"||param=="catch"||param=="finally"||param=="is"||param=="type"||
            param=="return"||param=="fnc"||
            std::find(current_->parameters.begin(),current_->parameters.end(),param)
            !=current_->parameters.end())fail("invalid or duplicate parameter");
        current_->parameters.push_back(param);
        if(current_->parameters.size()>4)fail("function parameter budget exceeded");
        if (!take(","))break;
      } while(true);
    }
    expect(")");
    if(!take(":"))expect("->");
    expect("int");expect("{");
    if (take("return")) {
      current_->result=expr(0);
      expect(";");
    } else if (take("if")) {
      const int condition=comparison(1);
      expect(";"); expect("do"); expect("return");
      const int yes=expr(1);
      expect(";"); expect("else"); expect("return");
      const int no=expr(1);
      expect(";"); expect("fi");
      current_->result=add(Kind::If,0,{}, {condition,yes,no});
    } else if (take("try")) {
      expect("{"); expect("return");
      const int protected_result=expr(1);
      expect(";"); expect("}");
      expect("catch"); expect("(");
      const std::string binding=identifier();
      if(std::find(current_->parameters.begin(),current_->parameters.end(),binding)
          !=current_->parameters.end())
        fail("callable ErrorSlot catch binding shadows parameter");
      expect(")"); expect("{");
      if(take("if")) {
        if(identifier()!=binding)
          fail("callable ErrorSlot type test must use catch binding");
        expect("is"); expect("type");
        const std::string type=identifier();
        if(type!="ArithmeticOverflowError" && type!="Error")
          fail("callable ErrorSlot type test supports only ArithmeticOverflowError or Error");
        expect("{"); expect("return");
        const int matched=expr(1);
        if(current_->nodes.at(static_cast<std::size_t>(matched)).kind!=Kind::Constant)
          fail("callable ErrorSlot type-match result must be integer literal");
        expect(";"); expect("}");
        expect("else"); expect("{"); expect("return");
        const int fallback=expr(1);
        if(current_->nodes.at(static_cast<std::size_t>(fallback)).kind!=Kind::Constant)
          fail("callable ErrorSlot type-mismatch result must be integer literal");
        expect(";"); expect("}"); expect("fi");
        current_->result=add(Kind::Catch,0,type,
          {protected_result,matched,fallback});
      } else {
        expect("return");
        const int handler=expr(1);
        if(current_->nodes.at(static_cast<std::size_t>(handler)).kind!=Kind::Constant)
          fail("callable ErrorSlot catch result must be integer literal");
        expect(";");
        current_->result=add(Kind::Catch,0,{}, {protected_result,handler});
      }
      expect("}");
      expect("finally"); expect("{");
      if(token_.text!="}")
        fail("callable ErrorSlot catch v1 requires empty finally");
      expect("}");
    } else {
      fail("expected return, canonical if/do/fi, or ErrorSlot try/catch body");
    }
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
      std::string name=identifier();
      bool qualified=false;
      if(take(".")) {
        qualified=true;
        name+="."+identifier();
        if(!linked_)fail("qualified callable requires linked module");
        if(!imported_public_.contains(name))fail("imported function is private or missing");
      }
      if (!take("(")) {
        if(qualified)fail("qualified imported value must be called");
        auto& params=current_->parameters;
        auto found=std::find(params.begin(),params.end(),name);
        if(found==params.end())fail("unknown parameter");
        return add(Kind::Parameter,static_cast<std::int64_t>(found-params.begin()));
      }
      std::vector<int> args;
      if (token_.text!=")") {
        do {
          args.push_back(expr(d+1));
          if(args.size()>4)fail("call argument budget exceeded");
          if(!take(","))break;
        }while(true);
      }
      expect(")");
      return add(Kind::Call,0,name,std::move(args));
    }
    fail("expected callable expression");
  }
};

void clear_error(OresNativeErrorSlotV1& error) {
  error.code=ORES_ERROR_NONE_V1;
  error.effect=ORES_EFFECT_NONE_V1;
  error.flags=0;
  error.reserved=0;
  error.payload=0;
}
void arithmetic_overflow(OresNativeErrorSlotV1& error) {
  error.code=ORES_ERROR_ARITHMETIC_OVERFLOW_V1;
  error.effect=ORES_EFFECT_THROW_V1;
  error.flags=0;
  error.reserved=0;
  error.payload=0;
}
std::int64_t checked(Kind k,std::int64_t a,std::int64_t b,
                     OresNativeErrorSlotV1& error) {
  std::int64_t out=0;
  switch(k) {
    case Kind::Add:
      if(__builtin_add_overflow(a,b,&out)){arithmetic_overflow(error);return 0;}
      return out;
    case Kind::Sub:
      if(__builtin_sub_overflow(a,b,&out)){arithmetic_overflow(error);return 0;}
      return out;
    case Kind::Mul:
      if(__builtin_mul_overflow(a,b,&out)){arithmetic_overflow(error);return 0;}
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
  OresNativeErrorSlotV1 error{};
  std::size_t remaining=kMaxEvalSteps;
  std::int64_t evaluate(std::size_t id,std::span<const std::int64_t> args,std::size_t depth) {
    if (depth>=kMaxStackDepth)throw std::runtime_error("call depth exceeded");
    const auto& f=m.functions.at(id);
    if(args.size()!=f.parameters.size())throw std::runtime_error("runtime argument count mismatch");
    return visit(f,f.result,args,depth);
  }
  std::int64_t visit(const Function& fn,int id,std::span<const std::int64_t> args,
                     std::size_t depth) {
    if(error.code!=ORES_ERROR_NONE_V1)return 0;
    if (remaining--==0)throw std::runtime_error("callable execution budget exceeded");
    const auto& n=fn.nodes.at(static_cast<std::size_t>(id));
    if(n.kind==Kind::Constant)return n.value;
    if(n.kind==Kind::Parameter)return args[static_cast<std::size_t>(n.value)];
    if(n.kind==Kind::If) {
      const auto pred=visit(fn,n.children[0],args,depth);
      if(error.code!=ORES_ERROR_NONE_V1)return 0;
      return visit(fn,n.children[pred ? 1u:2u],args,depth);
    }
    if(n.kind==Kind::Catch) {
      const auto protected_value=visit(fn,n.children.at(0),args,depth);
      if(error.code==ORES_ERROR_NONE_V1)return protected_value;
      if(error.effect!=ORES_EFFECT_THROW_V1)return 0;
      const auto code=error.code;
      clear_error(error);
      if(n.name.empty())
        return visit(fn,n.children.at(1),args,depth);
      bool match=false;
      if(n.name=="ArithmeticOverflowError")
        match=code==ORES_ERROR_ARITHMETIC_OVERFLOW_V1;
      else if(n.name=="Error")
        match=code!=ORES_ERROR_NONE_V1;
      else
        throw std::runtime_error("unsupported callable guest Error type");
      return visit(fn,n.children.at(match ? 1u:2u),args,depth);
    }
    if(n.kind==Kind::Call) {
      std::vector<std::int64_t> vals;
      for (int arg:n.children) {
        vals.push_back(visit(fn,arg,args,depth));
        if(error.code!=ORES_ERROR_NONE_V1)return 0;
      }
      return evaluate(static_cast<std::size_t>(n.callee),vals,depth+1);
    }
    const auto a=visit(fn,n.children.at(0),args,depth);
    if(error.code!=ORES_ERROR_NONE_V1)return 0;
    const auto b=visit(fn,n.children.at(1),args,depth);
    if(error.code!=ORES_ERROR_NONE_V1)return 0;
    return checked(n.kind,a,b,error);
  }
};

const char* argument_reg(std::size_t i) {
  constexpr const char* regs[]={"%rdi","%rsi","%rdx","%rcx"};
  return regs[i];
}
struct Native {
  const Module& program;
  std::ostringstream out;
  std::size_t label=0;
  std::string value_slot(int id) const {
    return "-"+std::to_string(8u*(static_cast<std::size_t>(id)+6u))+"(%rbp)";
  }
  std::string parameter_slot(int id) const {
    return "-"+std::to_string(8u*(static_cast<std::size_t>(id)+1u))+"(%rbp)";
  }
  std::string propagate_target(const std::string& target) const {
    return target.empty()?".Lores_error_return":target;
  }
  void emit_clear_error() {
    out<<"  movq $0, 0(%r9)\n"
       <<"  movq $0, 8(%r9)\n";
  }
  void emit_overflow_state() {
    out<<"  movl $"<<ORES_ERROR_ARITHMETIC_OVERFLOW_V1<<", 0(%r9)\n"
       <<"  movb $"<<static_cast<unsigned>(ORES_EFFECT_THROW_V1)<<", 4(%r9)\n"
       <<"  movb $0, 5(%r9)\n"
       <<"  movw $0, 6(%r9)\n"
       <<"  movq $0, 8(%r9)\n";
  }
  void emit_node(const Function& fn,int index,
                 const std::string& error_target={}) {
    const auto& n=fn.nodes.at(static_cast<std::size_t>(index));
    if(n.kind==Kind::Constant) {
      out<<"  movabsq $"<<n.value<<", %rax\n";
    } else if(n.kind==Kind::Parameter) {
      out<<"  movq "<<parameter_slot(static_cast<int>(n.value))<<", %rax\n";
    } else if(n.kind==Kind::If) {
      const auto tag=label++;
      emit_node(fn,n.children[0],error_target);
      out<<"  cmpq $0, %rax\n  je .Lores_else_"<<tag<<"\n";
      emit_node(fn,n.children[1],error_target);
      out<<"  jmp .Lores_end_"<<tag<<"\n"
         <<".Lores_else_"<<tag<<":\n";
      emit_node(fn,n.children[2],error_target);
      out<<".Lores_end_"<<tag<<":\n";
    } else if(n.kind==Kind::Catch) {
      const auto tag=label++;
      const std::string caught=".Lores_error_catch_"+std::to_string(tag);
      const std::string merge=".Lores_error_catch_merge_"+std::to_string(tag);
      const std::string outer=propagate_target(error_target);
      emit_node(fn,n.children.at(0),caught);
      out<<"  jmp "<<merge<<"\n"
         <<caught<<":\n"
         <<"  cmpl $0, 0(%r9)\n"
         <<"  je "<<outer<<"\n"
         <<"  cmpb $"<<static_cast<unsigned>(ORES_EFFECT_THROW_V1)
         <<", 4(%r9)\n"
         <<"  jne "<<outer<<"\n";
      if(n.name.empty()) {
        emit_clear_error();
        emit_node(fn,n.children.at(1),error_target);
        out<<"  jmp "<<merge<<"\n";
      } else if(n.name=="ArithmeticOverflowError") {
        const std::string mismatch=
            ".Lores_error_type_mismatch_"+std::to_string(tag);
        out<<"  cmpl $"<<ORES_ERROR_ARITHMETIC_OVERFLOW_V1
           <<", 0(%r9)\n"
           <<"  jne "<<mismatch<<"\n";
        emit_clear_error();
        emit_node(fn,n.children.at(1),error_target);
        out<<"  jmp "<<merge<<"\n"
           <<mismatch<<":\n";
        emit_clear_error();
        emit_node(fn,n.children.at(2),error_target);
        out<<"  jmp "<<merge<<"\n";
      } else if(n.name=="Error") {
        // Catch admission already proved a nonzero registered guest code.
        emit_clear_error();
        emit_node(fn,n.children.at(1),error_target);
        out<<"  jmp "<<merge<<"\n";
      } else {
        throw std::runtime_error("unsupported callable guest Error type");
      }
      out<<merge<<":\n";
    } else if(n.kind==Kind::Call) {
      for (int child:n.children)emit_node(fn,child,error_target);
      for (std::size_t i=0;i<n.children.size();++i)
        out<<"  movq "<<value_slot(n.children[i])<<", "<<argument_reg(i)<<"\n";
      out<<"  movq -40(%rbp), %r8\n"
         <<"  addq $1, %r8\n"
         <<"  call .Lores_fn_"<<n.callee<<"\n"
         <<"  cmpl $0, 0(%r9)\n"
         <<"  jne "<<propagate_target(error_target)<<"\n";
    } else {
      emit_node(fn,n.children[0],error_target);
      emit_node(fn,n.children[1],error_target);
      out<<"  movq "<<value_slot(n.children[0])<<", %rax\n";
      switch(n.kind) {
        case Kind::Add: out<<"  addq ";break;
        case Kind::Sub: out<<"  subq ";break;
        case Kind::Mul: out<<"  imulq ";break;
        default: out<<"  cmpq ";break;
      }
      out<<value_slot(n.children[1])<<", %rax\n";
      if(n.kind==Kind::Add||n.kind==Kind::Sub||n.kind==Kind::Mul) {
        if(error_target.empty()) {
          out<<"  jo .Lores_overflow\n";
        } else {
          const auto tag=label++;
          out<<"  jo .Lores_overflow_to_"<<tag<<"\n"
             <<"  jmp .Lores_overflow_ok_"<<tag<<"\n"
             <<".Lores_overflow_to_"<<tag<<":\n";
          emit_overflow_state();
          out<<"  jmp "<<error_target<<"\n"
             <<".Lores_overflow_ok_"<<tag<<":\n";
        }
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
       <<"oresforge_entry:\n"
       // SysV enters with rsp = 8 mod 16. Reserve the 16-byte ErrorSlot plus
       // 8 bytes padding so rsp is 16-byte aligned before the guest call.
       <<"  subq $24, %rsp\n"
       <<"  movq %rsp, %r9\n"
       <<"  movq $0, 0(%r9)\n"
       <<"  movq $0, 8(%r9)\n"
       <<"  xorl %r8d, %r8d\n"
       <<"  call .Lores_fn_"<<program.entry<<"\n"
       <<"  cmpl $0, 0(%r9)\n"
       <<"  jne .Lores_unhandled_error\n"
       <<"  addq $24, %rsp\n"
       <<"  ret\n";
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
    out<<".Lores_error_return:\n"
       <<"  xorl %eax, %eax\n"
       <<"  leave\n"
       <<"  ret\n"
       <<".Lores_overflow:\n";
    emit_overflow_state();
    out<<"  jmp .Lores_error_return\n"
       <<".Lores_unhandled_error:\n"
       <<"  movb $"<<static_cast<unsigned>(ORES_EFFECT_PANIC_V1)
       <<", 4(%r9)\n"
       <<"  ud2\n"
       <<".Lores_depth:\n  ud2\n"
       <<".size oresforge_entry, .-oresforge_entry\n"
       <<".section .note.GNU-stack,\"\",@progbits\n";
    return out.str();
  }
};}
Module parse(std::string_view source) {
  if (source.size()>kMaxSource)throw std::runtime_error("callable source budget exceeded");
  oresforge::source::validate_utf8(source);
  return Parser(source).parse();
}
Module parse_library(std::string_view source) {
  if (source.size()>kMaxSource)throw std::runtime_error("callable source budget exceeded");
  oresforge::source::validate_utf8(source);
  return Parser(source,true).parse();
}
Module parse_linked(std::string_view entry_source,
                    std::string_view library_source,
                    std::string_view expected_import_specifier) {
  if(entry_source.size()>kMaxSource || library_source.size()>kMaxSource)
    throw std::runtime_error("callable source budget exceeded");
  if(expected_import_specifier.empty() || expected_import_specifier.size()>512 ||
      expected_import_specifier.front()=='/' ||
      expected_import_specifier.find("\\")!=std::string_view::npos ||
      expected_import_specifier.find("//")!=std::string_view::npos)
    throw std::runtime_error("invalid callable linked import specifier");
  oresforge::source::validate_utf8(entry_source);
  oresforge::source::validate_utf8(library_source);
  auto library=Parser(library_source,true).parse();
  return Parser(entry_source,std::move(library),
                std::string(expected_import_specifier)).parse();
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
        case Kind::NotEqual: case Kind::If: case Kind::Catch: case Kind::Call: break;
        default: throw std::runtime_error("unknown callable opcode");
      }
      if(n.kind!=Kind::Call && n.kind!=Kind::Catch && !n.name.empty())
        throw std::runtime_error("unexpected callable symbol payload");
      if(n.kind==Kind::Catch && !n.name.empty() &&
          n.name!="ArithmeticOverflowError" && n.name!="Error")
        throw std::runtime_error("unsupported callable guest Error type");
      for(int arg:n.children) {
        if(arg<0 || static_cast<std::size_t>(arg)>=i)
          throw std::runtime_error("callable use before definition");
        if(++incoming[static_cast<std::size_t>(arg)]!=1)
          throw std::runtime_error("callable AST shared node");
      }
      std::size_t size=2;
      if(n.kind==Kind::Constant||n.kind==Kind::Parameter)size=0;
      else if(n.kind==Kind::If)size=3;
      else if(n.kind==Kind::Catch)size=n.name.empty()?2u:3u;
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
  const auto value=eval.evaluate(static_cast<std::size_t>(m.entry),args,0);
  if(eval.error.code!=ORES_ERROR_NONE_V1) {
    // This is the outer guest-invocation boundary: no recover scope exists in
    // callable v1, so an unhandled throw/raise reaches panic before the host
    // diagnostic. The C++ exception is transport for the CLI only.
    eval.error.effect=ORES_EFFECT_PANIC_V1;
    if(eval.error.code==ORES_ERROR_ARITHMETIC_OVERFLOW_V1)
      throw std::runtime_error("ArithmeticOverflowError: code=65537 effect=panic");
    throw std::runtime_error("unhandled Oreslang guest Error");
  }
  return value;
}
std::string emit_asm(const Module& m,Arch arch) {
  if(arch!=Arch::X86_64)
    throw std::runtime_error("native callable backend currently supports x86_64 only");
  verify(m);
  return Native{m}.build();
}
}
