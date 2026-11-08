#pragma once
#include <cstdint>
#include <stdexcept>

namespace oresforge::runtime {

// Trusted host-owned seam for later compiler-generated guest instrumentation.
// NOT a source actor interpreter, a watchdog, or an untrusted OS sandbox.
class CooperationBudget final {
public:
  static constexpr std::uint32_t kPollInterval = 128;
  static constexpr std::uint64_t kMaxFuel = 100'000'000;
  explicit CooperationBudget(std::uint64_t fuel)
      : remaining_fuel_(fuel) {
    if (fuel == 0 || fuel > kMaxFuel)
      throw std::invalid_argument("invalid logical guest fuel");
  }

  CooperationBudget(const CooperationBudget&) = delete;
  CooperationBudget& operator=(const CooperationBudget&) = delete;
  CooperationBudget(CooperationBudget&&) = delete;
  CooperationBudget& operator=(CooperationBudget&&) = delete;

  // Fairness resets per dispatch, but the 128-step polling phase does not.
  // Otherwise 127-step resumptions could evade every cold check.
  void begin_dispatch() noexcept { dispatch_ticks_ = 0; }

  // Cheap path: integer arithmetic and a bitmask; no clock, FFI or scheduler.
  // The compiler must call after committing loop-carried guest state.
  // True indicates a *possible* slow poll; yielding also requires a proven
  // resumable continuation and no live critical/borrow guard.
  bool charge_backedge(std::uint64_t weight = 1) {
    charge_operation(weight);
    ++dispatch_ticks_;
    ticks_ = (ticks_ + 1) & (kPollInterval - 1);
    return ticks_ == 0;
  }

  // Account work at function entry, recursion and expensive basic blocks
  // without creating a new suspension point.
  void charge_operation(std::uint64_t weight) {
    if (weight == 0 || weight > kMaxFuel)
      throw std::invalid_argument("invalid guest work weight");
    if (weight > remaining_fuel_)
      throw std::runtime_error("logical guest fuel exhausted");
    remaining_fuel_ -= weight;
  }

  std::uint64_t remaining_fuel() const noexcept { return remaining_fuel_; }
  std::uint64_t backedges_this_dispatch() const noexcept { return dispatch_ticks_; }

private:
  std::uint64_t remaining_fuel_;
  std::uint32_t ticks_ = 0;
  std::uint64_t dispatch_ticks_ = 0;
};

static_assert((CooperationBudget::kPollInterval &
               (CooperationBudget::kPollInterval - 1)) == 0,
              "poll interval must be a power of two");
} // namespace oresforge::runtime
