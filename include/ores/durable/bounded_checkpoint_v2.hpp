#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <utility>

namespace ores::durable {
// Proposed wire profile, not a general graph serializer. All lengths checked
// before reads or allocations; string is strict UTF-8, bytes are opaque.
class BoundedCheckpointV2 final {
public:
    enum class Kind : unsigned char { Int=1, Bool=2, String=3, Bytes=4 };
    struct Field { std::string name; Kind kind; };
    using Bytes=std::vector<unsigned char>;
    using Value=std::variant<std::int64_t,bool,std::string,Bytes>;
    static constexpr std::size_t max_field_bytes=16384,max_snapshot_bytes=65536;
private:
    std::vector<Field> fields_;
    std::uint64_t fingerprint_=0;
    [[noreturn]] static void deny(){throw std::logic_error("invalid bounded checkpoint v2");}
    static bool name_ok(std::string_view n){
        if(n.empty()||n.size()>128)return false;
        auto alpha=[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';};
        if(!alpha(n.front()))return false;
        for(unsigned char c:n)if(!alpha(c)&&!(c>='0'&&c<='9'))return false;
        return true;
    }
    static void put(std::string& b,std::uint64_t n,unsigned width){
        for(unsigned i=0;i<width;++i)b.push_back(static_cast<char>(static_cast<unsigned char>(n>>(8*i))));
    }
    static std::uint64_t get(std::string_view b,std::size_t& p,std::size_t end,unsigned width){
        if(p>end||width>end-p)deny();
        std::uint64_t n=0;
        for(unsigned i=0;i<width;++i)n|=std::uint64_t(static_cast<unsigned char>(b[p++]))<<(8*i);
        return n;
    }
    static std::uint64_t hash(std::string_view b){
        std::uint64_t h=UINT64_C(0xcbf29ce484222325);
        for(unsigned char c:b){h^=c;h*=UINT64_C(0x100000001b3);}return h;
    }
    static bool utf8(std::string_view s){
        for(std::size_t p=0;p<s.size();){
            unsigned char a=s[p++];if(a<128)continue;
            unsigned n=0;std::uint32_t v=0,min=0;
            if(a>=0xc2&&a<=0xdf){n=1;v=a&31;min=0x80;}
            else if(a>=0xe0&&a<=0xef){n=2;v=a&15;min=0x800;}
            else if(a>=0xf0&&a<=0xf4){n=3;v=a&7;min=0x10000;}
            else return false;
            if(n>s.size()-p)return false;
            for(unsigned i=0;i<n;++i){unsigned char c=s[p++];if((c&0xc0)!=0x80)return false;v=(v<<6)|(c&63);}
            if(v<min||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return false;
        }
        return true;
    }
    static std::int64_t signed_value(std::uint64_t u){
        return u<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())?static_cast<std::int64_t>(u):-1-static_cast<std::int64_t>(~u);
    }
    static void room(std::size_t current,std::size_t more){
        if(current>max_snapshot_bytes-8||more>max_snapshot_bytes-8-current)deny();
    }
public:
    BoundedCheckpointV2(std::string_view actor,std::vector<Field> fields):fields_(std::move(fields)){
        if(!name_ok(actor)||fields_.size()>256)deny();
        std::string sig="atto.actor.state.schema.v2";sig.push_back(0);put(sig,actor.size(),4);sig+=actor;
        for(std::size_t i=0;i<fields_.size();++i){const auto& f=fields_[i];
            if(!name_ok(f.name)||static_cast<unsigned>(f.kind)<1||static_cast<unsigned>(f.kind)>4)deny();
            for(std::size_t j=0;j<i;++j)if(fields_[j].name==f.name)deny();
            put(sig,f.name.size(),4);sig+=f.name;sig.push_back(static_cast<char>(f.kind));
        }fingerprint_=hash(sig);
    }
    BoundedCheckpointV2(const BoundedCheckpointV2&)=default;
    BoundedCheckpointV2(BoundedCheckpointV2&&)=delete;
    BoundedCheckpointV2& operator=(const BoundedCheckpointV2&)=delete;
    BoundedCheckpointV2& operator=(BoundedCheckpointV2&&)=delete;
    std::uint64_t fingerprint()const noexcept{return fingerprint_;}
    std::string encode(const std::vector<Value>& values) const {
        if(values.size()!=fields_.size())deny();
        std::string out="ATTOST02";out.push_back(2);put(out,fingerprint_,8);put(out,fields_.size(),2);
        for(std::size_t i=0;i<fields_.size();++i){auto kind=fields_[i].kind;room(out.size(),1);out.push_back(static_cast<char>(kind));
            if(kind==Kind::Int){auto n=std::get_if<std::int64_t>(&values[i]);if(!n)deny();room(out.size(),8);put(out,static_cast<std::uint64_t>(*n),8);}
            else if(kind==Kind::Bool){auto b=std::get_if<bool>(&values[i]);if(!b)deny();room(out.size(),1);out.push_back(*b?1:0);}
            else if(kind==Kind::String){auto s=std::get_if<std::string>(&values[i]);if(!s||s->size()>max_field_bytes||!utf8(*s))deny();room(out.size(),4+s->size());put(out,s->size(),4);out+=*s;}
            else {auto b=std::get_if<Bytes>(&values[i]);if(!b||b->size()>max_field_bytes)deny();room(out.size(),4+b->size());put(out,b->size(),4);for(auto c:*b)out.push_back(static_cast<char>(c));}
        }
        put(out,hash(out),8);return out;
    }
    std::vector<Value> decode(std::string_view bytes) const {
        if(bytes.size()<27||bytes.size()>max_snapshot_bytes||bytes.substr(0,8)!="ATTOST02"||bytes[8]!=2)deny();
        const std::size_t end=bytes.size()-8;std::size_t tail=end,p=9;
        if(get(bytes,tail,bytes.size(),8)!=hash(bytes.substr(0,end))||get(bytes,p,end,8)!=fingerprint_||get(bytes,p,end,2)!=fields_.size())deny();
        std::vector<Value> out;out.reserve(fields_.size());
        for(auto& f:fields_){
            if(get(bytes,p,end,1)!=static_cast<unsigned>(f.kind))deny();
            if(f.kind==Kind::Int)out.emplace_back(signed_value(get(bytes,p,end,8)));
            else if(f.kind==Kind::Bool){auto b=get(bytes,p,end,1);if(b>1)deny();out.emplace_back(b==1);}
            else {auto n=get(bytes,p,end,4);if(n>max_field_bytes||n>end-p)deny();auto s=bytes.substr(p,static_cast<std::size_t>(n));p+=static_cast<std::size_t>(n);
                if(f.kind==Kind::String){if(!utf8(s))deny();out.emplace_back(std::string(s));}
                else out.emplace_back(Bytes(s.begin(),s.end()));
            }
        }
        if(p!=end)deny();
        return out;
    }
};
} // namespace ores::durable
