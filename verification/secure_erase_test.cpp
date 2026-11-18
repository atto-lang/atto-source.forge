#include "secure_erase.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>

int main() {
  using oresforge::runtime::secure_erase;
  static_assert(noexcept(secure_erase(std::span<std::uint8_t>{})));
  std::array<std::uint8_t, 257> bytes{};
  bytes.fill(0xA5);
  secure_erase(bytes);
  for (auto b : bytes) assert(b == 0);
  bytes.fill(0x6C);
  secure_erase(std::span<std::uint8_t>{bytes}.subspan(5, 30));
  for (std::size_t i = 0; i < bytes.size(); ++i)
    assert(bytes[i] == (i >= 5 && i < 35 ? 0 : 0x6C));
  secure_erase(std::span<std::uint8_t>{bytes}.subspan(0, 0));
  assert(bytes[0] == 0x6C);
}
