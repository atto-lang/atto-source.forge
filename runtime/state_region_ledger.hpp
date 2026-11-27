#pragma once
#include "runtime/state_region_provenance.hpp"
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace oresforge::runtime {

// Host-only scalar-region admission. The owning scheduler must serialize all
// calls, authenticate the current execution and mint strictly increasing loan
// IDs per region incarnation. No guest code, raw state pointers, or wait queue
// are exposed. A separate physical-turn fence is required for memory reclaim.
// This is not compiler lowering or concurrent actor scheduling.
template<std::size_t Capacity = 64>
class StateRegionLoanLedger final {
  static_assert(Capacity > 0 && Capacity <= 64);
  using Receipt = StateRegionLoanProvenance;
  using Access = StateRegionAccess;
  const std::uint64_t region_, generation_, domain_;
  const std::string place_;
  std::array<Receipt, Capacity> active_{};
  std::uint64_t last_id_ = 0;
  std::size_t readers_ = 0;
  bool writer_ = false;
  bool retired_ = false;

  [[nodiscard]] bool same_region(const Receipt& r) const noexcept {
    return r.region_id == region_ && r.region_generation == generation_ &&
           r.runtime_domain == domain_ && r.place == place_;
  }

 public:
  StateRegionLoanLedger(std::uint64_t region, std::uint64_t generation,
                        std::uint64_t domain, std::string_view place)
      : region_(region), generation_(generation), domain_(domain), place_(place) {
    if (!region_ || !generation_ || !domain_ || !state_region_place_valid(place_))
      throw std::invalid_argument("invalid state region identity");
  }
  StateRegionLoanLedger(const StateRegionLoanLedger&) = delete;
  StateRegionLoanLedger& operator=(const StateRegionLoanLedger&) = delete;
  StateRegionLoanLedger(StateRegionLoanLedger&&) = delete;
  StateRegionLoanLedger& operator=(StateRegionLoanLedger&&) = delete;

  [[nodiscard]] bool try_borrow(const Receipt& r) noexcept {
    if (retired_ || !r.valid() || r.action != StateRegionAction::borrow ||
        !same_region(r) || r.loan_id <= last_id_ || writer_ ||
        (r.kind == Access::write && readers_ != 0)) return false;
    Receipt* free = nullptr;
    for (auto& slot : active_) {
      if (!slot.loan_id) { if (!free) free = &slot; continue; }
      // No nested scope or replacement generation on the same logical slot.
      if (slot.execution_id == r.execution_id || slot.execution_slot == r.execution_slot)
        return false;
    }
    if (!free) return false;
    *free = r;
    free->place = place_; // never retain the caller's non-owning string view
    last_id_ = r.loan_id;
    if (r.kind == Access::write) writer_ = true;
    else ++readers_;
    return true;
  }

  [[nodiscard]] bool allows_access(const Receipt& r, Access required) const noexcept {
    if (retired_ || !r.valid() || r.action != StateRegionAction::borrow ||
        !state_region_access_valid(required) || !same_region(r) ||
        (required == Access::write && r.kind != Access::write)) return false;
    for (const auto& slot : active_)
      if (slot.authorizes_release(r.release_record())) return true;
    return false;
  }

  [[nodiscard]] bool release(const Receipt& r) noexcept {
    if (!r.valid() || r.action != StateRegionAction::release || !same_region(r)) return false;
    for (auto& slot : active_) {
      if (!slot.authorizes_release(r)) continue;
      if (slot.kind == Access::write) writer_ = false;
      else --readers_;
      slot = {};
      return true;
    }
    return false;
  }

  // Revoke new admissions/access immediately, but retain existing receipts
  // until the trusted owner releases each after physical scope unwind.
  [[nodiscard]] bool retire() noexcept {
    if (retired_) return false;
    retired_ = true;
    return true;
  }
  [[nodiscard]] std::size_t active_count() const noexcept { return readers_ + (writer_ ? 1u : 0u); }
  [[nodiscard]] bool quiescent_after_retire() const noexcept { return retired_ && active_count() == 0; }
};

} // namespace oresforge::runtime
