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
      if(newline) { i+=newline;continue; }
      if(s[i]=='u') {
        ++i;
        if(i<s.size() && s[i]=='{') {
          const auto digits=++i;
          std::uint32_t scalar=0;
          while(i<s.size() && hex(s[i]) && i-digits<7) {
            scalar=scalar*16+digit(s[i]);
            ++i;
          }
          if(digits==i || i-digits>6 || i>=s.size() || s[i]!='}' ||
             scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff))
            fail("invalid unicode escape",start);
          ++i;
        } else {
          for(unsigned n=0;n<4;++n) {
            if(i>=s.size() || !hex(s[i])) fail("invalid unicode escape",start);
            ++i;
          }
        }
        continue;
      }
      if(s[i]=='x') {
        ++i;
        for(unsigned n=0;n<2;++n) {
          if(i>=s.size() || !hex(s[i])) fail("invalid hex escape",start);
          ++i;
        }
        continue;
      }
      if(std::string_view("\\'\"$0bfnrtv").find(s[i])==std::string_view::npos &&
         s[i]!=static_cast<char>(0x60)) fail("invalid string escape",start);
      ++i;
      continue;
    }
    if(s[i]==opener) return i+1;
    if(opener!=static_cast<char>(0x60) && line_bytes(s,i))
      fail("newline in quoted string",i);
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
    if(quote(s[i])) {i=quoted(s,i,depth+1,substitutions);continue;}
    if(s[i]=='{') {if(++braces>128)fail("interpolation brace limit",i);}
    else if(s[i]=='}' && --braces==0) return i+1;
    ++i;
  }
  fail("unterminated template interpolation",at-2);
}
inline std::size_t scan_quoted(std::string_view source,std::size_t start) {
  unsigned substitutions=0;
  return quoted(source,start,0,substitutions);
}
} // namespace oresforge::source_syntax
