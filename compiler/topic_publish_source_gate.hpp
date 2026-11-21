#pragma once
// Source-only admission fence for unlowered AttoLang Topic<T>.try_publish.
// This is NOT a typed receiver resolver or an implementation of pub/sub.
// It runs after source byte/size validation and BEFORE native IR emission.
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oresforge::source {

inline void reject_unlowered_topic_publish(std::string_view source) {
  constexpr std::size_t kMaxCommentDepth=128;
  auto is_start=[](unsigned char c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
  };
  auto is_part=[&](unsigned char c) {
    return is_start(c)||(c>='0'&&c<='9');
  };
  auto is_line_end=[&](std::size_t i) {
    if(i>=source.size()) return true;
    const unsigned char c=static_cast<unsigned char>(source[i]);
    return c=='\n'||c=='\r'||
      (source.substr(i,2)=="\xC2\x85") ||
      (source.substr(i,3)=="\xE2\x80\xA8") ||
      (source.substr(i,3)=="\xE2\x80\xA9");
  };
  auto ascii_space=[](unsigned char c) {
    return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f';
  };
  bool previous_dot=false;
  for(std::size_t i=0;i<source.size();) {
    const unsigned char c=static_cast<unsigned char>(source[i]);
    if(ascii_space(c)) {++i;continue;}
    if(source.substr(i,2)=="\xC2\x85") {i+=2;continue;}
    if(source.substr(i,3)=="\xE2\x80\xA8" ||
       source.substr(i,3)=="\xE2\x80\xA9") {i+=3;continue;}
    if(source.substr(i,2)=="//") {
      i+=2;
      while(i<source.size()&&!is_line_end(i)) ++i;
      continue;
    }
    if(source.substr(i,2)=="/*") {
      const std::size_t start=i;
      i+=2;
      std::size_t depth=1;
      while(i<source.size()&&depth!=0) {
        if(source.substr(i,2)=="/*") {
          if(++depth>kMaxCommentDepth)
            throw std::runtime_error("TOPIC_PUBLISH_SOURCE_COMMENT_DEPTH at byte "+std::to_string(start));
          i+=2;
        }else if(source.substr(i,2)=="*/") {--depth;i+=2;}
        else ++i;
      }
      if(depth!=0)throw std::runtime_error("TOPIC_PUBLISH_SOURCE_UNTERMINATED_COMMENT at byte "+std::to_string(start));
      continue;
    }
    if(c=='\''||c=='"'||c==0x60) {
      const std::size_t start=i;
      const unsigned char quote=c;
      ++i;
      bool closed=false;
      while(i<source.size()) {
        const unsigned char next=static_cast<unsigned char>(source[i++]);
        if(next=='\\') {
          if(i<source.size()) ++i;
          continue;
        }
        if(next==quote) {closed=true;break;}
      }
      if(!closed)throw std::runtime_error("TOPIC_PUBLISH_SOURCE_UNTERMINATED_STRING at byte "+std::to_string(start));
      previous_dot=false;
      continue;
    }
    if(is_start(c)) {
      const std::size_t start=i++;
      while(i<source.size()&&is_part(static_cast<unsigned char>(source[i])))++i;
      if(previous_dot && source.substr(start,i-start)=="try_publish")
        throw std::runtime_error("TOPIC_PUBLISH_BACKEND_UNSUPPORTED: "+
          std::string("typed Topic.try_publish requires actor authority, share! and Result receipt lowering at byte ")+
          std::to_string(start));
      previous_dot=false;
      continue;
    }
    previous_dot=c=='.';
    ++i;
  }
}

} // namespace oresforge::source
