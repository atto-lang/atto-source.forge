#pragma once
// Closed readonly class source -> real native pointer ABI. No externally
// constructible proof, actor capability, escaping reference or heap runtime.
#include "compiler/source_utf8.hpp"
#include "codegen/native.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oresforge::readonly_native {
inline constexpr std::string_view marker="// atto-ownership: borrow-default-v1";
inline bool selected(std::string_view s) {
  auto first=s.substr(0,s.find('\n'));
  if (!first.empty() && first.back()=='\r') first.remove_suffix(1);
  return first==marker;
}
class Parser {
  struct Helper { std::string name; bool forward; std::size_t index; };
  std::vector<std::string> tokens_;
  std::size_t at_=0;
  std::vector<std::string> fields_;
  std::vector<std::int64_t> values_;
  std::vector<Helper> helpers_;
  std::vector<std::size_t> calls_;
  std::vector<bool> subtract_;
  [[noreturn]] static void bad() { throw std::runtime_error("E_NATIVE_READONLY: unsupported closed class reference profile"); }
  static bool letter(unsigned char c) {return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
  static bool digit(unsigned char c) {return c>='0'&&c<='9';}
  static bool space(unsigned char c) {return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\v'||c=='\f';}
  std::string_view peek() const { return at_<tokens_.size()?std::string_view(tokens_[at_]):std::string_view{}; }
  std::string pop() {if(at_>=tokens_.size()) bad();return tokens_[at_++];}
  void need(std::string_view t) {if(pop()!=t)bad();}
  std::string name() {
    auto n=pop();
    if(n.empty()||n.size()>64||!letter(n.front()))bad();
    static constexpr std::string_view reserved[]={"define","class","as","end","pub","let","int","fnc","main","return","new","mut","const","val","self","actor","unsafe","suspends","await","import","void","bool","float","string","true","false","null","if","else","while","for","match","defer","recover","borrow","take","copy","move","ref","deref","unref","task","spawn","send","critical","static","private","constructor","namespace","module","using","with","set","Option","Result","Array","Map","List","RawPointer","Storage"};
    for(auto r:reserved)if(n==r)bad();
    for(unsigned char c:n)if(!letter(c)&&!digit(c)&&c!='_')bad();
    return n;
  }
  std::int64_t number() {
    auto n=pop();std::int64_t v=0;
    if(n.empty()||n.size()>19||!std::all_of(n.begin(),n.end(),[](unsigned char c){return digit(c);}))bad();
    auto r=std::from_chars(n.data(),n.data()+n.size(),v);
    if(r.ec!=std::errc{}||r.ptr!=n.data()+n.size())bad();
    return v;
  }
  std::size_t call(const std::string& owner) {
    auto n=name();auto f=std::find_if(helpers_.begin(),helpers_.end(),[&](const auto& h){return h.name==n;});
    if(f==helpers_.end())bad();
    need("(");need(owner);need(")");return static_cast<std::size_t>(f-helpers_.begin());
  }
  void parse() {
    need("define");need("class");auto cls=name();need("as");
    while(peek()=="pub"){
      if(fields_.size()>=8)bad();
      need("pub");need("let");need("int");auto f=name();
      if(std::find(fields_.begin(),fields_.end(),f)!=fields_.end())bad();
      need("=");auto v=number();need(";");fields_.push_back(f);values_.push_back(v);
    }
    if(fields_.empty())bad();
    need("end");
    while(peek()=="fnc"){
      if(helpers_.size()>=8)bad();
      need("fnc");auto h=name();
      if(h==cls||std::any_of(helpers_.begin(),helpers_.end(),[&](const auto& f){return f.name==h;}))bad();
      need("(");need(cls);auto p=name();need(")");need(":");need("int");need("{");need("return");
      if(peek()==p){
        need(p);need(".");auto f=name();auto it=std::find(fields_.begin(),fields_.end(),f);
        if(it==fields_.end())bad();
        helpers_.push_back({h,false,static_cast<std::size_t>(it-fields_.begin())});
      }else{
        auto target=call(p);helpers_.push_back({h,true,target});
      }
      need(";");need("}");
    }
    if(helpers_.empty())bad();
    need("pub");need("fnc");need("main");need("(");need(")");need(":");need("int");need("{");
    need("let");auto p=name();need("=");need("new");need(cls);need("(");need(")");need(";");
    need("return");calls_.push_back(call(p));
    while(peek()=="+"||peek()=="-"){
      if(calls_.size()>=8)bad();
      subtract_.push_back(pop()=="-");calls_.push_back(call(p));
    }
    need(";");need("}");if(at_!=tokens_.size())bad();
  }
  explicit Parser(std::string_view s) {
    if(!selected(s)||s.size()>65536)bad();
    oresforge::source::validate_utf8(s);
    // Restrict to ASCII tokens; comments cannot hide Unicode controls or
    // alternate line boundaries. Only line comments are admitted.
    for(unsigned char c:s)if(c>=127||c==0||(c<32&&!space(c)))bad();
    for(std::size_t i=0;i<s.size();){
      auto c=static_cast<unsigned char>(s[i]);
      if(space(c)){++i;continue;}
      if(s.substr(i,2)=="//"){i+=2;while(i<s.size()&&s[i]!='\n'&&s[i]!='\r')++i;continue;}
      auto start=i++;
      if(letter(c)){while(i<s.size()&&(letter(s[i])||digit(s[i])||s[i]=='_'))++i;}
      else if(digit(c)){while(i<s.size()&&digit(s[i]))++i;}
      else if(std::string_view("{}():;,.=+-").find(c)==std::string_view::npos)bad();
      tokens_.emplace_back(s.substr(start,i-start));
      if(tokens_.size()>1024)bad();
    }
    parse();
  }
  static void arm_const(std::ostringstream& out,std::int64_t n){
    auto u=static_cast<std::uint64_t>(n);
    out<<"  movz x9, #"<<(u&65535)<<"\n";
    for(unsigned shift=16;shift<=48;shift+=16)out<<"  movk x9, #"<<((u>>shift)&65535)<<", lsl #"<<shift<<"\n";
  }
  std::string emit(Arch arch)const{
    std::ostringstream out;out<<".text\n";
    const auto aligned=(values_.size()*8+15)/16*16;
    if(arch==Arch::X86_64){
      for(std::size_t i=0;i<helpers_.size();++i){
        const auto& h=helpers_[i];out<<".Lro_"<<i<<":\n";
        if(h.forward)out<<"  jmp .Lro_"<<h.index<<"\n";
        else out<<"  movq "<<h.index*8<<"(%rdi), %rax\n  ret\n";
      }
      out<<".globl oresforge_entry\n.type oresforge_entry,@function\noresforge_entry:\n  pushq %r12\n  pushq %r13\n  subq $"<<aligned+8<<", %rsp\n  movq %rsp, %r12\n";
      for(std::size_t i=0;i<values_.size();++i)out<<"  movabsq $"<<values_[i]<<", %rax\n  movq %rax, "<<i*8<<"(%r12)\n";
      out<<"  movq %r12, %rdi\n  call .Lro_"<<calls_[0]<<"\n  movq %rax, %r13\n";
      for(std::size_t i=1;i<calls_.size();++i)out<<"  movq %r12, %rdi\n  call .Lro_"<<calls_[i]<<"\n  "<<(subtract_[i-1]?"subq":"addq")<<" %rax, %r13\n  jo .Lro_overflow\n";
      out<<"  movq %r13, %rax\n  addq $"<<aligned+8<<", %rsp\n  popq %r13\n  popq %r12\n  ret\n.Lro_overflow:\n  ud2\n.section .note.GNU-stack,\"\",@progbits\n";
    }else if(arch==Arch::ARM64){
      for(std::size_t i=0;i<helpers_.size();++i){
        const auto& h=helpers_[i];out<<".Lro_"<<i<<":\n";
        if(h.forward)out<<"  b .Lro_"<<h.index<<"\n";
        else out<<"  ldr x0, [x0, #"<<h.index*8<<"]\n  ret\n";
      }
      out<<".globl oresforge_entry\n.type oresforge_entry,%function\noresforge_entry:\n  stp x29, x30, [sp, #-16]!\n  stp x19, x20, [sp, #-16]!\n  sub sp, sp, #"<<aligned<<"\n  mov x19, sp\n";
      for(std::size_t i=0;i<values_.size();++i){arm_const(out,values_[i]);out<<"  str x9, [x19, #"<<i*8<<"]\n";}
      out<<"  mov x0, x19\n  bl .Lro_"<<calls_[0]<<"\n  mov x20, x0\n";
      for(std::size_t i=1;i<calls_.size();++i)out<<"  mov x0, x19\n  bl .Lro_"<<calls_[i]<<"\n  "<<(subtract_[i-1]?"subs":"adds")<<" x20, x20, x0\n  b.vs .Lro_overflow\n";
      out<<"  mov x0, x20\n  add sp, sp, #"<<aligned<<"\n  ldp x19, x20, [sp], #16\n  ldp x29, x30, [sp], #16\n  ret\n.Lro_overflow:\n  brk #0\n.section .note.GNU-stack,\"\",%progbits\n";
    }else bad();
    return out.str();
  }
 public:
  static std::string compile(std::string_view s,Arch a){return Parser(s).emit(a);}
};
inline std::string compile(std::string_view s,Arch a){return Parser::compile(s,a);}
}
