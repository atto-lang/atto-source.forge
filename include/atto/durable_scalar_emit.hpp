#pragma once
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <set>

namespace atto::forge::scalar {
// Bounded schema-only frontend and native C++ lowering. No live actors, IO,
// reflection or guest-selected storage authority. Trusted caller owns buffers.
inline std::string emit_durable_scalar_v1_cpp(std::string_view source) {
    const auto deny=[](){throw std::invalid_argument("E_DURABLE_SCALAR_SCHEMA_V1");};
    const auto alpha=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';};
    const auto digit=[](char c){return c>='0'&&c<='9';};
    if(source.size()>16384)deny();
    std::vector<std::string_view> ts;
    for(std::size_t i=0;i<source.size();){
        char c=source[i];if(c==' '||c=='\t'||c=='\r'||c=='\n'){++i;continue;}
        auto start=i++;
        if(alpha(c)){while(i<source.size()&&(alpha(source[i])||digit(source[i])))++i;}
        else if(digit(c)){while(i<source.size()&&digit(source[i]))++i;}
        else if(std::string_view(":;=-").find(c)==std::string_view::npos)deny();
        if(ts.size()>=4096)deny();
        ts.push_back(source.substr(start,i-start));
    }
    std::size_t i=0;
    auto peek=[&](){return i<ts.size()?ts[i]:std::string_view{};};
    auto next=[&](){auto t=peek();if(t.empty())deny();++i;return t;};
    auto take=[&](std::string_view t){if(next()!=t)deny();};
    const std::string_view reserved=" define pub private durable persistent actor as state end int bool true false self receive init fnc mtd let val mut const return await suspends susp async critical using with import namespace module class struct contract enum new spawn stop if else match on default void ";
    auto name=[&](){auto n=next();if(n.empty()||n.size()>128||!alpha(n.front())||reserved.find(" "+std::string(n)+" ")!=std::string_view::npos)deny();return n;};
    auto le=[](std::string& b,std::uint64_t n,unsigned w){for(unsigned j=0;j<w;++j)b.push_back(static_cast<char>(static_cast<unsigned char>(n>>(8*j))));};
    auto hash=[](std::string_view b){std::uint64_t h=UINT64_C(0xcbf29ce484222325);for(unsigned char x:b){h^=x;h*=UINT64_C(0x100000001b3);}return h;};
    std::string sig="atto.actor.state.schema.v1";sig.push_back(0);
    auto sized=[&](std::string_view n){le(sig,n.size(),4);sig+=n;};
    take("define");take("durable");take("actor");sized(name());take("as");
    std::set<std::string_view> seen;
    struct Field {unsigned tag;std::int64_t value;};std::vector<Field> fields;
    while(peek()=="state"){
        if(fields.size()>=256)deny();
        take("state");auto n=name();if(!seen.insert(n).second)deny();sized(n);take(":");auto type=next();take("=");
        if(type=="int"){
            bool negative=peek()=="-";if(negative)next();auto lit=next();
            if(lit.empty()||lit.size()>19||!digit(lit.front())||(lit.size()>1&&lit.front()=='0'))deny();
            std::string s=(negative?"-":"")+std::string(lit);std::int64_t value=0;
            auto r=std::from_chars(s.data(),s.data()+s.size(),value);if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size())deny();
            fields.push_back({1,value});sig.push_back(1);
        }else if(type=="bool"){
            auto b=next();if(b!="true"&&b!="false")deny();fields.push_back({2,b=="true"?1:0});sig.push_back(2);
        }else deny();take(";");
    }
    take("end");if(!peek().empty())deny();
    std::string initial="ATTOST01";initial.push_back(1);le(initial,hash(sig),8);le(initial,fields.size(),2);
    for(auto f:fields){initial.push_back(static_cast<char>(f.tag));le(initial,static_cast<std::uint64_t>(f.value),f.tag==1?8:1);}
    le(initial,hash(initial),8);
    std::string out=R"CPP(#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>
namespace {
constexpr unsigned char initial[]={)CPP";
    for(unsigned char x:initial){out+=std::to_string(x);out+=',';}
    out+="};\nconstexpr std::array<unsigned char,"+std::to_string(fields.size())+"> tags={";
    for(auto f:fields){out+=std::to_string(f.tag);out+=',';}
    out+=R"CPP(};
constexpr std::size_t size=sizeof(initial), count=tags.size();
std::uint64_t hash(const unsigned char* b,std::size_t n) noexcept {
  std::uint64_t h=UINT64_C(0xcbf29ce484222325);
  for(std::size_t i=0;i<n;++i){h^=b[i];h*=UINT64_C(0x100000001b3);}return h;
}
void put(unsigned char* b,std::uint64_t v) noexcept {for(unsigned i=0;i<8;++i)b[i]=static_cast<unsigned char>(v>>(8*i));}
std::uint64_t get(const unsigned char* b) noexcept {std::uint64_t v=0;for(unsigned i=0;i<8;++i)v|=std::uint64_t(b[i])<<(8*i);return v;}
std::int64_t signed_value(std::uint64_t v) noexcept {
  return v<=UINT64_C(0x7fffffffffffffff)?static_cast<std::int64_t>(v):-1-static_cast<std::int64_t>(~v);
}
}
extern "C" std::int64_t atto_scalar_size_v1() noexcept {return size;}
extern "C" std::int64_t atto_scalar_fields_v1() noexcept {return count;}
extern "C" std::int64_t atto_scalar_initial_v1(void* out,std::int64_t cap) noexcept {
  if(!out||cap<static_cast<std::int64_t>(size))return -1;
  std::memcpy(out,initial,size);return size;
}
extern "C" std::int64_t atto_scalar_encode_v1(const void* values,std::int64_t n,void* out,std::int64_t cap) noexcept {
  if(!values||!out||n!=static_cast<std::int64_t>(count)||cap<static_cast<std::int64_t>(size))return -1;
  std::array<unsigned char,size> scratch{};std::memcpy(scratch.data(),initial,size);
  auto src=static_cast<const unsigned char*>(values);std::size_t p=19;
  for(auto tag:tags){std::int64_t v;std::memcpy(&v,src,sizeof(v));src+=sizeof(v);++p;
    if(tag==1){put(scratch.data()+p,static_cast<std::uint64_t>(v));p+=8;}
    else{if(v!=0&&v!=1)return -1;scratch[p++]=static_cast<unsigned char>(v);}}
  put(scratch.data()+p,hash(scratch.data(),p));std::memcpy(out,scratch.data(),size);return size;
}
extern "C" std::int64_t atto_scalar_decode_v1(const void* input,std::int64_t len,void* out,std::int64_t cap) noexcept {
  if(!input||!out||len!=static_cast<std::int64_t>(size)||cap<static_cast<std::int64_t>(count))return -1;
  std::array<unsigned char,size> scratch{};std::memcpy(scratch.data(),input,size);
  if(std::memcmp(scratch.data(),initial,19)!=0||get(scratch.data()+size-8)!=hash(scratch.data(),size-8))return -1;
  std::array<std::int64_t,count> staged{};std::size_t p=19,i=0;
  for(auto tag:tags){if(scratch[p++]!=tag)return -1;
    if(tag==1){staged[i++]=signed_value(get(scratch.data()+p));p+=8;}
    else{if(scratch[p]>1)return -1;staged[i++]=scratch[p++];}}
  if(p!=size-8)return -1;
  if constexpr(count>0)std::memcpy(out,staged.data(),count*sizeof(std::int64_t));
  return count;
}
)CPP";
    return out;
}
} // namespace atto::forge::scalar
