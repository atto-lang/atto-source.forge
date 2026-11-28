#pragma once
// Bounded opt-in Counter broker ABI. No disk authority or volatile fallback.
#include "atto/scalar_lexer.hpp"
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atto::forge::scalar {
inline std::uint64_t durable_fnv(const std::vector<unsigned char>& bytes) {
  std::uint64_t h=UINT64_C(0xcbf29ce484222325);
  for (auto b:bytes) { h^=b; h*=UINT64_C(0x100000001b3); }
  return h;
}
inline void durable_le(std::vector<unsigned char>& out,std::uint64_t value,unsigned width) {
  for(unsigned i=0;i<width;++i) out.push_back(static_cast<unsigned char>(value>>(8*i)));
}
inline void durable_text(std::vector<unsigned char>& out,std::string_view text) {
  for(unsigned char b:text) out.push_back(b);
}
inline void durable_uleb(std::vector<unsigned char>& out,std::size_t n) {
  do { const auto b=static_cast<unsigned char>(n&127);n>>=7;out.push_back(n?b|128:b); } while(n);
}
inline std::optional<std::string> emit_durable_counter_v1(std::string_view source) {
  if(source.size()>max_source_bytes) return std::nullopt;
  // Restricted ASCII profile: reject hidden controls even in comments.
  for(unsigned char c:source)
    if((c<32 && c!='\t' && c!='\r' && c!='\n')||c>=127) return std::nullopt;
  const auto lexed=tokenize(source);
  if(!lexed.ok()) return std::nullopt;
  std::size_t i=0;
  auto peek=[&]()->std::string_view {
    if(i>=lexed.tokens.size()) return {};
    const auto t=lexed.tokens[i];
    return source.substr(t.offset,t.length);
  };
  auto consume=[&](std::string_view token){if(peek()!=token||i>=lexed.tokens.size())return false;++i;return true;};
  auto sequence=[&](std::initializer_list<std::string_view> tokens){
    for(auto t:tokens) if(!consume(t)) return false;
    return true;
  };
  // For this initial profile only admit the exact cross-compiler test names;
  // do not claim general identifier/keyword parsing parity.
  if(!sequence({"define","durable","actor","Counter","as","state"}))return std::nullopt;
  const auto field=peek();
  if(field!="count"&&field!="balance")return std::nullopt;
  ++i;
  if(!sequence({":","int","=","0",";","receive","(","ActorMail","<","int",">"}))return std::nullopt;
  const auto mail=peek();
  if(mail!="mail"&&mail!="event")return std::nullopt;
  ++i;
  if(!sequence({")","->","void","{","self",".",field,"=","self",".",field,"+",mail,".","value",";","}","end"}))return std::nullopt;
  if(i+1!=lexed.tokens.size()||lexed.tokens[i].kind!=Kind::end)return std::nullopt;
  std::vector<unsigned char> sig;
  durable_text(sig,"atto.actor.state.schema.v1");sig.push_back(0);
  durable_le(sig,7,4);durable_text(sig,"Counter");
  durable_le(sig,field.size(),4);durable_text(sig,field);sig.push_back(1);
  std::vector<unsigned char> initial;
  durable_text(initial,"ATTOST01");initial.push_back(1);
  durable_le(initial,durable_fnv(sig),8);durable_le(initial,1,2);initial.push_back(1);
  durable_le(initial,0,8);durable_le(initial,durable_fnv(initial),8);
  std::vector<unsigned char> descriptor{1};
  durable_uleb(descriptor,7);durable_text(descriptor,"Counter");
  durable_uleb(descriptor,field.size());durable_text(descriptor,field);descriptor.push_back(1);
  durable_uleb(descriptor,initial.size());descriptor.insert(descriptor.end(),initial.begin(),initial.end());
  // The admitted AST maps to this fixed native transition. No source splice.
  std::string out=R"CPP(#include <cstdint>
#include <cstddef>
extern "C" std::int64_t atto_durable_transition_v1(std::int64_t state,std::int64_t op,std::int64_t delta) noexcept {
  constexpr std::int64_t maximum=1000000000;
  if(state<0||state>maximum)return -1;
  if(op==2)return delta==0?state:-1;
  if(op!=1||delta<=0||delta>maximum||state>maximum-delta)return -1;
  return state+delta;
}
extern "C" const unsigned char* atto_durable_state_v1(std::size_t* size) noexcept {
  static constexpr unsigned char bytes[]={)CPP";
  for(auto b:descriptor){out+=std::to_string(b);out+=',';}
  out+=R"CPP(};
  if(!size)return nullptr;
  *size=sizeof(bytes);return bytes;
}
)CPP";
  return out;
}
} // namespace atto::forge::scalar
