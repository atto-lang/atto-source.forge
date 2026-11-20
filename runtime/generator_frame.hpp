#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace oresforge::runtime {

class GeneratorFrameV1 {
 public:
  static constexpr std::size_t kMaxYields = 4096;

  // 1 = initialized, 0 = already initialized/terminal.
  int initialize() noexcept;

  // 1 = yielded slot, 0 = sticky DONE, -1 = invalid activation/budget.
  int pull(std::size_t yield_count, std::size_t& slot) noexcept;

  // 1 = active frame closed, 0 = already terminal, -1 = fresh/uninitialized.
  int close() noexcept;

  [[nodiscard]] bool terminal() const noexcept { return state_ == kTerminal; }

 private:
  static constexpr std::uint32_t kFresh = 0;
  static constexpr std::uint32_t kActive = 1;
  static constexpr std::uint32_t kTerminal = 2;

  std::uint32_t state_ = kFresh;
  std::size_t next_slot_ = 0;
};

}  // namespace oresforge::runtime
