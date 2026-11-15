#include "compiler/source_utf8.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

using oresforge::source::validate_utf8;

static void reject(const std::string& bytes, std::string_view expected) {
  try {
    validate_utf8(bytes);
  } catch (const std::runtime_error& error) {
    if (std::string_view(error.what()) != expected)
      throw std::runtime_error("unexpected admission error: " + std::string(error.what()));
    return;
  }
  throw std::runtime_error("invalid source was admitted");
}

int main() {
  validate_utf8("");
  validate_utf8("pub fnc main(): int { return 42; }");
  validate_utf8("/* café 😀 */");
  // Shared source-input-admission-v1 boundary vectors. Host-only.
  validate_utf8(std::string("\\xF4\\x8F\\xBF\\xBF", 4)); // U+10FFFF
  validate_utf8(std::string("e\\xCC\\x81", 3)); // combining mark
  reject(std::string("\\0", 1), "source contains NUL byte at byte offset 0");
  reject(std::string("\\"a\\0\\"", 4),
         "source contains NUL byte at byte offset 2");
  reject(std::string("/* café */\\0", 13),
         "source contains NUL byte at byte offset 12");
  reject(std::string("\\xE0\\x80\\x80", 3),
         "source is not valid UTF-8 at byte offset 1");
  reject(std::string("\\xC0\\xAF", 2),
         "source is not valid UTF-8 at byte offset 0");
  reject(std::string("a\\x80", 2),
         "source is not valid UTF-8 at byte offset 1");
  reject(std::string("x\\xF0\\x90\\x80", 4),
         "source is not valid UTF-8 at byte offset 1");

  reject(std::string("a\n/*\0*/", 7),
         "source contains NUL byte at byte offset 4");
  reject(std::string("\xC0\x80", 2),
         "source is not valid UTF-8 at byte offset 0"); // overlong
  reject(std::string("\xED\xA0\x80", 3),
         "source is not valid UTF-8 at byte offset 1"); // encoded surrogate
  reject(std::string("\xF4\x90\x80\x80", 4),
         "source is not valid UTF-8 at byte offset 1"); // > U+10FFFF
  reject(std::string("ok\xE2\x82", 4),
         "source is not valid UTF-8 at byte offset 2"); // truncation
  reject(std::string("x\xE2\x28\xA1", 4),
         "source is not valid UTF-8 at byte offset 2"); // bad continuation
  reject(std::string("x\x80", 2),
         "source is not valid UTF-8 at byte offset 1"); // stray continuation
  return 0;
}
