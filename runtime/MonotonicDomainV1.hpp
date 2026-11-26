#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace oresforge::runtime {

// 0 and UINT64_MAX are permanently reserved. Reuse storage, never authority.
class MonotonicDomainV1 final {
 public:
  explicit MonotonicDomainV1(std::uint64_t last = 0) noexcept : last_(last) {}
  MonotonicDomainV1(const MonotonicDomainV1&) = delete;
  MonotonicDomainV1& operator=(const MonotonicDomainV1&) = delete;

  [[nodiscard]] std::uint64_t try_issue() noexcept {
    constexpr auto last_valid = std::numeric_limits<std::uint64_t>::max() - 1;
    auto previous = last_.load(std::memory_order_relaxed);
    while (previous < last_valid) {
      const auto next = previous + 1;
      if (last_.compare_exchange_weak(previous, next,
                                     std::memory_order_relaxed)) return next;
    }
    return 0;
  }

 private:
  std::atomic<std::uint64_t> last_;
};

} // namespace oresforge::runtime
