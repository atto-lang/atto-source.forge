#include "generated.hpp"
#include <cassert>
#include <iostream>
using C=atto_generated::Codec;
using I=std::int64_t;
using atto_generated::encode;
using atto_generated::decode;
static std::string hex(std::string_view b){std::string out;for(unsigned char c:b){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out;}
static void reject(std::string_view b){bool denied=false;try{(void)decode(b);}catch(const std::logic_error&){denied=true;}assert(denied);}
int main(){
    assert(atto_generated::schema_id==UINT64_C(0xdb14dea8243c195b));
    const std::vector<C::Value> values{std::numeric_limits<I>::min(),true,std::string("A\0\xc3\xa9\xf0\x9f\x98\x80",8),C::Bytes{0,255,128}};
    const auto b=encode(values);
    assert(hex(b)=="4154544f53543032025b193c24a8de14db0400010000000000000080020103080000004100c3a9f09f9880040300000000ff8074c4cc420e041ac0");
    assert(decode(b)==values);
    for(I i=-5000;i<=5000;++i){std::vector<C::Value> v{i,i%2==0,std::string(static_cast<std::size_t>((i+5000)%257),'a'),C::Bytes{static_cast<unsigned char>(i),0,255}};assert(decode(encode(v))==v);}
    for(std::size_t i=0;i<b.size();++i){reject(std::string_view(b).substr(0,i));for(unsigned bit=0;bit<8;++bit){auto bad=b;bad[i]=static_cast<char>(static_cast<unsigned char>(bad[i])^(1u<<bit));reject(bad);}}
    auto owned=decode(b);std::get<C::Bytes>(owned[3])[0]=42;assert(decode(b)==values);
    std::cout<<"FORGE_BOUNDED_V2_GENERATED_PASS roundtrips=10001 bitflips=472 truncations=59\n";
}
