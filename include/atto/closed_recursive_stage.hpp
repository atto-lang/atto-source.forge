#pragma once
// Restricted, closed typed i64 recursion IR. Not yet source-parser admission.
// Emit a native state machine with explicit activations, no C++ recursion.
#include "atto/checked_i64.hpp"
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace atto::forge::recursive {
enum class Op { literal, arg, self, neg, add, sub, mul, div };
struct Node {
  Op op;
  std::size_t left=0, right=0;
  i64 value=0;
};
struct Arm { std::optional<i64> key; std::size_t body; };
struct Stage {
  std::vector<Node> nodes;
  std::size_t subject=0;
  std::vector<Arm> arms;
};
inline constexpr std::size_t max_nodes=512, max_depth=64, max_edges=1024;
[[nodiscard]] inline bool validate(const Stage& p) {
  if(p.nodes.empty()||p.nodes.size()>max_nodes||p.subject>=p.nodes.size()||
     p.arms.empty()||p.arms.size()>64)return false;
  std::vector<std::size_t> depth(p.nodes.size(),1);
  for(std::size_t i=0;i<p.nodes.size();++i){
    const auto& n=p.nodes[i];
    switch(n.op){
      case Op::literal:if(n.left||n.right)return false;break;
      case Op::arg:if(n.left||n.right||n.value)return false;break;
      case Op::self:case Op::neg:
        if(n.left>=i||n.right||n.value)return false;
        depth[i]=1+depth[n.left];break;
      case Op::add:case Op::sub:case Op::mul:case Op::div:
        if(n.left>=i||n.right>=i||n.value)return false;
        depth[i]=1+(depth[n.left]>depth[n.right]?depth[n.left]:depth[n.right]);break;
      default:return false;
    }
    if(depth[i]>max_depth)return false;
  }
  std::set<i64> keys;
  for(std::size_t i=0;i<p.arms.size();++i){
    const auto& a=p.arms[i];
    if(a.body>=p.nodes.size())return false;
    if(a.key){if(!keys.insert(*a.key).second)return false;}
    else if(i+1!=p.arms.size())return false;
  }
  if(p.arms.back().key)return false;
  return true;
}
[[nodiscard]] inline std::optional<std::string> emit(const Stage& p,i64 input){
  if(!validate(p))return std::nullopt;
  auto number=[](i64 n){
    return n==std::numeric_limits<i64>::min()?std::string("(-9223372036854775807LL-1LL)"):std::to_string(n)+"LL";
  };
  auto idx=[](std::size_t n){return std::to_string(n);};
  std::string out=R"(#include "atto/checked_i64.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
using atto::forge::i64;
struct Frame { std::size_t id,depth; i64 arg; unsigned phase=0; i64 left=0; };
[[noreturn]] static void fail(const char* text,int code){std::fputs(text,stderr);std::exit(code);}
static i64 evaluate(i64 input){
  std::vector<Frame> frames;
  i64 result=0;
  auto push=[&](std::size_t id,i64 arg,std::size_t depth){
    if(frames.size()>=1025u*66u)fail("E_STACK\n",75);
    frames.push_back({id,depth,arg});
  };
  auto finish=[&](i64 v){result=v;frames.pop_back();};
  [[maybe_unused]] auto checked=[](std::optional<i64> v){if(!v)fail("E_OVERFLOW\n",70);return *v;};
)";
  const auto root=idx(p.nodes.size());
  out+="  push("+root+",input,0);\n  while(!frames.empty()){\n    auto f=frames.back();\n    switch(f.id){\n";
  const auto child=[&](std::size_t n){return "push("+idx(n)+",f.arg,f.depth);";};
  for(std::size_t i=0;i<p.nodes.size();++i){
    const auto& n=p.nodes[i];
    out+="      case "+idx(i)+": { ";
    if(n.op==Op::literal)out+="finish("+number(n.value)+");";
    else if(n.op==Op::arg)out+="finish(f.arg);";
    else {
      out+="if(f.phase==0){frames.back().phase=1;"+child(n.left)+"} ";
      if(n.op==Op::neg)out+="else finish(checked(atto::forge::checked_neg(result)));";
      else if(n.op==Op::self)
        out+="else if(f.phase==1){if(f.depth>=1024)fail(\"E_STACK\\n\",75);frames.back().phase=2;push("+root+",result,f.depth+1);}else finish(result);";
      else{
        out+="else if(f.phase==1){frames.back().left=result;frames.back().phase=2;"+child(n.right)+"}else{";
        const char* name=n.op==Op::add?"add":n.op==Op::sub?"sub":n.op==Op::mul?"mul":"div";
        if(n.op==Op::div)out+="if(result==0)fail(\"E_DIVZERO\\n\",71);";
        out+="finish(checked(atto::forge::checked_"+std::string(name)+"(f.left,result)));}";
      }
    }
    out+=" break; }\n";
  }
  out+="      case "+root+": { if(f.phase==0){frames.back().phase=1;"+child(p.subject)+"}else if(f.phase==1){frames.back().phase=2;switch(result){";
  for(const auto& a:p.arms){
    out+=(a.key?"case "+number(*a.key)+":":"default:")+child(a.body)+"break;";
  }
  out+="}}else finish(result);break;}\n      default:fail(\"E_IR\\n\",76);\n    }\n  }\n  return result;\n}\n";
  out+="int main(){std::printf(\"%lld\\n\",static_cast<long long>(evaluate("+number(input)+")));}\n";
  if(out.size()>256*1024)return std::nullopt;
  return out;
}
} // namespace atto::forge::recursive
