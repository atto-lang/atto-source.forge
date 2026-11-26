#include "compiler/cache_envelope.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <algorithm>
#include <stdexcept>
namespace oresforge::cache {
namespace {
constexpr std::size_t Header=112,Max=4*1024*1024;
[[noreturn]] void bad(){throw std::runtime_error("E_CACHE_KEY: invalid input");}
[[noreturn]] void invalid(){throw std::runtime_error("E_CACHE_RECEIPT: invalid envelope");}
void append(Bytes& out,std::span<const std::uint8_t> data){out.insert(out.end(),data.begin(),data.end());}
void text(Bytes& out,std::string_view s){out.insert(out.end(),s.begin(),s.end());}
void integer(Bytes& out,std::uint64_t n,unsigned width){for(unsigned i=width;i>0;--i)out.push_back(static_cast<std::uint8_t>(n>>((i-1)*8)));}
void identity(Bytes& out,std::string_view s){
 if(s.empty()||s.size()>512||!std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;}))bad();
 integer(out,s.size(),4);text(out,s);
}
std::uint8_t nibble(char c){if(c>='0'&&c<='9')return static_cast<std::uint8_t>(c-'0');if(c>='a'&&c<='f')return static_cast<std::uint8_t>(c-'a'+10);bad();}
Secret digest(std::string_view s){if(s.size()!=64)bad();Secret out{};for(std::size_t i=0;i<32;i++)out[i]=static_cast<std::uint8_t>((nibble(s[2*i])<<4)|nibble(s[2*i+1]));return out;}
Secret sha(std::span<const std::uint8_t> data){
 Secret out{};unsigned n=0;
 if(EVP_Digest(data.data(),data.size(),out.data(),&n,EVP_sha256(),nullptr)!=1||n!=out.size())invalid();return out;
}
std::string hex(const Secret& b){const char* chars="0123456789abcdef";std::string out;for(auto c:b){out+=chars[c>>4];out+=chars[c&15];}return out;}
Secret mac(const Secret& secret,std::span<const std::uint8_t> header){
 Bytes data;text(data,"atto.compiler.artifact-receipt.v1");data.push_back(0);append(data,header.first(80));
 Secret out{};unsigned n=0;
 if(!HMAC(EVP_sha256(),secret.data(),static_cast<int>(secret.size()),data.data(),data.size(),out.data(),&n)||n!=out.size())invalid();return out;
}
bool equal(std::span<const std::uint8_t> a,std::span<const std::uint8_t> b){return a.size()==b.size()&&CRYPTO_memcmp(a.data(),b.data(),a.size())==0;}
Secret receipt_key(std::string_view s){try{return digest(s);}catch(const std::runtime_error&){invalid();}}
}
std::string encode(const Input& x){
 if((x.unit_kind!="namespace"&&x.unit_kind!="source-unit")||x.dependencies.size()>128)bad();
 Bytes out;text(out,"atto.compiler.cache-key.v1");out.push_back(0);
 for(auto s:{x.stage,x.owner,x.unit_kind,x.unit})identity(out,s);
 for(auto s:{x.compiler,x.toolchain,x.target,x.configuration,x.sources,x.bindings})append(out,digest(s));
 out.push_back(x.generated?1:0);if(x.generated)append(out,digest(*x.generated));
 std::vector<Dependency> deps(x.dependencies.begin(),x.dependencies.end());
 std::sort(deps.begin(),deps.end(),[](const auto&a,const auto&b){return a.identity<b.identity;});
 integer(out,deps.size(),4);
 for(std::size_t i=0;i<deps.size();i++){if(i&&deps[i-1].identity==deps[i].identity)bad();identity(out,deps[i].identity);append(out,digest(deps[i].digest));}
 return hex(sha(out));
}
Bytes seal(std::string_view key,std::span<const std::uint8_t> p,const Secret& secret){
 if(p.size()>Max)invalid();Bytes out;text(out,"ATTOCAC1");append(out,receipt_key(key));integer(out,p.size(),8);append(out,sha(p));append(out,mac(secret,out));append(out,p);return out;
}
Bytes verify(std::string_view key,std::span<const std::uint8_t> b,const Secret& secret){
 if(b.size()<Header||b.size()>Header+Max)invalid();
 auto k=receipt_key(key);Bytes magic;text(magic,"ATTOCAC1");std::uint64_t length=0;for(auto v:b.subspan(40,8))length=(length<<8)|v;
 if(!equal(magic,b.first(8))||!equal(k,b.subspan(8,32))||length!=b.size()-Header||!equal(mac(secret,b),b.subspan(80,32))||!equal(sha(b.subspan(Header)),b.subspan(48,32)))invalid();
 return Bytes(b.begin()+Header,b.end());
}

namespace {
[[noreturn]] void ledger_invalid(){throw std::runtime_error("E_CACHE_LEDGER: invalid or stale record");}
struct WipedKey {Secret value{}; ~WipedKey(){OPENSSL_cleanse(value.data(),value.size());}};
Bytes ledger_scope(std::string_view scope){
 Bytes result;try{identity(result,scope);}catch(const std::runtime_error&){ledger_invalid();}return result;
}
void derive_ledger(std::string_view scope,std::uint64_t epoch,const Secret& master,Secret& out){
 if(epoch==0||equal(master,Secret{}))ledger_invalid();
 Bytes message;text(message,"atto.compiler.local-receipt-key.v1");message.push_back(0);
 append(message,ledger_scope(scope));integer(message,epoch,8);
 unsigned n=0;if(!HMAC(EVP_sha256(),master.data(),static_cast<int>(master.size()),message.data(),message.size(),out.data(),&n)||n!=out.size())ledger_invalid();
}
}
Bytes ledger_record(std::string_view scope,std::uint64_t epoch,const Secret& master){
 auto id=ledger_scope(scope);WipedKey key;derive_ledger(scope,epoch,master,key.value);
 Bytes result;text(result,"ATTOLED1");append(result,id);integer(result,epoch,8);append(result,sha(key.value));return result;
}
Secret ledger_admit(std::string_view scope,std::span<const std::uint8_t> record,const Secret& current_pin,const Secret& master){
 auto id=ledger_scope(scope);if(record.size()!=52+scope.size())ledger_invalid();
 Bytes magic;text(magic,"ATTOLED1");
 if(!equal(sha(record),current_pin)||!equal(record.first(8),magic)||!equal(record.subspan(8,id.size()),id))ledger_invalid();
 std::uint64_t epoch=0;for(auto v:record.subspan(12+scope.size(),8))epoch=(epoch<<8)|v;
 WipedKey key;derive_ledger(scope,epoch,master,key.value);
 if(!equal(sha(key.value),record.subspan(20+scope.size(),32)))ledger_invalid();
 return key.value;
}
}
