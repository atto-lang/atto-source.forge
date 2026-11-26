#include "compiler/persistent_assembly.hpp"
#include "compiler/frontend.hpp"
#include "compiler/namespace_provider.hpp"
#include <map>
#include <set>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <algorithm>
#include <cerrno>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
namespace oresforge::cache {
namespace fs=std::filesystem;
namespace {
static void require(bool ok){if(!ok)throw std::runtime_error("E_CACHE_PATH: bounded private store");}
static Secret hash(std::span<const std::uint8_t> b){Secret out{};unsigned n=0;require(EVP_Digest(b.data(),b.size(),out.data(),&n,EVP_sha256(),nullptr)==1&&n==32);return out;}
static std::string hex(const Secret& b){const char* h="0123456789abcdef";std::string s;for(auto c:b){s+=h[c>>4];s+=h[c&15];}return s;}
static std::string digest(std::string_view s){return hex(hash(std::span(reinterpret_cast<const std::uint8_t*>(s.data()),s.size())));}
static void private_inode(const struct stat& s){require(s.st_uid==geteuid()&&(s.st_mode&077)==0&&S_ISREG(s.st_mode)&&s.st_nlink==1);}
struct Fd{int fd;~Fd(){if(fd>=0)close(fd);}Fd(const Fd&)=delete;explicit Fd(int value):fd(value){}};
static int open_root(const fs::path& path){
 require(path.is_absolute());Fd current(open("/",O_DIRECTORY|O_RDONLY|O_CLOEXEC));require(current.fd>=0);
 for(const auto& part:path.relative_path()){
  if(part==".")continue;require(part!=".."&&!part.empty());
  int next=openat(current.fd,part.c_str(),O_DIRECTORY|O_NOFOLLOW|O_RDONLY|O_CLOEXEC);require(next>=0);
  close(current.fd);current.fd=next;
 }
 int result=current.fd;current.fd=-1;return result;
}
static std::optional<Bytes> read_slot(int root,const std::string& name){
 Fd pinned(openat(root,name.c_str(),O_PATH|O_NOFOLLOW|O_CLOEXEC));if(pinned.fd<0){if(errno==ENOENT)return std::nullopt;throw std::runtime_error("cache open");}
 struct stat old{};require(fstat(pinned.fd,&old)==0);private_inode(old);require(old.st_size>=112&&old.st_size<=112+4*1024*1024);
 auto proc="/proc/self/fd/"+std::to_string(pinned.fd);Fd f(open(proc.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC));require(f.fd>=0);
 struct stat s{};require(fstat(f.fd,&s)==0);private_inode(s);require(s.st_ino==old.st_ino&&s.st_dev==old.st_dev&&s.st_size>=112&&s.st_size<=112+4*1024*1024);
 Bytes b(static_cast<std::size_t>(s.st_size));std::size_t p=0;while(p<b.size()){auto n=read(f.fd,b.data()+p,b.size()-p);require(n>0);p+=static_cast<std::size_t>(n);}
 unsigned char extra;require(read(f.fd,&extra,1)==0);return b;
}
static void publish(int root,const std::string& name,const Bytes& b){
 // Separate open-file description serializes cooperating writers.
 Fd lock(openat(root,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));require(lock.fd>=0);
 require(flock(lock.fd,LOCK_EX|LOCK_NB)==0);
 if(unlinkat(root,".pending",0)!=0)require(errno==ENOENT);Fd f(openat(root,".pending",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));require(f.fd>=0);
 try{std::size_t p=0;while(p<b.size()){auto n=write(f.fd,b.data()+p,b.size()-p);require(n>0);p+=static_cast<std::size_t>(n);}
 require(fsync(f.fd)==0);require(renameat(root,".pending",root,name.c_str())==0);require(fsync(root)==0);}
 catch(...){unlinkat(root,".pending",0);throw;}
}

struct Wipe {Secret value;~Wipe(){OPENSSL_cleanse(value.data(),value.size());}};
}
AssemblyBuild compile_cached_assembly(const fs::path& root,std::string_view source,std::string_view scope,
 std::string_view compiler,std::string_view toolchain,std::span<const std::uint8_t> record,
 const Secret& pin,const Secret& master,bool cold){
 if(source.size()>65536||source.find('\0')!=std::string_view::npos)throw std::runtime_error("E_CACHE_SOURCE: source budget");
 // Conservative closed-source admission, including inert text. The actual
 // scalar runtime parser still checks all syntax and semantics on a miss.
 for(std::size_t i=0;i<source.size();){
  auto word=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';};
  if(!word(source[i])){++i;continue;}auto begin=i++;while(i<source.size()&&word(source[i]))++i;
  auto token=source.substr(begin,i-begin);
  if(token=="import"||token=="include"||token=="namespace"||token=="package"||token=="extern"||token=="external"||token=="use"||token=="ffi")
   throw std::runtime_error("E_CACHE_SOURCE: standalone profile only");
 }
 auto target=digest("linux-x86_64"),config=digest("closed-runtime-assembly-v1"),sources=digest(source),bindings=digest("no imports");
 auto k=encode(Input{"native-asm",scope,"source-unit","main.atto",compiler,toolchain,target,config,sources,bindings,std::nullopt,{}});
 auto name="slot-"+k.substr(0,1);Wipe secret{ledger_admit(scope,record,pin,master)};
 Fd dir(open_root(root));struct stat st{};require(fstat(dir.fd,&st)==0&&st.st_uid==geteuid()&&(st.st_mode&077)==0);
 if(!cold){auto b=read_slot(dir.fd,name);if(b){Secret actual{};std::copy_n(b->begin()+8,32,actual.begin());auto a=hex(actual);require(a[0]==k[0]);
 auto payload=verify(a,*b,secret.value);if(a==k)return {std::move(payload),true};}}
 auto assembly=emit_runtime_asm(parse_runtime_source(std::string(source)),Arch::X86_64);
 Bytes bytes(assembly.begin(),assembly.end());publish(dir.fd,name,seal(k,bytes,secret.value));return {std::move(bytes),false};
}

namespace {
bool provider_ident(std::string_view v){
 if(v.empty()||v.size()>128)return false;
 auto alpha=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';};
 if(!alpha(v.front()))return false;
 return std::all_of(v.begin()+1,v.end(),[&](char c){return alpha(c)||(c>='0'&&c<='9');});
}
bool provider_path(std::string_view v){
 if(v.size()>256||!v.ends_with(".atto"))return false;
 v.remove_suffix(5);
 for(;;){auto slash=v.find('/');if(!provider_ident(v.substr(0,slash)))return false;
  if(slash==std::string_view::npos)return true;v.remove_prefix(slash+1);}
}
}
AssemblyBuild compile_cached_namespace_provider(const fs::path& root,
 std::span<const ProviderInput> inventory,std::string_view expected_manifest,std::string_view expected_sources,
 std::string_view name_space,std::string_view symbol,std::size_t arity,
 std::string_view scope,std::string_view compiler,std::string_view toolchain,
 std::span<const std::uint8_t> record,const Secret& pin,const Secret& master,bool cold){
 Wipe secret{ledger_admit(scope,record,pin,master)};
 if(inventory.empty()||inventory.size()>128)throw std::runtime_error("E_NAMESPACE_INVENTORY: member budget");
 std::map<std::string_view,const ProviderInput*> ordered;
 std::set<std::pair<std::string_view,std::string_view>> names;
 for(const auto& p:inventory){
  if(!provider_path(p.path)||!provider_ident(p.name_space)||!provider_ident(p.symbol)||p.arity>4||
     p.source.size()>4096||!ordered.emplace(p.path,&p).second||!names.emplace(p.name_space,p.symbol).second)
   throw std::runtime_error("E_NAMESPACE_INVENTORY: invalid or duplicate member");
 }
 std::string manifest="atto.forge.provider-inventory.v1\n",sources="atto.forge.provider-sources.v1\n";
 for(auto [path,p]:ordered){
  manifest+=std::string(path)+"\n"+std::string(p->name_space)+"\n"+std::string(p->symbol)+"\n"+std::to_string(p->arity)+"\n";
  sources+=std::string(path)+"\n"+digest(p->source)+"\n";
 }
 auto manifest_hash=digest(manifest);
 if(manifest_hash!=expected_manifest||digest(sources)!=expected_sources)
  throw std::runtime_error("E_NAMESPACE_PIN: inventory mismatch");
 const ProviderInput* selected=nullptr;std::optional<Program> program;
 for(auto [path,p]:ordered){
  auto checked=admit_namespace_provider(p->source,p->name_space,p->symbol,p->arity);
  if(p->name_space==name_space&&p->symbol==symbol){
   if(p->arity!=arity)throw std::runtime_error("E_NAMESPACE_ARITY: incompatible selected call");
   selected=p;program=std::move(checked);
  }
 }
 if(!selected||!program)throw std::runtime_error("E_NAMESPACE_SYMBOL: selected provider absent");
 auto target=digest("linux-x86_64"),config=digest("closed-provider-checked-i64-assembly-v1"),
      selected_hash=digest(selected->source),bindings=digest(std::string(selected->path)+"\n"+std::string(name_space)+"\n"+std::string(symbol)+"\n"+std::to_string(arity));
 const std::array<Dependency,1> deps{{{"provider-inventory",manifest_hash}}};
 auto k=encode(Input{"native-provider-asm-bound-input-v2",scope,"source-unit",selected->path,
  compiler,toolchain,target,config,selected_hash,bindings,std::nullopt,deps});
 auto slot="slot-"+k.substr(0,1);
 Fd dir(open_root(root));struct stat st{};require(fstat(dir.fd,&st)==0&&st.st_uid==geteuid()&&(st.st_mode&077)==0);
 if(!cold){auto b=read_slot(dir.fd,slot);if(b){
  Secret actual{};std::copy_n(b->begin()+8,32,actual.begin());auto a=hex(actual);require(a[0]==k[0]);
  auto payload=verify(a,*b,secret.value);if(a==k)return {std::move(payload),true};
 }}
 auto assembly=emit_runtime_asm(*program,Arch::X86_64);
 Bytes bytes(assembly.begin(),assembly.end());publish(dir.fd,slot,seal(k,bytes,secret.value));
 return {std::move(bytes),false};
}
}
