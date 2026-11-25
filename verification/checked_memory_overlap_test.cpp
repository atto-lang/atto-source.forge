#include "runtime/checked_memory_overlap.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

int main() {
  using oresforge::runtime::detail::overlaps_or_invalid;
  std::array<std::uint8_t, 128> a{};
  assert(!overlaps_or_invalid(a.data(), 64, a.data()+64, 64));
  assert(overlaps_or_invalid(a.data(), 65, a.data()+64, 64));
  assert(overlaps_or_invalid(a.data()+64, 64, a.data(), 65));
  assert(overlaps_or_invalid(a.data(), 128, a.data(), 128));
  assert(overlaps_or_invalid(a.data()+10, 1, a.data(), 128));
  assert(!overlaps_or_invalid(nullptr, 0, a.data(), 128));
  assert(!overlaps_or_invalid(a.data(), 128, nullptr, 0));
  assert(overlaps_or_invalid(nullptr, 1, a.data(), 128));
  assert(overlaps_or_invalid(a.data(), 128, nullptr, 1));
  const auto near_end = reinterpret_cast<void*>(
    std::numeric_limits<std::uintptr_t>::max() - 4);
  assert(overlaps_or_invalid(near_end, 12, a.data(), 1));
  assert(overlaps_or_invalid(a.data(), 1, near_end, 12));
}
