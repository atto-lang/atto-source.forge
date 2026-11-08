#pragma once
#include <cstdint>
#include <limits>

namespace oresforge::runtime {
// Host/verified-IR substrate only. Forge stage-0 emits straight-line trusted
// scalar arithmetic, not untrusted loop/call-cycle checkpoint lowering.
class AmortizedCooperationGate final {
public:
  static constexpr std::uint32_t kPollInterval = 128;
  static constexpr std::uint64_t kFairnessLimit = 512;
  static constexpr std::uint64_t saturating_add(std::uint64_t a,
                                                 std::uint64_t b) noexcept {
    return b > std::numeric_limits<std::uint64_t>::max() - a
      ? std::numeric_limits<std::uint64_t>::max() : a + b;
  }
  void begin_dispatch() noexcept { completed_batches_ = 0; remaining_ = kPollInterval; }
  // The hot path only decrements a 32-bit counter and branches. No clock,
  // scheduler or 64-bit saturating addition runs until the 128th backedge.
  bool completed_backedge_needs_slow_poll() noexcept {
    if (--remaining_ != 0) return false;
    remaining_ = kPollInterval;
    completed_batches_ = saturating_add(completed_batches_, kPollInterval);
    return true;
  }
  std::uint64_t completed_backedges() const noexcept {
    return saturating_add(completed_batches_, kPollInterval - remaining_);
  }
  bool fairness_expired() const noexcept { return completed_backedges() >= kFairnessLimit; }
private:
  std::uint64_t completed_batches_ = 0;
  std::uint32_t remaining_ = kPollInterval;
};
}
