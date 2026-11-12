#pragma once
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oresforge::literals {
enum class Kind { Quoted, Template };
struct ScanResult { Kind kind; std::size_t end; };

// Lexical recognition only: the stage-0 i64 IR does NOT lower String values.
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
    throw std::runtime_error(std::string(message)+" at byte "+std::to_string(at));
  }
  static bool line_end(char c) { return c == '\r' || c == '\n'; }
  void quoted() {
    const auto start = pos_;
    const auto quote = source_[pos_++];
    while (pos_ < source_.size()) {
      const auto c = source_[pos_++];
      if (c == '\\') {
        if (pos_ == source_.size()) fail("unterminated quoted string", start);
        ++pos_;
      } else if (c == quote) return;
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
    while (pos_ < source_.size()) {
      const auto c = source_[pos_];
      if (source_.substr(pos_,2) == "//") {
        pos_ += 2;
        while (pos_ < source_.size() && !line_end(source_[pos_])) ++pos_;
        continue;
      }
      if (source_.substr(pos_,2) == "/*") {block_comment(); continue;}
      if (c == 39 || c == 34) {quoted(); continue;}
      if (c == 96) {templ(template_depth + 1); continue;}
      if (c == '{') {
        if (++braces > 256) fail("template interpolation brace limit exceeded", start);
        ++pos_;
        continue;
      }
      if (c == '}') {
        ++pos_;
        if (braces == 0) return;
        --braces;
        continue;
      }
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
        if (pos_ + 1 >= source_.size()) fail("unterminated template string", start);
        pos_ += 2;
      } else if (c == 96) { ++pos_; return; }
      else if (c == '$' && pos_ + 1 < source_.size() && source_[pos_+1] == '{') {
        if (++substitutions_ > 1024) fail("template substitution limit exceeded", pos_);
        pos_ += 2;
        interpolation(depth);
      } else ++pos_;
    }
    fail("unterminated template string", start);
  }
};
} // namespace oresforge::literals
