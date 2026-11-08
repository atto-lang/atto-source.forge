#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>

namespace oresforge::runtime {
// Host-only contract fixture for bounded compiler-proven work units, not an
// untrusted guest sandbox. Opaque calls must be bounded or isolated separately.
class BatchedCheckpointV1 final {
 public:
  using Poll = std::function<void(std::uint64_t charged, std::uint64_t remaining)>;
  BatchedCheckpointV1(std::uint64_t fuel, Poll poll, std::uint32_t interval = 128)
      : interval_(interval), fuel_(fuel), next_(std::min<std::uint64_t>(interval, fuel)), poll_(std::move(poll)) {
    if (interval != 64 && interval != 128 && interval != 256 && interval != 512)
      throw std::invalid_argument("checkpoint interval must be power-of-two 64..512");
    if (!poll_) throw std::invalid_argument("trusted checkpoint poll required");
  }
  BatchedCheckpointV1(const BatchedCheckpointV1&) = delete;
  BatchedCheckpointV1& operator=(const BatchedCheckpointV1&) = delete;
  BatchedCheckpointV1(BatchedCheckpointV1&&) = delete;
  BatchedCheckpointV1& operator=(BatchedCheckpointV1&&) = delete;

  // Call AFTER committed bounded source work, before any next work unit.
  // No clock or callback runs in the fast path; hard fuel persists across turns.
  bool completedWorkUnit() {
    if (terminal_ || inPoll_) throw std::logic_error("checkpoint terminal or reentrant");
    if (fuel_ == pending_) {
      terminal_ = true;
      throw std::runtime_error("hard execution fuel exhausted");
    }
    ++pending_;  // 1..512, so never overflows even at UINT64_MAX fuel.
    if (pending_ < next_) return false;
    const auto charge = pending_;
    fuel_ -= charge;
    pending_ = 0;
    next_ = std::min<std::uint64_t>(interval_, fuel_);
    inPoll_ = true;
    try { poll_(charge, fuel_); }
    catch (...) { terminal_ = true; inPoll_ = false; throw; }
    inPoll_ = false;
    return true;
  }

  std::uint64_t remainingFuel() const noexcept { return fuel_ - pending_; }

 private:
  std::uint32_t interval_;
  std::uint64_t fuel_;
  std::uint64_t pending_ = 0;
  std::uint64_t next_;
  Poll poll_;
  bool inPoll_ = false;
  bool terminal_ = false;
};
} // namespace oresforge::runtime
