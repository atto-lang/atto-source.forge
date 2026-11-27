#include "compiler/nominal_scalar.hpp"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <algorithm>
#include <vector>

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
    if (!take("let")) return parse_fieldless();
    expect("int");
    result_.field = identifier();
    expect("=");
    result_.initializer = integer();
    expect(";");
    bool saw_writer = false, saw_reader = false;
    while (token_.text != "end") {
      if (token_.text.empty()) fail("unterminated nominal class");
      take("pub");
      std::string name = identifier();
      // Explicit instance markers share the same method slot as bare names.
      // mtd() and method() are contextual identifiers, not introducers.
      if (name == "fnc" ||
          (name == "mtd" && token_.text != "("))
        name = identifier();
      if (name == "fnc") fail("reserved nominal method name");
      if (name == "method" && token_.text != "(")
        fail("obsolete method introducer; use mtd");
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
        if (result_.methods.size() >= 16) fail("nominal method budget exceeded");
        if (parameter == "self" || parameter == result_.field)
          fail("invalid or shadowed mutable receiver parameter");
        expect(":");
        expect("void");
        expect("{");
        expect("self");expect(".");expect(result_.field);
        expect("=");expect("self");expect(".");expect(result_.field);
        const bool subtract = take("-");
        if (!subtract) expect("+");
        expect(parameter);expect(";");
        if (take("return")) expect(";");
        expect("}");
        declare_method(name, subtract ? Method::Kind::Subtract : Method::Kind::Add, 1);
        if (!saw_writer) result_.writer = name;
        saw_writer = true;
      } else {
        // Unmarked zero-value methods have an implicit shared self receiver.
        if (token_.text == "self") next();
        expect(")");
        if (result_.methods.size() >= 16) fail("nominal method budget exceeded");
        if (!take(":")) expect("->");
        expect("int");
        expect("{");
        expect("return");expect("self");expect(".");
        expect(result_.field);expect(";");
        expect("}");
        declare_method(name, Method::Kind::Read, 0);
        if (!saw_reader) result_.reader = name;
        saw_reader = true;
      }
    }
    expect("end");
    if (!saw_writer || !saw_reader)
      fail("bounded class requires one mutable writer and one reader");

    take("pub");
    take("define");
    take("pub");
    expect("fnc");expect("main");
    expect("(");expect(")");
    if (!take(":")) expect("->");
    expect("int");expect("{");
    bool returned = false;
    while (token_.text != "}") {
      if (token_.text.empty()) fail("unterminated nominal main");
      if (take("let")) {
        expect("mut");expect(result_.owner);
        const std::string instance = identifier();
        if (instance == "self" ||
            std::find(result_.instances.begin(), result_.instances.end(), instance)
                != result_.instances.end()) fail("duplicate or invalid class binding");
        if (result_.instances.size() >= 8) fail("nominal instance budget exceeded");
        expect("=");expect("new");expect(result_.owner);
        expect("(");expect(")");expect(";");
        const std::size_t id = result_.instances.size();
        result_.instances.push_back(instance);
        add_step({Step::Kind::Construct,id,0,0});
        if (result_.binding.empty()) result_.binding = instance;
      } else if (take("return")) {
        const auto instance = find_instance(identifier());
        expect(".");
        const auto method = find_method(identifier(),0);
        if (result_.methods[method].kind != Method::Kind::Read)
          fail("return requires shared receiver read method");
        expect("(");expect(")");expect(";");
        add_step({Step::Kind::ReturnRead,instance,method,0});
        returned = true;
        break;
      } else {
        const auto instance = find_instance(identifier());
        expect(".");
        const auto method = find_method(identifier(),1);
        if (result_.methods[method].kind == Method::Kind::Read)
          fail("mutation requires exclusive mutable receiver");
        expect("(");const auto value = integer();
        expect(")");expect(";");
        add_step({Step::Kind::Mutate,instance,method,value});
        if (result_.steps.size() == 2) result_.argument = value;
      }
    }
    if (!returned) fail("nominal main must return a shared receiver read");
    expect("}");
    if (!token_.text.empty()) fail("unsupported trailing nominal source");
    return result_;
  }
 private:
  // Closed fieldless profile: a public, statically selected i64 method on
  // a fresh receiver. No fields, mutable self, escaping receiver or dispatch table.
  ScalarClass parse_fieldless() {
    while (!take("end")) {
      if (result_.methods.size() >= 16) fail("nominal method budget exceeded");
      const bool pub = take("pub");
      std::string name = identifier();
      if (name == "method" && token_.text != "(")
        fail("obsolete method introducer; use mtd");
      if (name == "fnc" || (name == "mtd" && token_.text != "("))
        name = identifier();
      if (name == "fnc" || name == "self" || name == "static" || name == "mut")
        fail("reserved nominal method name");
      expect("(");
      bool closed = false;
      if (take("self")) {
        if (take(")")) {
          if (!take("(")) closed = true;
        } else {
          expect(",");
          if (token_.text == ")") fail("trailing receiver comma");
        }
      }
      std::string parameter;
      if (!closed && !take(")")) {
        expect("int"); parameter = identifier();
        if (parameter == "self") fail("invalid shared receiver parameter");
        expect(")");
      }
      if (!take(":")) expect("->");
      expect("int"); expect("{"); expect("return");
      const auto kind = parameter.empty() ? Method::Kind::Constant : Method::Kind::ArgumentAdd;
      if (!parameter.empty()) { expect(parameter); expect("+"); }
      const auto immediate = integer();
      expect(";"); expect("}");
      declare_method(name, kind, parameter.empty() ? 0u : 1u);
      result_.methods.back().immediate = immediate;
      result_.methods.back().public_method = pub;
    }
    take("pub"); expect("fnc"); expect("main"); expect("("); expect(")");
    if (!take(":")) expect("->");
    expect("int"); expect("{"); expect("return"); expect("new");
    expect(result_.owner); expect("("); expect(")"); expect(".");
    const auto name = identifier(); expect("(");
    std::size_t arity = 0;
    std::int64_t argument = 0;
    if (!take(")")) { argument = integer(); arity = 1; expect(")"); }
    const auto method = find_method(name, arity);
    if (!result_.methods[method].public_method) fail("private fieldless method");
    expect(";"); expect("}");
    if (!token_.text.empty()) fail("unsupported trailing nominal source");
    result_.instances.push_back("temporary_receiver");
    add_step({Step::Kind::Construct,0,0,0});
    add_step({Step::Kind::ReturnRead,0,method,argument});
    return result_;
  }
  void declare_method(const std::string& name, Method::Kind kind,
                      std::size_t value_arity) {
    for (const auto& prior : result_.methods)
      if (prior.name == name && prior.value_arity == value_arity)
        fail("duplicate native method name/arity");
    result_.methods.push_back({name,kind,value_arity});
  }
  std::size_t find_instance(const std::string& name) const {
    const auto found = std::find(result_.instances.begin(),result_.instances.end(),name);
    if (found == result_.instances.end()) fail("unknown or uninitialized class receiver");
    return static_cast<std::size_t>(found-result_.instances.begin());
  }
  std::size_t find_method(const std::string& name,std::size_t arity) const {
    for(std::size_t i=0;i<result_.methods.size();++i)
      if(result_.methods[i].name==name && result_.methods[i].value_arity==arity)
        return i;
    fail("unknown nominal method or caller-visible arity");
  }
  void add_step(Step step) {
    if (result_.steps.size() >= 32) fail("nominal operation budget exceeded");
    result_.steps.push_back(step);
  }
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
  const bool fieldless = p.field.empty();
  if (!valid_identifier(p.owner) || (!fieldless && !valid_identifier(p.field)) ||
      p.methods.empty() || p.methods.size()>16 ||
      p.instances.empty() || p.instances.size()>8 ||
      p.steps.empty() || p.steps.size()>32 ||
      p.steps.back().kind != Step::Kind::ReturnRead)
    throw std::runtime_error("invalid nominal scalar IR");
  std::vector<bool> constructed(p.instances.size(),false);
  for (const auto& method:p.methods) {
    const bool recognized = fieldless
      ? method.kind == Method::Kind::Constant || method.kind == Method::Kind::ArgumentAdd
      : method.kind == Method::Kind::Add || method.kind == Method::Kind::Subtract || method.kind == Method::Kind::Read;
    if (!recognized || !valid_identifier(method.name) ||
        method.value_arity != ((method.kind == Method::Kind::Read || method.kind == Method::Kind::Constant) ? 0u : 1u))
      throw std::runtime_error("invalid nominal method IR");
  }
  for (std::size_t i=0;i<p.methods.size();++i)
    for (std::size_t j=0;j<i;++j)
      if(p.methods[i].name==p.methods[j].name &&
         p.methods[i].value_arity==p.methods[j].value_arity)
        throw std::runtime_error("duplicate nominal method IR slot");
  for (const auto& name:p.instances)
    if(!valid_identifier(name)) throw std::runtime_error("invalid nominal binding");
  for (std::size_t i=0;i<p.instances.size();++i)
    for (std::size_t j=0;j<i;++j)
      if(p.instances[i]==p.instances[j]) throw std::runtime_error("duplicate nominal binding");
  if (fieldless && (p.instances.size()!=1 || p.steps.size()!=2 ||
      p.steps.front().kind!=Step::Kind::Construct))
    throw std::runtime_error("invalid fieldless nominal shape");
  for (std::size_t i=0;i<p.steps.size();++i) {
    const auto& step=p.steps[i];
    if(step.instance>=p.instances.size()) throw std::runtime_error("invalid nominal instance slot");
    if(step.kind==Step::Kind::Construct) {
      if(step.method!=0 || step.argument!=0)
        throw std::runtime_error("invalid nominal construction operands");
      if(constructed[step.instance]) throw std::runtime_error("duplicate nominal construction");
      constructed[step.instance]=true;
    } else {
      if(step.kind!=Step::Kind::Mutate && step.kind!=Step::Kind::ReturnRead)
        throw std::runtime_error("invalid nominal operation kind");
      if(!constructed[step.instance] || step.method>=p.methods.size())
        throw std::runtime_error("invalid nominal receiver or method reference");
      const auto kind=p.methods[step.method].kind;
      if(step.kind==Step::Kind::Mutate && kind==Method::Kind::Read)
        throw std::runtime_error("mutable operation targets read method");
      if(step.kind==Step::Kind::ReturnRead &&
         (i!=p.steps.size()-1 || (fieldless
          ? !p.methods[step.method].public_method || (kind==Method::Kind::Constant && step.argument!=0)
          : kind!=Method::Kind::Read || step.argument!=0)))
        throw std::runtime_error("invalid nominal return method");
    }
  }
}
} // namespace

ScalarClass parse(std::string_view source) {return Parser(source).parse();}

std::int64_t interpret(const ScalarClass& p) {
  validate(p);
  if (p.field.empty()) {
    const auto& step=p.steps.back();
    const auto& method=p.methods[step.method];
    if (method.kind==Method::Kind::Constant) return method.immediate;
    std::int64_t result=0;
    if (__builtin_add_overflow(step.argument,method.immediate,&result))
      throw std::runtime_error("i64 add overflow");
    return result;
  }
  std::vector<std::int64_t> values(p.instances.size());
  for(const auto& step:p.steps) {
    if(step.kind == Step::Kind::Construct) {
      values[step.instance] = p.initializer;
    } else if(step.kind == Step::Kind::ReturnRead) {
      return values[step.instance];
    } else {
      std::int64_t result=0;
      const auto kind=p.methods[step.method].kind;
      const bool overflow=kind==Method::Kind::Add
        ? __builtin_add_overflow(values[step.instance],step.argument,&result)
        : __builtin_sub_overflow(values[step.instance],step.argument,&result);
      if(overflow) throw std::runtime_error(kind==Method::Kind::Add
        ? "i64 add overflow" : "i64 sub overflow");
      values[step.instance]=result;
    }
  }
  throw std::runtime_error("nominal source missing return");
}

std::string emit_x86_64_asm(const ScalarClass& p) {
  validate(p);
  if (p.field.empty()) {
    const auto& step=p.steps.back();
    const auto& method=p.methods[step.method];
    std::string out=".text\n.globl oresforge_entry\n.type oresforge_entry, @function\noresforge_entry:\n";
    out+="  movabsq $"+std::to_string(method.kind==Method::Kind::Constant ? method.immediate : step.argument)+", %rax\n";
    if (method.kind==Method::Kind::ArgumentAdd)
      out+="  movabsq $"+std::to_string(method.immediate)+", %rdx\n  addq %rdx, %rax\n  jo .Loresforge_nominal_overflow\n";
    out+="  ret\n.Loresforge_nominal_overflow:\n  ud2\n.size oresforge_entry, .-oresforge_entry\n";
    return out;
  }
  const auto bytes=((p.instances.size()*8u+15u)/16u)*16u;
  std::string out=".text\n.globl oresforge_entry\n.type oresforge_entry, @function\n"
                  "oresforge_entry:\n  subq $"+std::to_string(bytes)+", %rsp\n";
  for (const auto& step:p.steps) {
    const auto slot=step.instance==0 ? std::string("(%rsp)")
      : std::to_string(step.instance*8u)+"(%rsp)";
    if(step.kind==Step::Kind::Construct) {
      out+="  movabsq $"+std::to_string(p.initializer)+", %rax\n"
           "  movq %rax, "+slot+"\n";
    } else if(step.kind==Step::Kind::Mutate) {
      out+="  movabsq $"+std::to_string(step.argument)+", %rdx\n"
           "  movq "+slot+", %rax\n";
      out+= p.methods[step.method].kind==Method::Kind::Subtract
        ? "  subq %rdx, %rax\n" : "  addq %rdx, %rax\n";
      out+="  jo .Loresforge_nominal_overflow\n"
           "  movq %rax, "+slot+"\n";
    } else {
      out+="  movq "+slot+", %rax\n"
           "  addq $"+std::to_string(bytes)+", %rsp\n  ret\n";
    }
  }
  out+=".Loresforge_nominal_overflow:\n  ud2\n"
       ".size oresforge_entry, .-oresforge_entry\n";
  return out;
}
} // namespace oresforge::nominal
