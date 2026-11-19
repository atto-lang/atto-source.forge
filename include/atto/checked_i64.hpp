#pragma once

// Portable C++20 checked i64 runtime primitives for Forge experiments.
// These are NOT a .atto compiler or actor runtime.
#include <cstdint>
#include <limits>
#include <optional>

namespace atto::forge {
using i64 = std::int64_t;
using u64 = std::uint64_t;

[[nodiscard]] constexpr std::optional<i64> checked_add(i64 a, i64 b) noexcept {
  constexpr i64 lo = std::numeric_limits<i64>::min();
  constexpr i64 hi = std::numeric_limits<i64>::max();
  if ((b > 0 && a > hi - b) || (b < 0 && a < lo - b))
    return std::nullopt;
  return a + b;
}

[[nodiscard]] constexpr std::optional<i64> checked_sub(i64 a, i64 b) noexcept {
  constexpr i64 lo = std::numeric_limits<i64>::min();
  constexpr i64 hi = std::numeric_limits<i64>::max();
  if ((b > 0 && a < lo + b) || (b < 0 && a > hi + b))
    return std::nullopt;
  return a - b;
}

[[nodiscard]] constexpr u64 magnitude(i64 value) noexcept {
  const u64 bits = static_cast<u64>(value);
  return value < 0 ? (~bits + u64{1}) : bits;
}

[[nodiscard]] constexpr std::optional<i64> checked_mul(i64 a, i64 b) noexcept {
  constexpr i64 lo = std::numeric_limits<i64>::min();
  constexpr u64 positive_limit = static_cast<u64>(std::numeric_limits<i64>::max());
  constexpr u64 negative_limit = positive_limit + u64{1};
  const bool negative = (a < 0) != (b < 0);
  const u64 left = magnitude(a), right = magnitude(b);
  const u64 limit = negative ? negative_limit : positive_limit;
  if (right != 0 && left > limit / right)
    return std::nullopt;
  const u64 product = left * right; // guaranteed <= 2^63
  if (negative && product == negative_limit)
    return lo;
  const i64 value = static_cast<i64>(product); // guaranteed <= INT64_MAX
  return negative ? -value : value;
}
// Truncation toward zero, matching C++ signed division semantics.
// Both division by zero and INT64_MIN / -1 are rejected before evaluation.
[[nodiscard]] constexpr std::optional<i64> checked_div(i64 a, i64 b) noexcept {
  if (b == 0 || (a == std::numeric_limits<i64>::min() && b == -1))
    return std::nullopt;
  return a / b;
}

[[nodiscard]] constexpr std::optional<i64> checked_rem(i64 a, i64 b) noexcept {
  if (b == 0 || (a == std::numeric_limits<i64>::min() && b == -1))
    return std::nullopt;
  return a % b;
}
}  // namespace atto::forge
