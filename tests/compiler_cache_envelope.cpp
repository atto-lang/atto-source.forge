#include "compiler/cache_envelope.hpp"
#include "compiler/frontend.hpp"
#include <iostream>
#include <openssl/evp.h>
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

 auto tohex=[&](auto const& data){std::string s;for(auto v:data){s+=chars[v>>4];s+=chars[v&15];}return s;};
 auto hash=[](const Bytes& data){Secret out{};unsigned n=0;require(EVP_Digest(data.data(),data.size(),out.data(),&n,EVP_sha256(),nullptr)==1&&n==32);return out;};
 Secret master{};master.fill(7);
 auto record=ledger_record("tenant/a",2,master);auto pin=hash(record);
 require(tohex(record)=="4154544f4c4544310000000874656e616e742f610000000000000002a63c8f18ca3245c9d319f61b2ca45a2a6605ea3293664ad236792133ce088462");
 require(tohex(pin)=="95c69714073e8a9a4c845fa54d0f87706f718fed1f5879016723780618b83309");
 auto current=ledger_admit("tenant/a",record,pin,master);
 require(tohex(current)=="d2153371616fc59b235d24aff7417a37fea38a6fdc5391503a54ea33f429a90e");
 auto old=ledger_record("tenant/a",1,secret);auto prior=ledger_admit("tenant/a",old,hash(old),secret);
 deny([&]{verify(key,seal(key,payload,prior),current);});
 deny([&]{ledger_admit("tenant/a",old,pin,secret);});
 deny([&]{ledger_admit("tenant/b",record,pin,master);});
 deny([&]{ledger_admit("tenant/a",record,pin,secret);});
 deny([&]{ledger_record("tenant/a",0,master);});
 deny([&]{ledger_record("tenant/a",2,Secret{});});
 deny([&]{ledger_record("tenant\n/a",2,master);});
 for(std::size_t i:{std::size_t(0),std::size_t(8),std::size_t(12),std::size_t(20),record.size()-1}){auto changed=record;changed[i]^=1;deny([&]{ledger_admit("tenant/a",changed,pin,master);});}
 auto extra=record;extra.push_back(0);deny([&]{ledger_admit("tenant/a",extra,pin,master);});
 auto high=ledger_record("tenant/a",UINT64_MAX,master);require(ledger_admit("tenant/a",high,hash(high),master)!=Secret{});
 std::cout<<"CACHE_LEDGER_FORGE|canonical-vector|stale-pin-denied|revoked-receipt-denied\n";
}
