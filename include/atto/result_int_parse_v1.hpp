#pragma once
// HOST_ONLY foundation: scoped Result<int,Error> parser; not Atto guest codegen.
#include "atto/checked_i64.hpp"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace atto::forge::result_v1 {
inline constexpr std::size_t max_parse_bytes = 1024;
inline constexpr std::string_view invalid_integer = "invalid integer";
enum class Tag : std::uint32_t { ok = 0, err = 1 };
struct Error {
  std::uint64_t tenant_id = 0;
  std::uint64_t identity = 0;
  std::string_view message{};
};
struct Result {
  Tag tag = Tag::ok;
  i64 value = 0;
  Error error{};
  [[nodiscard]] constexpr bool is_ok() const noexcept { return tag == Tag::ok; }
  [[nodiscard]] constexpr bool is_err() const noexcept { return tag == Tag::err; }
};
class TenantParser {
  std::uint64_t tenant_id_;
  std::uint64_t next_error_ = 0;
  [[nodiscard]] Result invalid() {
    if (next_error_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("E_ERROR_ID_EXHAUSTED");
    return {Tag::err, 0, {tenant_id_, ++next_error_, invalid_integer}};
  }
 public:
  explicit TenantParser(std::uint64_t tenant_id) : tenant_id_(tenant_id) {
    if (!tenant_id) throw std::invalid_argument("E_TENANT_ID");
  }
  [[nodiscard]] constexpr std::uint64_t tenant_id() const noexcept { return tenant_id_; }
  [[nodiscard]] Result parse(std::string_view text) {
    if (text.empty() || text.size() > max_parse_bytes) return invalid();
    std::size_t at = 0;
    bool negative = false;
    if (text[0] == '-' || text[0] == '+') {
      negative = text[0] == '-';
      at = 1;
    }
    if (at == text.size()) return invalid();
    const i64 limit = negative ? std::numeric_limits<i64>::min()
                               : -std::numeric_limits<i64>::max();
    const i64 cutoff = limit / i64{10};
    i64 result = 0;
    for (; at < text.size(); ++at) {
      const unsigned char c = static_cast<unsigned char>(text[at]);
      if (c < '0' || c > '9') return invalid();
      const i64 digit = static_cast<i64>(c - '0');
      if (result < cutoff) return invalid();
      result *= 10;
      if (result < limit + digit) return invalid();
      result -= digit;
    }
    return {Tag::ok, negative ? result : -result, {}};
  }
  [[nodiscard]] bool can_observe(const Error& error) const noexcept {
    return error.tenant_id == tenant_id_ && error.identity != 0;
  }
};
} // namespace atto::forge::result_v1
