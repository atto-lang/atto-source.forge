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
        // Trojan Source: reject bidi marks/overrides/isolates even in comments.
        // The UTF-8 sequence is already validated above.
        if ((lead == 0xd8 && static_cast<unsigned char>(text[i + 1]) == 0x9c) ||
            (lead == 0xe2 &&
              ((static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                 (static_cast<unsigned char>(text[i + 2]) == 0x8e ||
                  static_cast<unsigned char>(text[i + 2]) == 0x8f ||
                  (static_cast<unsigned char>(text[i + 2]) >= 0xaa &&
                   static_cast<unsigned char>(text[i + 2]) <= 0xae))) ||
               (static_cast<unsigned char>(text[i + 1]) == 0x81 &&
                static_cast<unsigned char>(text[i + 2]) >= 0xa6 &&
                static_cast<unsigned char>(text[i + 2]) <= 0xa9))))
            throw std::runtime_error("source contains bidirectional Unicode control");
    i+=following+1;
  }
}
}
