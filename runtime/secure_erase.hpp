#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace oresforge::runtime {
// Scrub tenant-owned bytes on retirement and abnormal owner destruction.
// Ordinary std::fill/memset may be removed as a dead store by an optimizer.
// Volatile writes and compiler signal fences keep the stores observable;
// this does NOT stop a compromised process from having read the bytes earlier.
inline void secure_erase(std::span<std::uint8_t> bytes) noexcept {
  std::atomic_signal_fence(std::memory_order_seq_cst);
  volatile std::uint8_t* const data = bytes.data();
  for (std::size_t i = 0; i < bytes.size(); ++i) data[i] = 0;
  std::atomic_signal_fence(std::memory_order_seq_cst);
}
} // namespace oresforge::runtime
