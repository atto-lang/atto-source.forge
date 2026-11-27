#include "compiler/source_utf8.hpp"
#include "compiler/topic_publish_source_gate.hpp"
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
// Scalar payloads, typed variants, and ordinary effects remain distinct.
bool boolean_kind(Kind k) {
  return k==Kind::BoolIf || k==Kind::BoolCall || k==Kind::Boolean || k==Kind::Not || k==Kind::And || k==Kind::Or || k==Kind::BooleanXor ||
         k==Kind::Less || k==Kind::LessEqual || k==Kind::Greater ||
         k==Kind::GreaterEqual || k==Kind::Equal || k==Kind::NotEqual;
}
bool reserved_predicate_word(std::string_view s) {
  return s=="true" || s=="false" || s=="ifnot" || s=="unless" ||
         s=="eq" || s=="neq" || s=="fi" || s=="guard" || s=="end" ||
         s=="trap" || s=="trap!" || s=="Result" || s=="Error" || s=="val" ||
         s=="Option" || s=="Some" || s=="None" || s=="Ok" || s=="Err" ||
         s=="match" || s=="over" || s=="on";
}

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) { next(); }
  Module parse() {
    bool has_namespace=false;
    if(take("use")) {
      expect("namespace"); (void)identifier(); expect(";");
      has_namespace=true;
    }
    while (!token_.text.empty()) {
      if(token_.text=="use" || token_.text=="namespace")
        fail(has_namespace?"FORGE_NAMESPACE_DUPLICATE: duplicate file header":
             "FORGE_NAMESPACE_HEADER_ORDER: namespace must precede declarations");
      definition();
    }
    for (std::size_t i=0; i<module_.functions.size(); ++i) {
      if (module_.functions[i].name=="main") module_.entry=static_cast<int>(i);
    }
    if (module_.entry<0) fail("missing main function");
    // Resolve every function, including unused ones. No partially linked IR.
    for (auto& fn:module_.functions) {
      for (auto& node:fn.nodes) {
        if (node.kind != Kind::Call && node.kind != Kind::ResultCall && node.kind != Kind::OptionCall && node.kind != Kind::BoolCall) continue;
        const auto found=names_.find(node.name);
        if (found==names_.end()) fail("unknown function");
        node.callee=found->second;
        if (node.children.size()!=module_.functions.at(static_cast<std::size_t>(node.callee)).parameters.size())
          fail("function arity mismatch");
      }
    }
    for (auto& fn:module_.functions) for (auto& node:fn.nodes) {
      if (node.kind!=Kind::Trap) continue;
      auto& operand=fn.nodes.at(static_cast<std::size_t>(node.children.at(0)));
      if (operand.kind==Kind::Call &&
          module_.functions.at(static_cast<std::size_t>(operand.callee)).returns_scalar_result)
        operand.kind=Kind::ResultCall;
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
  std::string local_name_;
  int local_source_=-1;
  std::vector<std::pair<std::string,int>> payload_bindings_;

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
      const auto word=text_.substr(begin,at_-begin);
      const bool bang_keyword=word=="trap" || word=="throw" || word=="raise" ||
          word=="panic" || word=="copy" || word=="take" || word=="move" || word=="borrow" ||
          word=="share" || word=="pin" || word=="drop" || word=="pipe" ||
          word=="rpipe" || word=="stop" || word=="expand";
      const bool word_neq=text_.substr(at_,3)=="!eq" &&
          (at_+3==text_.size() || (!letter(text_[at_+3]) && !digit(text_[at_+3])));
      if (bang_keyword && at_<text_.size() && text_[at_]=='!' &&
          text_.substr(at_,2)!="!=" && !word_neq) {
        const auto suffix=word=="copy" && text_.substr(at_,2)=="!!" ? 2u : 1u;
        const auto after=at_+suffix;
        if (after>=text_.size() || !space(text_[after]))
          throw std::runtime_error("E_BANG_KEYWORD_WHITESPACE: whitespace required after bang keyword at byte "+
                                   std::to_string(begin));
        // Only trap is admitted by this restricted native-call frontend.
        // Other keywords keep their existing fail-closed lowering gates.
        if (word=="trap") ++at_;
        else if (word=="take" || word=="move")
          throw std::runtime_error("BANG_KEYWORD_NATIVE_CALL_UNSUPPORTED: typed lowering required at byte "+std::to_string(begin));
      }
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
    } else if (std::string_view("()+-*/{};:,<>=^?").find(c)==std::string_view::npos) {
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
    // A scalar call graph cannot grant a protected region or actor identity.
    if (take("state")) {
      if (token_.text=="container" || token_.text=="on")
        fail("STATE_CONTAINER_BACKEND_UNSUPPORTED: protected heap region and critical guard lowering required");
      fail("unexpected declaration after state");
    }
    if (take("virtual")) {
      if (token_.text=="actor")
        fail("VIRTUAL_ACTOR_BACKEND_UNSUPPORTED: logical key and activation fencing required");
      fail("unexpected declaration after virtual");
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
    local_name_.clear(); local_source_=-1; payload_bindings_.clear();
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
    if (take("Result")) {
      expect("<");expect("int");expect(",");expect("Error");expect(">");
      current_->returns_scalar_result=true;
      expect("{");
      int binding=-1;
      if (take("val")) {
        const auto name_local=identifier();
        if (std::find(current_->parameters.begin(),current_->parameters.end(),name_local)!=current_->parameters.end())
          fail("Result binding shadows a parameter");
        expect("=");
        binding=factor(0);
        if (current_->nodes.at(static_cast<std::size_t>(binding)).kind!=Kind::Call)
          fail("postfix ? requires a Result callable");
        current_->nodes.at(static_cast<std::size_t>(binding)).kind=Kind::ResultCall;
        expect("?"); expect(";");
        local_name_=name_local; local_source_=binding;
      }
      if (!take("^")) expect("return");
      const int terminal=trapped_return(0);
      expect(";");
      current_->result=binding<0?terminal:add(Kind::BindResult,0,{}, {binding,terminal});
    } else if (take("Option")) {
      expect("<");expect("int");expect(">");
      current_->returns_scalar_option=true;
      expect("{");
      if (!take("^")) expect("return");
      current_->result=option_return(0);
      expect(";");
    } else {
      current_->returns_bool=take("bool");
      if(!current_->returns_bool) expect("int");
      expect("{");
      current_->result=terminal_body(0);
    }
    expect("}");
  }
  int option_return(std::size_t d) {
    depth(d);
    if (take("Some")) {
      expect("("); const int v=expr(d+1); require_integer(v); expect(")");
      return add(Kind::Some,0,{}, {v});
    }
    if (take("None")) return add(Kind::None);
    if (take("if")) {
      const int pred=logical_or(d+1); require_boolean(pred);
      expect("then"); const int yes=option_return(d+1);
      expect("else"); const int no=option_return(d+1);
      expect("fi");
      return add(Kind::OptionIf,0,{}, {pred,yes,no});
    }
    const int v=factor(d+1);
    auto& node=current_->nodes.at(static_cast<std::size_t>(v));
    if(node.kind!=Kind::Call) fail("Option return requires Some, None or Option call");
    node.kind=Kind::OptionCall;
    return v;
  }
  // A match owns its subject and both lazy arms; the payload reference is
  // a non-owning IR index, independently checked for lexical dominance.
  int scalar_match(std::size_t d) {
    depth(d);
    expect("<");expect("int");expect(">");
    const bool explicit_result=token_.text=="trap" || token_.text=="trap!" || token_.text=="Ok";
    const int subject=explicit_result?trapped_return(d+1):option_return(d+1);
    expect("over");
    expect("on");
    const bool result=token_.text=="Ok" || token_.text=="Err";
    if(explicit_result && !result) fail("Result match requires Ok and Err arms");
    if(result && !explicit_result) {
      auto& node=current_->nodes.at(static_cast<std::size_t>(subject));
      if(node.kind!=Kind::OptionCall) fail("Result match requires Result subject");
      node.kind=Kind::ResultCall;
    }
    int some=-1,none=-1;
    for(int arm=0;arm<2;++arm) {
      if(arm!=0)expect("on");
      const bool is_some=take(result?"Ok":"Some");
      if(!is_some)expect(result?"Err":"None");
      if((is_some?some:none)>=0)fail(result?"duplicate Result match arm":"duplicate Option match arm");
      bool bound=false;
      if(is_some) {
        expect("(");
        if(token_.text!=")") {
          auto name=identifier();
          if(name=="Xor")fail("unsupported Option payload binding name");
          if(name!="_") {
            payload_bindings_.emplace_back(std::move(name),subject);
            bound=true;
          }
        }
        expect(")");
      } else if(result) {
        expect("(");
        if(token_.text!=")" && !take("_"))fail("Err binding requires nominal Error payload lowering");
        expect(")");
      }
      if(take(":"))expect("int");
      expect("->");
      const int value=expr(d+1);
      require_integer(value);
      if(bound)payload_bindings_.pop_back();
      if(is_some)some=value;else none=value;
      if(arm==0)expect(",");else take(",");
    }
    expect("end");
    if(some<0||none<0)fail("nonexhaustive Option match");
    return add(result?Kind::ResultMatch:Kind::OptionMatch,0,{}, {subject,some,none});
  }
  // Java parses trap as a unary operator. This terminal Result
  // admission accepts repeated trap aliases as one flat capture boundary.
  int trapped_return(std::size_t d) {
    depth(d);
    if (take("Ok")) {
      expect("("); const int v=expr(d+1); require_integer(v); expect(")");
      return add(Kind::Ok,0,{}, {v});
    }
    if (token_.text=="Err") fail("Err requires typed Error payload lowering");
    if (!take("trap") && !take("trap!")) {
      const int value=factor(d+1);
      auto& n=current_->nodes.at(static_cast<std::size_t>(value));
      if(n.kind!=Kind::Call) fail("scalar Result entry requires explicit trap or Result call");
      n.kind=Kind::ResultCall;
      return value;
    }
    while (token_.text=="trap" || token_.text=="trap!") {
      depth(++d); next();
    }
    const int operand=token_.text=="Ok"?trapped_return(d+1):factor(d+1);
    require_integer(operand);
    return add(Kind::Trap,0,{}, {operand});
  }
  // This scalar subset has no local state, effects, or general statements.
  // Every admitted guard failure body ends in an explicit typed return;
  // nested guards must satisfy the same terminal proof on every path.
  // Reuse the verified lazy If tree: true continues, false returns early.
  int terminal_body(std::size_t d) {
    depth(d);
    // Restricted terminal scalar conditionals. Both explicit return
    // arms lower to the same checked lazy tree as value conditionals.
    const bool prefixed = take("do");
    if (prefixed || token_.text=="if") {
      expect("if");
      const int condition=logical_or(d+1);
      require_boolean(condition);
      bool braced=false;
      if (prefixed) expect("then");
      else if (take("{")) braced=true;
      else if (!take("do")) expect("then"); // historical library spelling
      const int yes=terminal_return(d+1);
      if (braced) expect("}");
      if (take("else")) {
        if (braced) expect("{");
        const int no=terminal_return(d+1);
        expect(braced?"}":"fi");
        return add(current_->returns_bool?Kind::BoolIf:Kind::If,0,{}, {condition,yes,no});
      }
      if (!braced) expect("fi");
      const int no=terminal_body(d+1);
      return add(current_->returns_bool?Kind::BoolIf:Kind::If,0,{}, {condition,yes,no});
    }
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
    if(token_.text!="return" && token_.text!="^" && token_.text!="guard")
      fail("guard_failure_requires_return: scalar guard failure must return from callable");
    const int failure=terminal_body(d+1);
    expect(braced?"}":"end");
    const int continuation=terminal_body(d+1);
    return add(current_->returns_bool?Kind::BoolIf:Kind::If,0,{}, {condition,continuation,failure});
  }
  int terminal_return(std::size_t d) {
    depth(d);
    if (!take("^")) expect("return");
    const int value=expr(d+1);
    if(current_->returns_bool) require_boolean(value);
    else require_integer(value);
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
      // Admit the canonical explicit fi. Preserve historical unclosed
      // ordinary value-if in this migration-compatible native-call lane.
      // Negated forms still require the terminator.
      if (negated) expect("fi");
      else take("fi");
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
    while (token_.text=="*" || token_.text=="/") {
      const bool multiply=token_.text=="*";
      next();
      const int right=factor(d+1);
      require_integer(left); require_integer(right);
      left=add(multiply?Kind::Mul:Kind::Div,0,{}, {left,right});
    }
    return left;
  }
  int factor(std::size_t d) {
    depth(d);
    if(token_.text=="spawn")
      fail("E_SPAWN_OPTIONS_UNSUPPORTED: actor spawn requires typed initialization, resource admission and runtime lowering");
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
    if(take("match"))return scalar_match(d+1);
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
      const auto binding=std::find_if(payload_bindings_.rbegin(),payload_bindings_.rend(),
          [&](const auto& entry){return entry.first==name;});
      if (!take("(")) {
        if(binding!=payload_bindings_.rend()) {
          const auto kind=current_->nodes.at(static_cast<std::size_t>(binding->second)).kind;
          const bool result=kind==Kind::Trap||kind==Kind::ResultCall||kind==Kind::Ok;
          return add(result?Kind::ResultPayload:Kind::OptionPayload,binding->second);
        }
        if(local_source_>=0 && name==local_name_)
          return add(Kind::Local,local_source_);
        auto& params=current_->parameters;
        auto found=std::find(params.begin(),params.end(),name);
        if(found==params.end())fail("unknown parameter");
        return add(Kind::Parameter,static_cast<std::int64_t>(found-params.begin()));
      }
      if(binding!=payload_bindings_.rend())fail("Option payload is not callable");
      if(local_source_>=0 && name==local_name_)
        fail("scalar Result local is not callable");
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
      const auto found=names_.find(name);
      const bool bool_target=found!=names_.end() &&
          module_.functions.at(static_cast<std::size_t>(found->second)).returns_bool;
      return add(bool_target?Kind::BoolCall:Kind::Call,0,name,std::move(args));
    }
    fail("expected callable expression");
  }
};

// Host reference only: distinguish the two admitted guest ordinary errors
// from depth, budget, verifier, or host allocation failures.
struct ArithmeticFailure final : std::runtime_error {
  ArithmeticStatus status;
  ArithmeticFailure(ArithmeticStatus s,const char* message)
    :std::runtime_error(message),status(s) {}
};
std::int64_t checked(Kind k,std::int64_t a,std::int64_t b) {
  std::int64_t out=0;
  switch(k) {
    case Kind::Add:
      if(__builtin_add_overflow(a,b,&out))throw ArithmeticFailure(ArithmeticStatus::Overflow,"i64 add overflow");
      return out;
    case Kind::Sub:
      if(__builtin_sub_overflow(a,b,&out))throw ArithmeticFailure(ArithmeticStatus::Overflow,"i64 sub overflow");
      return out;
    case Kind::Mul:
      if(__builtin_mul_overflow(a,b,&out))throw ArithmeticFailure(ArithmeticStatus::Overflow,"i64 mul overflow");
      return out;
    case Kind::IntegerXor: return a^b;
    case Kind::Div:
      if(b==0) throw ArithmeticFailure(ArithmeticStatus::DivisionByZero,"i64 division by zero");
      if(a==std::numeric_limits<std::int64_t>::min() && b==-1)
        throw ArithmeticFailure(ArithmeticStatus::Overflow,"i64 division overflow");
      return a/b;
    case Kind::Less: return a<b;
    case Kind::LessEqual: return a<=b;
    case Kind::Greater: return a>b;
    case Kind::GreaterEqual: return a>=b;
    case Kind::Equal: return a==b;
    case Kind::NotEqual: case Kind::BooleanXor: return a!=b;
    default: throw std::runtime_error("invalid binary callable opcode");
  }
}
struct ResultFailure { ArithmeticStatus status; };
struct EvalFrame {
  std::int64_t value=0;
  std::map<int,std::int64_t> payloads;
};
struct Evaluator {
  const Module& m;
  std::size_t remaining=kMaxEvalSteps;
  std::int64_t evaluate(std::size_t id,std::span<const std::int64_t> args,std::size_t depth) {
    if (depth>=kMaxStackDepth)throw std::runtime_error("call depth exceeded");
    const auto& f=m.functions.at(id);
    if(args.size()!=f.parameters.size())throw std::runtime_error("runtime argument count mismatch");
    EvalFrame local;
    return visit(f,f.result,args,depth,local);
  }
  ArithmeticResult evaluate_result(std::size_t id,std::span<const std::int64_t> args,std::size_t depth) {
    try { return {evaluate(id,args,depth),ArithmeticStatus::Ok}; }
    catch(const ResultFailure& f) { return {0,f.status}; }
  }
  ScalarOption evaluate_option(std::size_t id,std::span<const std::int64_t> args,std::size_t depth) {
    if(depth>=kMaxStackDepth)throw std::runtime_error("call depth exceeded");
    const auto& fn=m.functions.at(id);
    if(args.size()!=fn.parameters.size())throw std::runtime_error("runtime argument count mismatch");
    EvalFrame local;
    return option_visit(fn,fn.result,args,depth,local);
  }
  ScalarOption option_visit(const Function& fn,int id,std::span<const std::int64_t> args,
                            std::size_t depth,EvalFrame& local) {
    if(remaining--==0)throw std::runtime_error("callable execution budget exceeded");
    const auto& n=fn.nodes.at(static_cast<std::size_t>(id));
    if(n.kind==Kind::None)return {0,0};
    if(n.kind==Kind::Some)return {visit(fn,n.children[0],args,depth,local),1};
    if(n.kind==Kind::OptionIf) {
      const auto pred=visit(fn,n.children[0],args,depth,local);
      return option_visit(fn,n.children[pred?1u:2u],args,depth,local);
    }
    if(n.kind==Kind::OptionCall) {
      std::vector<std::int64_t> vals;
      for(int child:n.children) vals.push_back(visit(fn,child,args,depth,local));
      return evaluate_option(static_cast<std::size_t>(n.callee),vals,depth+1);
    }
    throw std::runtime_error("invalid scalar Option opcode");
  }
  std::int64_t visit(const Function& fn,int id,std::span<const std::int64_t> args,
                     std::size_t depth,EvalFrame& local) {
    if (remaining--==0)throw std::runtime_error("callable execution budget exceeded");
    const auto& n=fn.nodes.at(static_cast<std::size_t>(id));
    if(n.kind==Kind::Constant||n.kind==Kind::Boolean)return n.value;
    if(n.kind==Kind::Trap) {
      try { return visit(fn,n.children[0],args,depth,local); }
      catch(const ArithmeticFailure& f) { throw ResultFailure{f.status}; }
    }
    if(n.kind==Kind::Ok)return visit(fn,n.children[0],args,depth,local);
    if(n.kind==Kind::Local)return local.value;
    if(n.kind==Kind::OptionPayload||n.kind==Kind::ResultPayload)
      return local.payloads.at(static_cast<int>(n.value));
    if(n.kind==Kind::ResultMatch) {
      std::int64_t subject=0;
      bool error=false;
      // Only the subject's Result variant is caught; arm effects escape.
      try { subject=visit(fn,n.children[0],args,depth,local); }
      catch(const ResultFailure&) { error=true; }
      if(error)return visit(fn,n.children[2],args,depth,local);
      local.payloads.emplace(n.children[0],subject);
      const auto value=visit(fn,n.children[1],args,depth,local);
      local.payloads.erase(n.children[0]);
      return value;
    }
    if(n.kind==Kind::OptionMatch) {
      const auto subject=option_visit(fn,n.children[0],args,depth,local);
      if(subject.tag==0)return visit(fn,n.children[2],args,depth,local);
      local.payloads.emplace(n.children[0],subject.value);
      const auto value=visit(fn,n.children[1],args,depth,local);
      local.payloads.erase(n.children[0]);
      return value;
    }
    if(n.kind==Kind::BindResult) {
      local.value=visit(fn,n.children[0],args,depth,local);
      return visit(fn,n.children[1],args,depth,local);
    }
    if(n.kind==Kind::Not)return !visit(fn,n.children[0],args,depth,local);
    if(n.kind==Kind::And) {
      if(!visit(fn,n.children[0],args,depth,local)) return 0;
      return visit(fn,n.children[1],args,depth,local);
    }
    if(n.kind==Kind::Or) {
      if(visit(fn,n.children[0],args,depth,local)) return 1;
      return visit(fn,n.children[1],args,depth,local);
    }
    if(n.kind==Kind::Parameter)return args[static_cast<std::size_t>(n.value)];
    if(n.kind==Kind::If||n.kind==Kind::BoolIf||n.kind==Kind::Ternary) {
      const auto pred=visit(fn,n.children[0],args,depth,local);
      return visit(fn,n.children[pred ? 1u:2u],args,depth,local);
    }
    if(n.kind==Kind::Call || n.kind==Kind::ResultCall || n.kind==Kind::BoolCall) {
      std::vector<std::int64_t> vals;
      for (int arg:n.children) vals.push_back(visit(fn,arg,args,depth,local));
      if(n.kind==Kind::ResultCall) {
        const auto result=evaluate_result(static_cast<std::size_t>(n.callee),vals,depth+1);
        if(result.status!=ArithmeticStatus::Ok) throw ResultFailure{result.status};
        return result.value;
      }
      return evaluate(static_cast<std::size_t>(n.callee),vals,depth+1);
    }
    const auto a=visit(fn,n.children.at(0),args,depth,local);
    const auto b=visit(fn,n.children.at(1),args,depth,local);
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
  std::string effect_target=".Lores_return_effect";
  std::string variant_target=".Lores_return_variant";
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
    } else if(n.kind==Kind::Some||n.kind==Kind::Ok) {
      emit_node(fn,n.children[0]);
      out<<(n.kind==Kind::Some?"  movl $1, %edx\n":"  xorl %edx, %edx\n");
    } else if(n.kind==Kind::None) {
      out<<"  xorl %eax, %eax\n  xorl %edx, %edx\n";
    } else if(n.kind==Kind::OptionMatch) {
      const auto tag=label++;
      emit_node(fn,n.children[0]);
      out<<"  testq %rdx, %rdx\n  je .Lores_option_none_"<<tag<<"\n";
      emit_node(fn,n.children[1]);
      out<<"  jmp .Lores_option_end_"<<tag<<"\n"
         <<".Lores_option_none_"<<tag<<":\n";
      emit_node(fn,n.children[2]);
      out<<".Lores_option_end_"<<tag<<":\n";
    } else if(n.kind==Kind::ResultMatch) {
      const auto tag=label++;
      const auto old_target=variant_target;
      const auto failure=".Lores_result_err_"+std::to_string(tag);
      variant_target=failure;
      emit_node(fn,n.children[0]);
      variant_target=old_target;
      out<<"  testq %rdx, %rdx\n  jne "<<failure<<"\n";
      emit_node(fn,n.children[1]);
      out<<"  jmp .Lores_result_end_"<<tag<<"\n"
         <<failure<<":\n  xorl %r9d, %r9d\n";
      emit_node(fn,n.children[2]);
      out<<".Lores_result_end_"<<tag<<":\n";
    } else if(n.kind==Kind::Local||n.kind==Kind::OptionPayload||n.kind==Kind::ResultPayload) {
      out<<"  movq "<<value_slot(static_cast<int>(n.value))<<", %rax\n";
    } else if(n.kind==Kind::BindResult) {
      emit_node(fn,n.children[0]);
      emit_node(fn,n.children[1]);
    } else if(n.kind==Kind::Trap) {
      // Ordinary throws are captured here, not at every function return.
      // A ResultCall's Err follows the variant-return path independently.
      const auto old_target=effect_target;
      const auto tag=label++;
      const auto handler=".Lores_trap_effect_"+std::to_string(tag);
      effect_target=handler;
      emit_node(fn,n.children[0]);
      effect_target=old_target;
      out<<"  xorl %edx, %edx\n  jmp .Lores_trap_end_"<<tag<<"\n"
         <<handler<<":\n  movq %r9, %rdx\n  jmp "<<variant_target<<"\n"
         <<".Lores_trap_end_"<<tag<<":\n";
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
    } else if(n.kind==Kind::If||n.kind==Kind::BoolIf||n.kind==Kind::Ternary||n.kind==Kind::OptionIf) {
      const auto tag=label++;
      emit_node(fn,n.children[0]);
      out<<"  cmpq $0, %rax\n  je .Lores_else_"<<tag<<"\n";
      emit_node(fn,n.children[1]);
      out<<"  jmp .Lores_end_"<<tag<<"\n"
         <<".Lores_else_"<<tag<<":\n";
      emit_node(fn,n.children[2]);
      out<<".Lores_end_"<<tag<<":\n";
    } else if(n.kind==Kind::Call||n.kind==Kind::ResultCall||n.kind==Kind::OptionCall||n.kind==Kind::BoolCall) {
      for (int child:n.children)emit_node(fn,child);
      for (std::size_t i=0;i<n.children.size();++i)
        out<<"  movq "<<value_slot(n.children[i])<<", "<<argument_reg(i)<<"\n";
      out<<"  movq -40(%rbp), %r8\n"
         <<"  addq $1, %r8\n"
         <<"  call .Lores_fn_"<<n.callee<<"\n";
      out<<"  testq %r9, %r9\n  jne "<<effect_target<<"\n";
      if(n.kind==Kind::ResultCall)
        out<<"  testq %rdx, %rdx\n  jne "<<variant_target<<"\n";
    } else if(n.kind==Kind::Div) {
      emit_node(fn,n.children[0]);
      emit_node(fn,n.children[1]);
      const auto tag=label++;
      out<<"  movq "<<value_slot(n.children[1])<<", %r11\n"
         <<"  movl $"<<static_cast<std::uint64_t>(ArithmeticStatus::DivisionByZero)<<", %r9d\n"
         <<"  testq %r11, %r11\n  je "<<effect_target<<"\n"
         <<"  movq "<<value_slot(n.children[0])<<", %rax\n"
         <<"  cmpq $-1, %r11\n  jne .Lores_div_safe_"<<tag<<"\n"
         <<"  movabsq $-9223372036854775808, %r10\n"
         <<"  movl $"<<static_cast<std::uint64_t>(ArithmeticStatus::Overflow)<<", %r9d\n"
         <<"  cmpq %r10, %rax\n  je "<<effect_target<<"\n"
         <<".Lores_div_safe_"<<tag<<":\n  cqto\n  idivq %r11\n";
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
        out<<"  movl $"<<static_cast<std::uint64_t>(ArithmeticStatus::Overflow)<<", %r9d\n"
           <<"  jo "<<effect_target<<"\n";
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
    // Keep the historic scalar entry's terminal fault behavior, and expose
    // an explicit value/status entry for the new checked ABI. Internal calls
    // always use the checked convention and propagate arithmetic failures.
    const bool result_entry=program.functions.at(static_cast<std::size_t>(program.entry)).returns_scalar_result;
    const bool option_entry=program.functions.at(static_cast<std::size_t>(program.entry)).returns_scalar_option;
    const char* entry_symbol=option_entry?"oresforge_entry_option":
        (result_entry?"oresforge_entry_result":"oresforge_entry_checked");
    out<<".text\n";
    if(!result_entry && !option_entry) out<<".globl oresforge_entry\n.type oresforge_entry, @function\n"
       <<"oresforge_entry:\n  subq $8, %rsp\n  call oresforge_entry_checked\n"
       <<"  addq $8, %rsp\n  testq %rdx, %rdx\n  jne .Lores_legacy_fault\n  ret\n"
       <<".size oresforge_entry, .-oresforge_entry\n";
    out<<".globl "<<entry_symbol<<"\n.type "<<entry_symbol<<", @function\n"
       <<entry_symbol<<":\n  subq $8, %rsp\n  xorl %r8d, %r8d\n"
       <<"  call .Lores_fn_"<<program.entry<<"\n  addq $8, %rsp\n"
       <<"  testq %r9, %r9\n";
    if(result_entry||option_entry) out<<"  jne .Lores_legacy_fault\n";
    else out<<"  je .Lores_scalar_entry_ok\n  movq %r9, %rdx\n.Lores_scalar_entry_ok:\n";
    out<<"  ret\n.size "<<entry_symbol<<", .-"<<entry_symbol<<"\n";
    // SysV C inspection entry: void entry_effect(out*, arg0..arg3).
    // Shift incoming arguments left, preserve out pointer across the call.
    out<<".globl oresforge_entry_effect\n.type oresforge_entry_effect, @function\n"
       <<"oresforge_entry_effect:\n  pushq %rdi\n"
       <<"  movq %rsi, %rdi\n  movq %rdx, %rsi\n"
       <<"  movq %rcx, %rdx\n  movq %r8, %rcx\n  xorl %r8d, %r8d\n"
       <<"  call .Lores_fn_"<<program.entry<<"\n  popq %r10\n"
       <<"  movq %rax, 0(%r10)\n  movq %rdx, 8(%r10)\n  movq %r9, 16(%r10)\n  ret\n"
       <<".size oresforge_entry_effect, .-oresforge_entry_effect\n";
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
      if(!fn.returns_scalar_option) out<<"  xorl %edx, %edx\n";
      out<<"  xorl %r9d, %r9d\n  leave\n  ret\n";
    }
    out<<".Lores_depth:\n  ud2\n"
       <<".Lores_legacy_fault:\n  ud2\n"
       <<".Lores_return_effect:\n  xorl %eax, %eax\n  xorl %edx, %edx\n  leave\n  ret\n"
       <<".Lores_return_variant:\n  xorl %eax, %eax\n  xorl %r9d, %r9d\n  leave\n  ret\n"
       <<".section .note.GNU-stack,\"\",@progbits\n";
    return out.str();
  }
};
}
Module parse(std::string_view source) {
  if (source.size()>kMaxSource)throw std::runtime_error("callable source budget exceeded");
  oresforge::source::validate_utf8(source);
  oresforge::source::reject_unlowered_topic_publish(source);
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
    const auto root_kind=fn.nodes[static_cast<std::size_t>(fn.result)].kind;
    const auto option_kind=[](Kind k) {
      return k==Kind::Some||k==Kind::None||k==Kind::OptionIf||k==Kind::OptionCall;
    };
    const auto result_kind=[](Kind k) {
      return k==Kind::Trap||k==Kind::ResultCall||k==Kind::Ok;
    };
    if(static_cast<int>(fn.returns_scalar_result)+static_cast<int>(fn.returns_scalar_option)+
       static_cast<int>(fn.returns_bool)>1)
      throw std::runtime_error("conflicting scalar return types");
    if (fn.returns_scalar_result) {
      if(root_kind!=Kind::Trap && root_kind!=Kind::ResultCall && root_kind!=Kind::BindResult && root_kind!=Kind::Ok)
        throw std::runtime_error("invalid scalar Result return");
    } else if(fn.returns_scalar_option) {
      if(!option_kind(root_kind)) throw std::runtime_error("invalid scalar Option return");
    } else if(fn.returns_bool) {
      if(!boolean_kind(root_kind)) throw std::runtime_error("callable return type mismatch");
      if(fi==static_cast<std::size_t>(m.entry))
        throw std::runtime_error("Boolean entry requires explicit int adapter in this profile");
    } else if (root_kind==Kind::Trap || root_kind==Kind::BindResult || root_kind==Kind::ResultCall || root_kind==Kind::Ok || option_kind(root_kind) || boolean_kind(root_kind))
      throw std::runtime_error("callable int return cannot be Boolean or Result");
    // This stage-0 AST must be an owned tree, not an arbitrary dataflow DAG.
    // Otherwise a node on an untaken if-branch could be reused without ever
    // initializing its stack slot. Enforce one reachable parent per child.
    std::vector<unsigned> incoming(fn.nodes.size(),0);
    std::vector<int> parent(fn.nodes.size(),-1);
    for(std::size_t i=0;i<fn.nodes.size();++i) {
      const auto& n=fn.nodes[i];
      switch(n.kind) {
        case Kind::Constant: case Kind::Parameter: case Kind::Add:
        case Kind::Sub: case Kind::Mul: case Kind::Less: case Kind::LessEqual:
        case Kind::Greater: case Kind::GreaterEqual: case Kind::Equal:
        case Kind::NotEqual: case Kind::If: case Kind::BoolIf: case Kind::Call:
        case Kind::Boolean: case Kind::Not: case Kind::Ternary:
         case Kind::And: case Kind::Or: case Kind::BooleanXor: case Kind::Div: case Kind::Trap: case Kind::ResultCall: case Kind::BindResult: case Kind::Local: case Kind::Ok: case Kind::Some: case Kind::None: case Kind::OptionIf: case Kind::OptionCall: case Kind::BoolCall: case Kind::IntegerXor: case Kind::OptionMatch: case Kind::OptionPayload: case Kind::ResultMatch: case Kind::ResultPayload: break;
        default: throw std::runtime_error("unknown callable opcode");
      }
      if(n.kind!=Kind::Call && n.kind!=Kind::ResultCall && n.kind!=Kind::OptionCall && n.kind!=Kind::BoolCall && !n.name.empty())
        throw std::runtime_error("unexpected callable symbol payload");
      for(int arg:n.children) {
        if(arg<0 || static_cast<std::size_t>(arg)>=i)
          throw std::runtime_error("callable use before definition");
        parent[static_cast<std::size_t>(arg)]=static_cast<int>(i);
        if(++incoming[static_cast<std::size_t>(arg)]!=1)
          throw std::runtime_error("callable AST shared node");
      }
      std::size_t size=2;
      if(n.kind==Kind::Constant||n.kind==Kind::Boolean||n.kind==Kind::Parameter||n.kind==Kind::Local||n.kind==Kind::None||n.kind==Kind::OptionPayload||n.kind==Kind::ResultPayload)size=0;
      else if(n.kind==Kind::Not||n.kind==Kind::Trap||n.kind==Kind::Some||n.kind==Kind::Ok)size=1;
      else if(n.kind==Kind::If||n.kind==Kind::BoolIf||n.kind==Kind::Ternary||n.kind==Kind::OptionIf||n.kind==Kind::OptionMatch||n.kind==Kind::ResultMatch)size=3;
      else if(n.kind==Kind::Call||n.kind==Kind::ResultCall||n.kind==Kind::OptionCall||n.kind==Kind::BoolCall)size=n.children.size();
      if(n.children.size()!=size)throw std::runtime_error("callable operand arity mismatch");
      if(n.kind==Kind::Boolean && n.value!=0 && n.value!=1)
        throw std::runtime_error("invalid Boolean literal");
      const auto child_bool=[&](std::size_t pos) {
        return boolean_kind(fn.nodes.at(static_cast<std::size_t>(n.children.at(pos))).kind);
      };
      if(n.kind==Kind::Some||n.kind==Kind::Ok) {
        const auto k=fn.nodes.at(static_cast<std::size_t>(n.children[0])).kind;
        if(boolean_kind(k)||option_kind(k)||k==Kind::Trap||k==Kind::ResultCall||
           k==Kind::BindResult||k==Kind::Ok)
          throw std::runtime_error("constructor requires int payload");
      }
      if(n.kind==Kind::OptionMatch) {
        const auto subject_kind=fn.nodes.at(static_cast<std::size_t>(n.children[0])).kind;
        if(!option_kind(subject_kind)||child_bool(1)||child_bool(2))
          throw std::runtime_error("Option match requires Option subject and int arms");
        for(std::size_t j=1;j<3;++j) {
          const auto arm=fn.nodes.at(static_cast<std::size_t>(n.children[j])).kind;
          if(option_kind(arm)||arm==Kind::Trap||arm==Kind::ResultCall||
             arm==Kind::BindResult||arm==Kind::Ok)
            throw std::runtime_error("Option match requires int arms");
        }
      }
      if(n.kind==Kind::ResultMatch) {
        if(!result_kind(fn.nodes.at(static_cast<std::size_t>(n.children[0])).kind))
          throw std::runtime_error("Result match requires Result subject");
        for(std::size_t j=1;j<3;++j) {
          const auto arm=fn.nodes.at(static_cast<std::size_t>(n.children[j])).kind;
          if(child_bool(j)||option_kind(arm)||result_kind(arm)||arm==Kind::BindResult)
            throw std::runtime_error("Result match requires int arms");
        }
      }
      if(n.kind==Kind::OptionIf) {
        if(!child_bool(0)||!option_kind(fn.nodes.at(static_cast<std::size_t>(n.children[1])).kind)||
           !option_kind(fn.nodes.at(static_cast<std::size_t>(n.children[2])).kind))
          throw std::runtime_error("Option conditional requires bool and Option arms");
      }
      if(n.kind==Kind::Trap && child_bool(0))
        throw std::runtime_error("scalar trap must be an int or Result operand");
      if(n.kind==Kind::BindResult) {
        if(!fn.returns_scalar_result || i!=static_cast<std::size_t>(fn.result) ||
           fn.nodes.at(static_cast<std::size_t>(n.children[0])).kind!=Kind::ResultCall ||
           (fn.nodes.at(static_cast<std::size_t>(n.children[1])).kind!=Kind::Trap &&
            fn.nodes.at(static_cast<std::size_t>(n.children[1])).kind!=Kind::ResultCall &&
            fn.nodes.at(static_cast<std::size_t>(n.children[1])).kind!=Kind::Ok))
          throw std::runtime_error("invalid scalar Result binding");
      }
      if(n.kind==Kind::Not && !child_bool(0))
        throw std::runtime_error("condition_not_bool: negation requires bool");
      if(n.kind==Kind::If||n.kind==Kind::BoolIf||n.kind==Kind::Ternary) {
        if(!child_bool(0)) throw std::runtime_error("condition_not_bool: conditional requires bool");
        if(n.kind==Kind::BoolIf) {
          if(!child_bool(1)||!child_bool(2)) throw std::runtime_error("Boolean conditional requires bool arms");
        } else if(child_bool(1)||child_bool(2)) throw std::runtime_error("conditional branch is not int");
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
      } else if(n.kind==Kind::IntegerXor||n.kind==Kind::Add||n.kind==Kind::Sub||n.kind==Kind::Mul||n.kind==Kind::Div||
                n.kind==Kind::Less||n.kind==Kind::LessEqual||n.kind==Kind::Greater||
                n.kind==Kind::GreaterEqual) {
        if(child_bool(0)||child_bool(1)) throw std::runtime_error("Boolean is not an int");
      } else if(n.kind==Kind::Call||n.kind==Kind::ResultCall||n.kind==Kind::OptionCall||n.kind==Kind::BoolCall) {
        for(std::size_t j=0;j<n.children.size();++j)
          if(child_bool(j)) throw std::runtime_error("Boolean is not an int argument");
      }
      if(n.kind==Kind::Parameter && (n.value<0 ||
         static_cast<std::uint64_t>(n.value)>=fn.parameters.size()))
        throw std::runtime_error("invalid callable parameter");
      if(n.kind==Kind::Call||n.kind==Kind::ResultCall||n.kind==Kind::OptionCall||n.kind==Kind::BoolCall) {
        if(n.callee<0 || static_cast<std::size_t>(n.callee)>=m.functions.size())
          throw std::runtime_error("invalid callable target");
        const auto& target=m.functions[static_cast<std::size_t>(n.callee)];
        if(target.returns_scalar_result && n.callee==m.entry)
          throw std::runtime_error("calls to scalar Result entry are unsupported");
        if(target.returns_scalar_result!=(n.kind==Kind::ResultCall))
          throw std::runtime_error("scalar Result call type mismatch");
        if(target.returns_scalar_option!=(n.kind==Kind::OptionCall))
          throw std::runtime_error("scalar Option call type mismatch");
        if(target.returns_bool!=(n.kind==Kind::BoolCall))
          throw std::runtime_error("callable Boolean target type mismatch; declare bool helpers before use");
        if(n.kind==Kind::OptionCall && n.callee==m.entry)
          throw std::runtime_error("calls to scalar Option entry are unsupported");
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
      const auto& n=fn.nodes[i];
      const auto parent_id=parent[i];
      const auto* p=parent_id<0?nullptr:&fn.nodes[static_cast<std::size_t>(parent_id)];
      if(option_kind(n.kind)) {
        const bool permitted=p ? ((p->kind==Kind::OptionIf && p->children[0]!=static_cast<int>(i)) ||
                                  (p->kind==Kind::OptionMatch && p->children[0]==static_cast<int>(i)))
                               : fn.returns_scalar_option;
        if(!permitted)throw std::runtime_error("scalar Option value requires typed terminal return or match subject");
      }
      if(n.kind==Kind::OptionPayload||n.kind==Kind::ResultPayload) {
        const bool result=n.kind==Kind::ResultPayload;
        if(n.value<0 || static_cast<std::uint64_t>(n.value)>=i ||
           !(result?result_kind(fn.nodes[static_cast<std::size_t>(n.value)].kind):
                    option_kind(fn.nodes[static_cast<std::size_t>(n.value)].kind)))
          throw std::runtime_error(result?"invalid Result payload subject":"invalid Option payload subject");
        bool dominated=false;
        int current=static_cast<int>(i);
        for(int owner_id=parent[i];owner_id>=0;owner_id=parent[static_cast<std::size_t>(owner_id)]) {
          const auto& owner=fn.nodes[static_cast<std::size_t>(owner_id)];
          if(owner.kind==(result?Kind::ResultMatch:Kind::OptionMatch) && owner.children[0]==n.value &&
             owner.children[1]==current) {dominated=true;break;}
          current=owner_id;
        }
        if(!dominated)throw std::runtime_error(result?"Result payload outside Ok arm":"Option payload outside Some arm");
      }
      if(result_kind(n.kind)) {
        const bool match_subject=p&&p->kind==Kind::ResultMatch&&p->children[0]==static_cast<int>(i);
        const bool terminal=!p&&fn.returns_scalar_result;
        const bool bound_terminal=p&&p->kind==Kind::BindResult&&p->children[1]==static_cast<int>(i);
        const bool call_operand=p&&((n.kind==Kind::ResultCall&&
            (p->kind==Kind::Trap||p->kind==Kind::BindResult))||
            (n.kind==Kind::Ok&&p->kind==Kind::Trap));
        if(!(match_subject||terminal||bound_terminal||call_operand)) {
          if(n.kind==Kind::Ok)throw std::runtime_error("Ok requires terminal Result return");
          if(n.kind==Kind::Trap)throw std::runtime_error("scalar trap must be a terminal Result boundary");
          throw std::runtime_error("scalar Result value requires return or postfix propagation");
        }
      }
      if(n.kind==Kind::Local) {
        const auto& root=fn.nodes[static_cast<std::size_t>(fn.result)];
        if(root.kind!=Kind::BindResult || n.value!=root.children[0] ||
           n.value<0 || static_cast<std::size_t>(n.value)>=i)
          throw std::runtime_error("invalid scalar Result local use");
        int current=static_cast<int>(i);
        while(parent[static_cast<std::size_t>(current)]>=0 &&
              parent[static_cast<std::size_t>(current)]!=fn.result)
          current=parent[static_cast<std::size_t>(current)];
        if(current!=root.children[1])
          throw std::runtime_error("invalid scalar Result local use");
      }
    }
  }
  if(m.functions[static_cast<std::size_t>(m.entry)].name!="main")
    throw std::runtime_error("callable entry is not main");
}
std::int64_t interpret(const Module& m,std::span<const std::int64_t> args) {
  verify(m);
  if(m.functions.at(static_cast<std::size_t>(m.entry)).returns_scalar_result)
    throw std::runtime_error("scalar Result requires run-result-calls");
  if(m.functions.at(static_cast<std::size_t>(m.entry)).returns_scalar_option)
    throw std::runtime_error("scalar Option requires run-option-calls");
  Evaluator eval{m};
  return eval.evaluate(static_cast<std::size_t>(m.entry),args,0);
}
ArithmeticResult interpret_result(const Module& m,std::span<const std::int64_t> args) {
  verify(m);
  if(!m.functions.at(static_cast<std::size_t>(m.entry)).returns_scalar_result)
    throw std::runtime_error("run-result-calls requires Result<int,Error> entry");
  Evaluator eval{m};
  return eval.evaluate_result(static_cast<std::size_t>(m.entry),args,0);
}
ScalarOption interpret_option(const Module& m,std::span<const std::int64_t> args) {
  verify(m);
  if(!m.functions.at(static_cast<std::size_t>(m.entry)).returns_scalar_option)
    throw std::runtime_error("run-option-calls requires Option<int> entry");
  Evaluator eval{m};
  return eval.evaluate_option(static_cast<std::size_t>(m.entry),args,0);
}
ScalarOutcome interpret_effect(const Module& m,std::span<const std::int64_t> args) {
  verify(m);
  Evaluator eval{m};
  const auto entry=static_cast<std::size_t>(m.entry);
  try {
    const auto& fn=m.functions.at(entry);
    if(fn.returns_scalar_result) {
      const auto r=eval.evaluate_result(entry,args,0);
      return {r.value,static_cast<std::uint64_t>(r.status),ArithmeticStatus::Ok};
    }
    if(fn.returns_scalar_option) {
      const auto r=eval.evaluate_option(entry,args,0);
      return {r.value,r.tag,ArithmeticStatus::Ok};
    }
    return {eval.evaluate(entry,args,0),0,ArithmeticStatus::Ok};
  } catch(const ArithmeticFailure& failure) {
    return {0,0,failure.status};
  }
}
std::string emit_asm(const Module& m,Arch arch) {
  if(arch!=Arch::X86_64)
    throw std::runtime_error("native callable backend currently supports x86_64 only");
  verify(m);
  return Native{m}.build();
}
}
