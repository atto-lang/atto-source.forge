#include "compiler/persistent_assembly.hpp"
#include <openssl/evp.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
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
 if(lane=="cold"){
  namespace fs=std::filesystem;fs::path root=argv[1],slot;
  for(const auto& e:fs::directory_iterator(root)){if(e.path().filename().string().starts_with("slot-")){require(slot.empty());slot=e.path();}}
  require(!slot.empty());auto saved=root/"saved";fs::rename(slot,saved);
  fs::create_symlink(saved,slot);deny([&]{build(a,true);});require(fs::is_symlink(slot));fs::remove(slot);
  fs::create_hard_link(saved,slot);deny([&]{build(a,true);});require(fs::equivalent(slot,saved));fs::remove(slot);
  require(mkfifo(slot.c_str(),0600)==0);deny([&]{build(a,true);});require(fs::is_fifo(fs::symlink_status(slot)));fs::remove(slot);
  fs::rename(saved,slot);
  auto pending=root/".pending";std::ofstream(pending)<<"abandoned";require(chmod(pending.c_str(),0600)==0);
  deny([&]{build(a,true);});std::ifstream in(pending);std::string text;in>>text;require(text=="abandoned");fs::remove(pending);
  require(build(a).reused);
  std::ifstream original_in(slot,std::ios::binary);
  Bytes original((std::istreambuf_iterator<char>(original_in)),{});
  original_in.close();
  require(original.size()>=112);
  const char* digits="0123456789abcdef";
  std::string key;
  for(std::size_t i=8;i<40;++i){key+=digits[original[i]>>4];key+=digits[original[i]&15];}
  auto secret=ledger_admit("tenant/api",record,pin,master);
  auto malformed=[&](std::string text){
   Bytes bytes(text.begin(),text.end());auto signed_bytes=seal(key,bytes,secret);
   require(verify(key,signed_bytes,secret)==bytes);
   {std::ofstream f(slot,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(signed_bytes.data()),signed_bytes.size());require(bool(f));}
   bool denied=false;try{(void)build(a);}catch(const std::runtime_error& e){denied=std::string(e.what()).find("E_ASM_ADMISSION")!=std::string::npos;}
   require(denied);
  };
  std::string assembly(result.bytes.begin(),result.bytes.end());
  malformed(assembly+".byte 0\n");
  auto forged=assembly;auto offset=forged.find("0(%rsp)");require(offset!=std::string::npos);forged.replace(offset,7,"999999(%rsp)");malformed(forged);
  {std::ofstream f(slot,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(original.data()),original.size());require(bool(f));}
  require(build(a).reused);
  std::cout<<"FORGE_PINNED_ASSEMBLY|signed-malformed-denied|restored-hit\n";
  std::cout<<"FORGE_COLD_DESTINATION|symlink|hardlink|fifo|staging-preserved|rollback\n";
 }
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
