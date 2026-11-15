#pragma once
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

// One source-byte admission rule for all Forge frontends (including callers
// that bypass the CLI). Valid UTF-8 comments are allowed; grammar tokens
// remain ASCII-only. Reject NUL, overlong encodings, surrogates and >U+10FFFF.
namespace oresforge::source {
// Preserve the existing error category while identifying the exact failing
// byte. No source excerpt is included: untrusted text may contain controls.
inline std::runtime_error invalid_source_byte(const char* description, std::size_t offset) {
  return std::runtime_error(std::string(description) + " at byte offset " +
                            std::to_string(offset));
}

inline void validate_utf8(std::string_view text) {
  for (std::size_t i=0; i<text.size();) {
    const auto lead=static_cast<unsigned char>(text[i]);
    if (lead==0) throw invalid_source_byte("source contains NUL byte", i);
    // Raw terminal controls must never disappear inside ignored comments/literals.
    if (lead==0x1b || lead==0x7f)
      throw invalid_source_byte("source contains terminal control", i);
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
    } else throw invalid_source_byte("source is not valid UTF-8", i);
    if (text.size()-i<=following)
      throw invalid_source_byte("source is not valid UTF-8", i);
    for (std::size_t j=1; j<=following; ++j) {
      const auto b=static_cast<unsigned char>(text[i+j]);
      if (b<(j==1?low:0x80) || b>(j==1?high:0xbf))
        throw invalid_source_byte("source is not valid UTF-8", i + j);
    }
    i+=following+1;
  }
}
}
