#include "compiler/cache_envelope.hpp"
#include "compiler/frontend.hpp"
#include <iostream>
#include <stdexcept>
using namespace oresforge::cache;
void require(bool value){if(!value)throw std::runtime_error("cache vector mismatch");}
template<class F> void deny(F f){try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error("invalid input admitted");}
int main(){
 std::array<std::string,10> hs;for(unsigned i=0;i<10;i++)for(int j=0;j<32;j++)hs[i]+="0"+std::to_string(i);
 std::array<Dependency,2> deps{{{"@org/project/b",hs[7]},{"@org/project/c",hs[8]}}};
 Input x{"typed-ir","@org/project/a","namespace","network",hs[1],hs[2],hs[3],hs[4],hs[5],hs[6],std::nullopt,deps};
 const std::string key="6d0658c41d4e45ede029ca0c7654333c5aa0bc117f3179f12832aa7b1d22b657";
 require(encode(x)==key);std::swap(deps[0],deps[1]);require(encode(x)==key);
 auto bad=x;bad.owner="bad\n";deny([&]{encode(bad);});bad=x;bad.compiler="bad";deny([&]{encode(bad);});bad=x;bad.generated=hs[0];require(encode(bad)!=key);
 auto saved=deps[0];deps[0]=deps[1];deny([&]{encode(x);});deps[0]=saved;
 Secret secret{};for(unsigned i=0;i<32;i++)secret[i]=static_cast<std::uint8_t>(i);
 Bytes payload{'a','t','t','o','-','a','r','t','i','f','a','c','t','-','v','1',0,255};
 auto envelope=seal(key,payload,secret);
 const char* chars="0123456789abcdef";std::string tag;for(unsigned i=80;i<112;i++){tag+=chars[envelope[i]>>4];tag+=chars[envelope[i]&15];}
 require(tag=="3653396c79c689b07bb08274d60b4de17d57d0c9dd4fd22180aa7af88940b524");require(verify(key,envelope,secret)==payload);
 for(unsigned i:{0u,8u,40u,48u,80u,112u,129u}){auto b=envelope;b[i]^=1;deny([&]{verify(key,b,secret);});}
 deny([&]{verify(hs[0],envelope,secret);});deny([&]{verify(key,envelope,Secret{});});deny([&]{seal(key,Bytes(4*1024*1024+1),secret);});
 auto b=envelope;b.push_back(0);deny([&]{verify(key,b,secret);});
 // Real compiler-generated assembly also round trips. This does not grant
 // a cache lookup authority or skip any semantic checking.
 auto asm_text=oresforge::emit_runtime_asm(oresforge::parse_runtime_source("fnc main(x: int): int {return x+1;}"),oresforge::Arch::X86_64);
 Bytes assembly(asm_text.begin(),asm_text.end());require(verify(key,seal(key,assembly,secret),secret)==assembly);
 std::cout<<"CACHE_FORGE_ENVELOPE|canonical-vector|tampering-denied|actual-assembly-roundtrip\n";
}
