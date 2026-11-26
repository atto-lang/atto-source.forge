#include "compiler/persistent_assembly.hpp"
#include <openssl/evp.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace oresforge::cache;
static void require(bool ok){if(!ok)throw std::runtime_error("API assertion");}
static Secret hash(std::span<const std::uint8_t> b){Secret out{};unsigned n=0;require(EVP_Digest(b.data(),b.size(),out.data(),&n,EVP_sha256(),nullptr)==1&&n==32);return out;}
template<class F>void deny(F f){try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error("negative admitted");}
int main(int argc,char**argv){
 require(argc==5);std::string lane=argv[2],compiler=argv[3],toolchain(64,'a');
 std::string a="fnc main(x: int): int { return x+1; }",b="fnc main(x: int): int { return x+2; }";
 Secret master{};master.fill(7);auto record=ledger_record("tenant/api",1,master);auto pin=hash(record);
 auto build=[&](std::string_view s,bool cold=false){return compile_cached_assembly(argv[1],s,"tenant/api",compiler,toolchain,record,pin,master,cold);};
 auto result=build(lane=="edit"?b:a,lane=="cold");require(result.reused==(lane=="warm"));
 if(lane=="edit"){
  deny([&]{build("fnc main(x: int): int { return missing; }",true);});
  require(build(b).reused);deny([&]{build("import pkg x;");});deny([&]{build(std::string(65537,'x'));});
  deny([&]{compile_cached_assembly(argv[1],b,"tenant/api",compiler,toolchain,record,Secret{},master,true);});
  auto changed=compile_cached_assembly(argv[1],b,"tenant/api",compiler,std::string(64,'b'),record,pin,master);
  require(!changed.reused&&changed.bytes==result.bytes);
 }
 std::ofstream out(argv[4],std::ios::binary);out.write(reinterpret_cast<const char*>(result.bytes.data()),result.bytes.size());require(bool(out));
 std::cout<<"FORGE_RESTRICTED_API|"<<lane<<"|"<<(result.reused?"hit":"compiled")<<"\n";
}
