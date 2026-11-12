#pragma once
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oresforge::literals {
enum class Kind { Quoted, Template };
struct ScanResult { Kind kind; std::size_t end; };

// Lexical scan only: i64 Forge IR does not yet lower String expressions.
class Scanner {
 public:
  explicit Scanner(std::string_view source) : source_(source) {}
  ScanResult scan(std::size_t start) {
    if (start >= source_.size()) fail("expected quoted literal", start);
    pos_ = start;
    const auto c = source_[pos_];
    if (c == 39 || c == 34) { quoted(); return {Kind::Quoted, pos_}; }
    if (c == 96) { templ(1); return {Kind::Template, pos_}; }
    fail("expected quoted literal", start);
  }
 private:
  std::string_view source_;
  std::size_t pos_ = 0, substitutions_ = 0;
  [[noreturn]] static void fail(const char* message, std::size_t at) {
    throw std::runtime_error(std::string(message) + " at byte " + std::to_string(at));
  }
  static bool line_end(char c) { return c == '\r' || c == '\n'; }
  static bool hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
           (c >= 'a' && c <= 'f');
  }
  void escape(std::size_t opening, bool is_template) {
    if (pos_ >= source_.size()) fail("unterminated literal escape", opening);
    const auto c = source_[pos_++];
    if (c == 'u') {
      for (int i=0; i<4; ++i) {
        if (pos_ >= source_.size() || !hex(source_[pos_]))
          fail("invalid Unicode escape", opening);
        ++pos_;
      }
      return;
    }
    if (!is_template && line_end(c)) {
      if (c == '\r' && pos_ < source_.size() && source_[pos_] == '\n') ++pos_;
      return;
    }
    if (c != 'n' && c != 'r' && c != 't' && c != '\\' && c != '\'' &&
        c != '"' && c != '$' && c != 96)
      fail("invalid source literal escape", opening);
  }
  void quoted() {
    const auto start = pos_;
    const auto quote = source_[pos_++];
    while (pos_ < source_.size()) {
      const auto c = source_[pos_++];
      if (c == '\\') escape(start, false);
      else if (c == quote) return;
      else if (line_end(c)) fail("newline in quoted string", start);
    }
    fail("unterminated quoted string", start);
  }
  void block_comment() {
    const auto start = pos_;
    pos_ += 2;
    std::size_t nesting = 1;
    while (pos_ < source_.size() && nesting) {
      if (source_.substr(pos_,2) == "/*") {
        if (++nesting > 128) fail("block comment nesting too deep", start);
        pos_ += 2;
      } else if (source_.substr(pos_,2) == "*/") {--nesting; pos_ += 2;}
      else ++pos_;
    }
    if (nesting) fail("unterminated block comment", start);
  }
  void interpolation(std::size_t template_depth) {
    const auto start = pos_;
    std::size_t braces = 0;
    bool expression = false;
    while (pos_ < source_.size()) {
      const auto c = source_[pos_];
      if (source_.substr(pos_,2) == "//") {
        pos_ += 2;
        while (pos_ < source_.size() && !line_end(source_[pos_])) ++pos_;
        continue;
      }
      if (source_.substr(pos_,2) == "/*") {block_comment(); continue;}
      if (c == 39 || c == 34) {quoted(); expression=true; continue;}
      if (c == 96) {templ(template_depth + 1); expression=true; continue;}
      if (c == '{') {
        expression=true;
        if (++braces > 256) fail("template interpolation brace limit exceeded", start);
        ++pos_;
        continue;
      }
      if (c == '}') {
        ++pos_;
        if (braces == 0) {
          if (!expression) fail("empty template interpolation", start);
          return;
        }
        --braces;
        continue;
      }
      if (c!=' ' && c!='\t' && c!='\n' && c!='\r' && c!='\v' && c!='\f')
        expression=true;
      ++pos_;
    }
    fail("unterminated template interpolation", start);
  }
  void templ(std::size_t depth) {
    const auto start = pos_;
    if (depth > 32) fail("template nesting limit exceeded", start);
    ++pos_;
    while (pos_ < source_.size()) {
      const auto c = source_[pos_];
      if (c == '\\') {
        ++pos_;
        escape(start, true);
      } else if (c == 96) {++pos_; return;}
      else if (c == '$' && pos_+1 < source_.size() && source_[pos_+1] == '{') {
        if (++substitutions_ > 1024) fail("template substitution limit exceeded", pos_);
        pos_ += 2;
        interpolation(depth);
      } else ++pos_;
    }
    fail("unterminated template string", start);
  }
};
} // namespace oresforge::literals
