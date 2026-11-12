#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oresforge::source_syntax {
// Lexical boundary only. Typed String/template IR and guest execution remain
// unsupported in the Forge i64 bootstrap. No source expression is evaluated.
inline bool quote(char c) {
  return c=='\'' || c=='"' || c==static_cast<char>(0x60);
}
inline bool hex(char c) {
  return (c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F');
}
inline unsigned digit(char c) {
  if (c>='0' && c<='9') return static_cast<unsigned>(c-'0');
  if (c>='A' && c<='F') return static_cast<unsigned>(c-'A'+10);
  return static_cast<unsigned>(c-'a'+10);
}
inline std::size_t line_bytes(std::string_view s,std::size_t i) {
  if (i>=s.size()) return 0;
  if (s[i]=='\r') return i+1<s.size() && s[i+1]=='\n' ? 2 : 1;
  if (s[i]=='\n') return 1;
  if (i+1<s.size() && static_cast<unsigned char>(s[i])==0xc2 &&
      static_cast<unsigned char>(s[i+1])==0x85) return 2;
  if (i+2<s.size() && static_cast<unsigned char>(s[i])==0xe2 &&
      static_cast<unsigned char>(s[i+1])==0x80 &&
      (static_cast<unsigned char>(s[i+2])==0xa8 ||
       static_cast<unsigned char>(s[i+2])==0xa9)) return 3;
  return 0;
}
[[noreturn]] inline void fail(const char* reason,std::size_t at) {
  throw std::runtime_error(std::string(reason)+" at byte "+std::to_string(at));
}
inline std::size_t interpolation(std::string_view s,std::size_t at,
                                 unsigned depth,unsigned& substitutions);
inline std::size_t quoted(std::string_view s,std::size_t at,
                          unsigned depth,unsigned& substitutions) {
  if (depth>64) fail("string/template nesting limit",at);
  if (at>=s.size() || !quote(s[at])) fail("expected quote",at);
  const auto opener=s[at];
  std::size_t i=at+1;
  while(i<s.size()) {
    if(s[i]=='\\') {
      const auto start=i++;
      if(i>=s.size()) fail("unterminated string escape",start);
      const auto newline=line_bytes(s,i);
      if(newline && opener!=static_cast<char>(0x60)) {i+=newline;continue;}
      if(s[i]=='u') {
        ++i;
        auto decode4 = [&](std::size_t at)->std::uint32_t {
          if(at+4>s.size()) fail("invalid unicode escape",start);
          std::uint32_t v=0;
          for(unsigned n=0;n<4;++n) {
            if(!hex(s[at+n])) fail("invalid unicode escape",start);
            v=v*16+digit(s[at+n]);
          }
          return v;
        };
        const auto scalar=decode4(i);
        i+=4;
        if(scalar>=0xd800 && scalar<=0xdbff) {
          if(i+6>s.size() || s[i]!='\\' || s[i+1]!='u')
            fail("invalid unicode escape",start);
          const auto tail=decode4(i+2);
          if(tail<0xdc00 || tail>0xdfff) fail("invalid unicode escape",start);
          i+=6;
        } else if(scalar>=0xdc00 && scalar<=0xdfff)
          fail("invalid unicode escape",start);
        continue;
      }
      if(opener==static_cast<char>(0x60) &&
         (newline || (std::string_view("\\'\"$nrt").find(s[i])==std::string_view::npos &&
          s[i]!=static_cast<char>(0x60))))
        fail("invalid template escape",start);
      ++i;
      continue;
    }
    if(s[i]==opener) return i+1;
    // Java/Graal ordinary strings preserve raw multiline content.
    if(opener==static_cast<char>(0x60) && s[i]=='$' &&
       i+1<s.size() && s[i+1]=='{') {
      if(++substitutions>128) fail("template substitution limit",i);
      const auto body=i+2;
      const auto end=interpolation(s,body,depth+1,substitutions);
      bool populated=false;
      for(auto j=body;j<end-1;++j) {
        const auto c=static_cast<unsigned char>(s[j]);
        if(c!=' ' && c!='\t' && c!='\r' && c!='\n') {populated=true;break;}
      }
      if(!populated) fail("empty template interpolation",i);
      i=end;
      continue;
    }
    ++i;
  }
  fail("unterminated string/template",at);
}
inline std::size_t interpolation(std::string_view s,std::size_t at,
                                 unsigned depth,unsigned& substitutions) {
  if(depth>64) fail("template nesting limit",at);
  unsigned braces=1;
  bool has_code=false;
  std::size_t i=at;
  while(i<s.size()) {
    if(i+1<s.size() && s[i]=='/' && s[i+1]=='/') {
      i+=2;
      while(i<s.size() && !line_bytes(s,i)) ++i;
      continue;
    }
    if(i+1<s.size() && s[i]=='/' && s[i+1]=='*') {
      const auto start=i;
      i+=2;
      unsigned comments=1;
      while(i<s.size() && comments) {
        if(i+1<s.size() && s[i]=='/' && s[i+1]=='*') {
          if(++comments>64) fail("interpolation comment nesting limit",i);
          i+=2;
        } else if(i+1<s.size() && s[i]=='*' && s[i+1]=='/') {
          --comments;i+=2;
        } else ++i;
      }
      if(comments) fail("unterminated interpolation comment",start);
      continue;
    }
    if(quote(s[i])) {has_code=true;i=quoted(s,i,depth+1,substitutions);continue;}
    if(s[i]=='{') {has_code=true;if(++braces>128)fail("interpolation brace limit",i);}
    else if(s[i]=='}') {
      if(--braces==0) {
        if(!has_code) fail("empty template interpolation",at-2);
        return i+1;
      }
      has_code=true;
    } else if(line_bytes(s,i)) {i+=line_bytes(s,i);continue;}
    else if(s[i]!=' ' && s[i]!='\t' && s[i]!='\v' && s[i]!='\f') has_code=true;
    ++i;
  }
  fail("unterminated template interpolation",at-2);
}
inline std::size_t scan_quoted(std::string_view source,std::size_t start) {
  unsigned substitutions=0;
  return quoted(source,start,0,substitutions);
}
} // namespace oresforge::source_syntax
