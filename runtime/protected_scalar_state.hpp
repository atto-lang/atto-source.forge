#pragma once
#include "runtime/state_region_ledger.hpp"
#include <limits>
#include <optional>

namespace oresforge::runtime {

// Trusted host scalar storage. Scheduler serializes calls and authenticates
// receipt identity; no raw reference to the state can escape. This standalone
// substrate is not yet wired into the actor arena or compiler.
template<std::size_t Capacity = 64>
class ProtectedScalarState final {
  using Receipt = StateRegionLoanProvenance;
  using Access = StateRegionAccess;
  StateRegionLoanLedger<Capacity> ledger_;
  std::int64_t value_;
 public:
  ProtectedScalarState(std::uint64_t region, std::uint64_t generation,
                       std::uint64_t domain, std::string_view place, std::int64_t initial)
      : ledger_(region, generation, domain, place), value_(initial) {}
  [[nodiscard]] bool try_borrow(const Receipt& r) noexcept { return ledger_.try_borrow(r); }
  [[nodiscard]] bool release(const Receipt& r) noexcept { return ledger_.release(r); }
  [[nodiscard]] std::optional<std::int64_t> read(const Receipt& r) const noexcept {
    if (!ledger_.allows_access(r, Access::read)) return std::nullopt;
    return value_;
  }
  [[nodiscard]] bool write(const Receipt& r, std::int64_t value) noexcept {
    if (!ledger_.allows_access(r, Access::write)) return false;
    value_ = value;
    return true;
  }
  // An overflow is distinct from a denied access; neither mutates the cell.
  // The caller still owns the loan and must release it on scope unwind.
  [[nodiscard]] std::optional<std::int64_t> add_checked(const Receipt& r, std::int64_t delta) {
    if (!ledger_.allows_access(r, Access::write)) return std::nullopt;
    constexpr auto lo = std::numeric_limits<std::int64_t>::min();
    constexpr auto hi = std::numeric_limits<std::int64_t>::max();
    if ((delta > 0 && value_ > hi - delta) || (delta < 0 && value_ < lo - delta))
      throw std::overflow_error("protected scalar addition overflow");
    value_ += delta;
    return value_;
  }
  [[nodiscard]] bool retire() noexcept { return ledger_.retire(); }
  [[nodiscard]] bool quiescent_after_retire() const noexcept { return ledger_.quiescent_after_retire(); }
};

} // namespace oresforge::runtime
