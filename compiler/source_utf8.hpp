#pragma once
#include <cstddef>
#include <stdexcept>
#include <string_view>

// One source-byte admission rule for all Forge frontends (including callers
// that bypass the CLI). Valid UTF-8 comments are allowed; grammar tokens
// remain ASCII-only. Reject NUL, overlong encodings, surrogates and >U+10FFFF.
namespace oresforge::source {
inline void validate_utf8(std::string_view text) {
  for (std::size_t i=0; i<text.size();) {
    const auto lead=static_cast<unsigned char>(text[i]);
    if (lead==0) throw std::runtime_error("source contains NUL byte");
    if (lead==0x1b || lead==0x7f)
      throw std::runtime_error("source contains terminal control byte");
    if (lead<=0x7f) { ++i; continue; }
    std::size_t following=0;
    unsigned char low=0x80, high=0xbf;
    if (lead>=0xc2 && lead<=0xdf) following=1;
    else if (lead>=0xe0 && lead<=0xef) {
      following=2;
      if (lead==0xe0) low=0xa0;
      if (lead==0xed) high=0x9f;
    } else if (lead>=0xf0 && lead<=0xf4) {
      following=3;
      if (lead==0xf0) low=0x90;
      if (lead==0xf4) high=0x8f;
    } else throw std::runtime_error("source is not valid UTF-8");
    if (text.size()-i<=following)
      throw std::runtime_error("source is not valid UTF-8");
    for (std::size_t j=1; j<=following; ++j) {
      const auto b=static_cast<unsigned char>(text[i+j]);
      if (b<(j==1?low:0x80) || b>(j==1?high:0xbf))
        throw std::runtime_error("source is not valid UTF-8");
    }
    i+=following+1;
  }
}
}
