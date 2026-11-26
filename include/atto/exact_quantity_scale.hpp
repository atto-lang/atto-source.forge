#pragma once
// Fixed-size unsigned integer used ONLY for bounded compile-time unit scales.
// 256-bit stored components; 512-bit intermediates. No external dependency.
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
namespace atto::forge::scalar {
struct ScaleUInt {
  std::array<std::uint32_t,16> words{};
  constexpr ScaleUInt(std::uint64_t n=0) {words[0]=static_cast<std::uint32_t>(n);words[1]=static_cast<std::uint32_t>(n>>32);}
  bool operator==(const ScaleUInt&) const=default;
  [[nodiscard]] bool zero()const{return *this==ScaleUInt{};}
  [[nodiscard]] bool fits256()const{for(std::size_t i=8;i<16;++i)if(words[i])return false;return true;}
  [[nodiscard]] int cmp(const ScaleUInt& b)const{for(std::size_t i=16;i-->0;){if(words[i]!=b.words[i])return words[i]<b.words[i]?-1:1;}return 0;}
  [[nodiscard]] std::uint64_t low64()const{return std::uint64_t(words[0])|(std::uint64_t(words[1])<<32);}
  void sub(const ScaleUInt& b){std::uint64_t borrow=0;for(std::size_t i=0;i<16;++i){auto v=std::uint64_t(words[i]);auto w=std::uint64_t(b.words[i])+borrow;words[i]=static_cast<std::uint32_t>(v-w);borrow=v<w;}}
  void shift(){std::uint32_t carry=0;for(auto& w:words){auto c=w>>31;w=(w<<1)|carry;carry=c;}}
  [[nodiscard]] static ScaleUInt mul256(const ScaleUInt&a,const ScaleUInt&b){
    ScaleUInt out;for(std::size_t i=0;i<8;++i){std::uint64_t carry=0;for(std::size_t j=0;j<8;++j){auto v=std::uint64_t(a.words[i])*b.words[j]+out.words[i+j]+carry;out.words[i+j]=static_cast<std::uint32_t>(v);carry=v>>32;}out.words[i+8]=static_cast<std::uint32_t>(carry);}return out;
  }
  // Long division; the carry bit handles a 513-bit shifted remainder.
  [[nodiscard]] static std::pair<ScaleUInt,ScaleUInt> divmod(const ScaleUInt&a,const ScaleUInt&b){
    ScaleUInt q,r;for(std::size_t i=512;i-->0;){bool carry=(r.words[15]>>31)!=0;r.shift();r.words[0]|=(a.words[i/32]>>(i%32))&1u;if(carry||r.cmp(b)>=0){r.sub(b);q.words[i/32]|=std::uint32_t(1)<<(i%32);}}return {q,r};
  }
  [[nodiscard]] static ScaleUInt gcd(ScaleUInt a,ScaleUInt b){while(!b.zero()){auto r=divmod(a,b).second;a=b;b=r;}return a;}
};
struct ExactScale {
  ScaleUInt n{1},d{1};
  bool operator==(const ExactScale&)const=default;
  [[nodiscard]] ExactScale inv()const{return {d,n};}
  [[nodiscard]] std::optional<ExactScale> mul(ExactScale rhs)const{
    auto a=ScaleUInt::mul256(n,rhs.n),b=ScaleUInt::mul256(d,rhs.d);
    auto g=ScaleUInt::gcd(a,b);
    a=ScaleUInt::divmod(a,g).first;b=ScaleUInt::divmod(b,g).first;
    if(!a.fits256()||!b.fits256())return std::nullopt;
    return ExactScale{a,b};
  }
  enum class Error {none,inexact,range};
  struct Conversion {std::int64_t value;Error error;};
  [[nodiscard]] Conversion convert(std::int64_t value,ExactScale target)const{
    if(value==0)return {0,Error::none};
    auto a=ScaleUInt::mul256(n,target.d),b=ScaleUInt::mul256(d,target.n);
    auto g=ScaleUInt::gcd(a,b);a=ScaleUInt::divmod(a,g).first;b=ScaleUInt::divmod(b,g).first;
    const bool neg=value<0;
    const auto magnitude=neg?std::uint64_t(0)-static_cast<std::uint64_t>(value):static_cast<std::uint64_t>(value);
    auto [q,r]=ScaleUInt::divmod(ScaleUInt(magnitude),b);
    if(!r.zero())return {0,Error::inexact};
    const std::uint64_t limit=neg?(std::uint64_t(1)<<63):std::uint64_t(std::numeric_limits<std::int64_t>::max());
    if(a.cmp(ScaleUInt(limit))>0||q.cmp(ScaleUInt(limit/a.low64()))>0)return {0,Error::range};
    auto result=q.low64()*a.low64();
    if(neg&&result==(std::uint64_t(1)<<63))return {std::numeric_limits<std::int64_t>::min(),Error::none};
    return {neg?-static_cast<std::int64_t>(result):static_cast<std::int64_t>(result),Error::none};
  }
};
}
