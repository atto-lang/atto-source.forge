#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace oresforge::runtime::detail {
// Conservative native-address test for trusted host byte spans. Invalid or
// overflowing pointer intervals are treated as unsafe, not as disjoint.
// Only supported where uintptr_t can represent the native address space.
inline bool overlaps_or_invalid(const void* first, std::size_t first_size,
                                const void* second, std::size_t second_size) noexcept {
  if (first_size == 0 || second_size == 0) return false;
  if (first == nullptr || second == nullptr) return true;
  const auto a = reinterpret_cast<std::uintptr_t>(first);
  const auto b = reinterpret_cast<std::uintptr_t>(second);
  constexpr auto limit = std::numeric_limits<std::uintptr_t>::max();
  if (first_size > limit - a || second_size > limit - b) return true;
  return a < b + second_size && b < a + first_size;
}
} // namespace oresforge::runtime::detail
