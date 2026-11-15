#include "atto/checked_i64.hpp"

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>

using atto::forge::checked_add;
using atto::forge::checked_sub;
using atto::forge::checked_mul;
using atto::forge::i64;
constexpr i64 MIN = std::numeric_limits<i64>::min();
constexpr i64 MAX = std::numeric_limits<i64>::max();

static_assert(checked_add(1, 2) == std::optional<i64>{3});
static_assert(!checked_add(MAX, 1));
static_assert(checked_sub(MIN, 0) == std::optional<i64>{MIN});
static_assert(!checked_sub(MIN, 1));
static_assert(checked_mul(MIN, 1) == std::optional<i64>{MIN});
static_assert(!checked_mul(MIN, -1));
static_assert(checked_mul(-3, -7) == std::optional<i64>{21});

void require(bool condition) {
  if (!condition) std::abort();
}

int main() {
  require(!checked_add(MAX, 1));
  require(!checked_add(MIN, -1));
  require(checked_add(MIN, MAX) == std::optional<i64>{-1});
  require(!checked_sub(MAX, -1));
  require(!checked_sub(MIN, 1));
  require(checked_sub(MIN, MIN) == std::optional<i64>{0});
  require(checked_sub(MAX, MIN) == std::nullopt);
  require(checked_mul(0, MIN) == std::optional<i64>{0});
  require(checked_mul(MIN, 1) == std::optional<i64>{MIN});
  require(!checked_mul(MIN, -1));
  require(!checked_mul(MAX, 2));
  require(!checked_mul(MIN, 2));
  require(checked_mul(MAX, -1) == std::optional<i64>{-MAX});
  require(checked_mul(-1, -1) == std::optional<i64>{1});
  require(checked_mul(MIN / 2, 2) == std::optional<i64>{MIN});
  // Exhaustive small-domain arithmetic: oracle operations cannot overflow.
  for (i64 a = -128; a <= 128; ++a) {
    for (i64 b = -128; b <= 128; ++b) {
      require(checked_add(a, b) == std::optional<i64>{a + b});
      require(checked_sub(a, b) == std::optional<i64>{a - b});
      require(checked_mul(a, b) == std::optional<i64>{a * b});
    }
  }
}
