#pragma once
#include <cstdint>
#include <limits>

namespace oresforge::runtime {
// Native host/IR substrate only: Forge stage-0 emits straight-line trusted
// scalar arithmetic. Compiled untrusted actor loops are NOT admitted yet.
// Checkpoints require a verified safe continuation before yielding.
class AmortizedCooperationGate final {
public:
  static constexpr std::uint32_t kPollInterval = 128;
  static constexpr std::uint64_t kFairnessLimit = 512;
  void begin_dispatch() noexcept { total_ = 0; window_ = 0; }
  bool completed_backedge_needs_slow_poll() noexcept {
    if (total_ != std::numeric_limits<std::uint64_t>::max()) ++total_;
    if (++window_ != kPollInterval) return false;
    window_ = 0;
    return true;
  }
  std::uint64_t completed_backedges() const noexcept { return total_; }
  bool fairness_expired() const noexcept { return total_ >= kFairnessLimit; }
private:
  std::uint64_t total_ = 0;
  std::uint32_t window_ = 0;
};
}
