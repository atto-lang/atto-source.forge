#include "compiler/persistent_assembly.hpp"
#include "compiler/namespace_provider.hpp"
#include <openssl/evp.h>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
using namespace oresforge::cache;
static void require(bool ok){if(!ok)throw std::runtime_error("bound provider assertion");}
static Secret hash(std::span<const std::uint8_t> b){Secret out{};unsigned n=0;require(EVP_Digest(b.data(),b.size(),out.data(),&n,EVP_sha256(),nullptr)==1&&n==32);return out;}
static std::string digest(std::string_view s){auto b=hash(std::span(reinterpret_cast<const std::uint8_t*>(s.data()),s.size()));std::string out;for(auto c:b){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out;}
template<class F>void deny(F f){try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error("negative admitted");}
int main(int argc,char**argv){
 require(argc==5);std::string lane=argv[2],compiler=argv[3],toolchain(64,'a');
 require(lane=="cold"||lane=="warm"||lane=="edit");
 const std::string a="namespace billing;\npub fnc get(int x, int y): int { return x * y + 2; }\n";
 const std::string b="namespace billing;\npub fnc get(int x, int y): int { return x * y + 3; }\n";
 const std::string other="namespace identity;\npub fnc other(): int { return 900; }\n";
 const std::string other_edit="namespace identity;\npub fnc other(): int { return 901; }\n";
 std::array<ProviderInput,2> inputs{{{"src/billing.atto","billing","get",lane=="edit"?b:a,2},
 {"src/identity.atto","identity","other",lane=="cold"?other:other_edit,0}}};
 auto pins=[](auto items){
  std::sort(items.begin(),items.end(),[](auto x,auto y){return x.path<y.path;});
  std::string m="atto.forge.provider-inventory.v1\n",s="atto.forge.provider-sources.v1\n";
  for(auto p:items){m+=std::string(p.path)+"\n"+std::string(p.name_space)+"\n"+std::string(p.symbol)+"\n"+std::to_string(p.arity)+"\n";
   s+=std::string(p.path)+"\n"+digest(p.source)+"\n";}
  return std::pair{digest(m),digest(s)};
 };
 Secret master{};master.fill(7);auto record=ledger_record("tenant/provider",1,master);auto pin=hash(record);
 auto build=[&](auto items,bool cold=false){auto [m,s]=pins(items);return compile_cached_namespace_provider(argv[1],items,m,s,"billing","get",2,"tenant/provider",compiler,toolchain,record,pin,master,cold);};
 auto result=build(inputs,lane=="cold");require(result.reused==(lane=="warm"));
 auto clean=oresforge::compile_namespace_provider(inputs[0].source,"billing","get",oresforge::Arch::X86_64,2);
 require(result.bytes==Bytes(clean.begin(),clean.end()));
 if(lane=="warm"){
  auto stale=inputs;stale[1].source=other;
  auto [m,s]=pins(stale);
  deny([&]{compile_cached_namespace_provider(argv[1],inputs,m,s,"billing","get",2,"tenant/provider",compiler,toolchain,record,pin,master);});
  auto bad=inputs;bad[1].source="namespace identity;\npub fnc other(): int { return missing; }\n";
  deny([&]{build(bad);});
  auto duplicate=inputs;duplicate[1]=duplicate[0];deny([&]{build(duplicate);});
  auto current=pins(inputs);
  deny([&]{compile_cached_namespace_provider(argv[1],inputs,current.first,current.second,"billing","get",1,"tenant/provider",compiler,toolchain,record,pin,master);});
  deny([&]{compile_cached_namespace_provider(argv[1],inputs,current.first,current.second,"billing","get",2,"tenant/provider",compiler,toolchain,record,Secret{},master);});
  require(build(inputs).reused);
 }
 if(lane=="edit"){
  auto renamed=inputs;renamed[1].path="src/renamed.atto";
  auto changed=build(renamed);require(!changed.reused&&changed.bytes==result.bytes);
 }
 std::ofstream out(argv[4],std::ios::binary);out.write(reinterpret_cast<const char*>(result.bytes.data()),result.bytes.size());require(bool(out));
 std::cout<<"FORGE_BOUND_PROVIDER|"<<lane<<"|"<<(result.reused?"hit":"compiled")<<"|clean-equivalent\n";
}
