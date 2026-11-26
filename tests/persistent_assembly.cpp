#include "compiler/frontend.hpp"
#include "compiler/cache_envelope.hpp"
#include <openssl/evp.h>
#include <filesystem>
#include <algorithm>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
using namespace oresforge;
using namespace oresforge::cache;
namespace fs=std::filesystem;
// Bounded closed scalar fixture. Host owns private directory and ancestors;
// Linux descriptor-relative fixture, trusted procfs and cooperating writers.
// No general compiler cache API or hostile same-UID writer guarantee.
static void require(bool ok){if(!ok)throw std::runtime_error("persistent fixture failed");}
static Secret hash(std::span<const std::uint8_t> b){Secret out{};unsigned n=0;require(EVP_Digest(b.data(),b.size(),out.data(),&n,EVP_sha256(),nullptr)==1&&n==32);return out;}
static std::string hex(const Secret& b){const char* h="0123456789abcdef";std::string s;for(auto c:b){s+=h[c>>4];s+=h[c&15];}return s;}
static std::string digest(std::string_view s){return hex(hash(std::span(reinterpret_cast<const std::uint8_t*>(s.data()),s.size())));}
static const std::string A="fnc main(x: int): int { return x+1; }", B="fnc main(x: int): int { return x+2; }", BAD="fnc main(x: int): int { return missing; }";
static std::string key(std::string_view source,std::string_view compiler){
 auto tool=digest("fixture host compiler"),target=digest("linux-x86_64"),config=digest("closed scalar"),sources=digest(source),bindings=digest("no imports");
 return encode(Input{"native-asm","@test/forge","source-unit","main.atto",compiler,tool,target,config,sources,bindings,std::nullopt,{}});
}
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
static Bytes compile(const std::string& source){auto s=emit_runtime_asm(parse_runtime_source(source),Arch::X86_64);return Bytes(s.begin(),s.end());}
static std::string slot(const std::string& key){return "slot-"+key.substr(0,1);}
static Bytes build(int root,const std::string& source,const std::string& compiler,const Secret& secret,bool cold,bool must_hit){
 auto k=key(source,compiler);auto name=slot(k);
 if(!cold){auto b=read_slot(root,name);if(b){Secret actual{};std::copy_n(b->begin()+8,32,actual.begin());auto a=hex(actual);require(slot(a)==name);
 auto payload=verify(a,*b,secret);if(a==k)return payload;}}
 require(!must_hit);auto bytes=compile(source);publish(root,name,seal(k,bytes,secret));return bytes;
}
template<class F>static void deny(F f){try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error("negative admitted");}
int main(int argc,char** argv){
 require(argc==5);std::string lane=argv[2],compiler=argv[3];Fd root(open_root(argv[1]));require(root.fd>=0);
 struct stat st{};require(fstat(root.fd,&st)==0&&st.st_uid==geteuid()&&(st.st_mode&077)==0);
 Secret master{};master.fill(7);auto record=ledger_record("tenant/a",2,master);auto pin=hash(record);auto secret=ledger_admit("tenant/a",record,pin,master);
 auto source=lane=="edit"?B:A;auto bytes=build(root.fd,source,compiler,secret,lane=="cold",lane=="warm");
 if(lane!="warm")require(bytes==compile(source));
 if(lane=="edit"){auto name=slot(key(B,compiler));auto before=read_slot(root.fd,name);deny([&]{build(root.fd,BAD,compiler,secret,true,false);});
 require(read_slot(root.fd,name)==before);require(build(root.fd,B,compiler,secret,false,true)==bytes);
 deny([&]{ledger_admit("tenant/a",record,Secret{},master);});
 // Test filesystem denials against the real slot, not a mock store.
 require(renameat(root.fd,name.c_str(),root.fd,"saved")==0);
 require(symlinkat("saved",root.fd,name.c_str())==0);deny([&]{read_slot(root.fd,name);});require(unlinkat(root.fd,name.c_str(),0)==0);
 require(linkat(root.fd,"saved",root.fd,name.c_str(),0)==0);deny([&]{read_slot(root.fd,name);});require(unlinkat(root.fd,name.c_str(),0)==0);
 require(mkfifoat(root.fd,name.c_str(),0600)==0);deny([&]{read_slot(root.fd,name);});require(unlinkat(root.fd,name.c_str(),0)==0);
 require(renameat(root.fd,"saved",root.fd,name.c_str())==0);
 require(fchmodat(root.fd,name.c_str(),0644,0)==0);deny([&]{read_slot(root.fd,name);});require(fchmodat(root.fd,name.c_str(),0600,0)==0);
 Fd busy(openat(root.fd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC));require(busy.fd>=0&&flock(busy.fd,LOCK_EX|LOCK_NB)==0);
 deny([&]{publish(root.fd,name,*before);});require(flock(busy.fd,LOCK_UN)==0);require(read_slot(root.fd,name)==before);
 auto alias=fs::path(argv[1])/"alias";fs::create_directory_symlink(fs::path(argv[1]),alias);
 deny([&]{Fd rejected(open_root(alias));});fs::remove(alias);
 std::cout<<"FORGE_DESCRIPTOR|symlink|hardlink|fifo|mode|writer-lock|ancestor-denied\n";
 auto corrupt=*before;corrupt[80]^=1;publish(root.fd,name,corrupt);
 deny([&]{build(root.fd,B,compiler,secret,false,true);});}
 std::ofstream out(argv[4],std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());require(bool(out));
 std::cout<<"PERSISTENT_FORGE|"<<lane<<"|"<<(lane=="warm"?"no-compile":"compiled")<<"\n";
}
