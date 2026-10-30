#pragma once
#include <cstdint>
#include <string_view>

namespace oresforge::runtime {

enum class StateRegionAccess : std::uint8_t { read, write };
enum class StateRegionAction : std::uint8_t { borrow, release };

/*
 * Non-owning compiler/runtime provenance metadata. This does not grant
 * authority by itself and intentionally requires no heap allocation.
 */
struct StateRegionLoanProvenance {
  std::uint64_t region_id = 0;
  std::uint64_t region_generation = 0;
  std::uint64_t runtime_domain = 0;
  std::uint64_t execution_id = 0;
  std::uint64_t execution_slot = 0;
  std::uint64_t execution_generation = 0;
  std::uint64_t loan_id = 0;
  std::string_view place{};
  StateRegionAccess kind = StateRegionAccess::read;
  StateRegionAction action = StateRegionAction::borrow;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return region_id && region_generation && runtime_domain && execution_id &&
           execution_slot && execution_generation && loan_id && !place.empty();
  }

  [[nodiscard]] constexpr bool authorizes_release(
      const StateRegionLoanProvenance& release) const noexcept {
    return valid() && release.valid() &&
           action == StateRegionAction::borrow &&
           release.action == StateRegionAction::release &&
           region_id == release.region_id &&
           region_generation == release.region_generation &&
           runtime_domain == release.runtime_domain &&
           execution_id == release.execution_id &&
           execution_slot == release.execution_slot &&
           execution_generation == release.execution_generation &&
           loan_id == release.loan_id &&
           place == release.place &&
           kind == release.kind;
  }

  [[nodiscard]] constexpr StateRegionLoanProvenance release_record() const noexcept {
    auto out = *this;
    out.action = StateRegionAction::release;
    return out;
  }
};

static_assert(sizeof(StateRegionLoanProvenance) <= 96,
              "state-region provenance must remain compact value metadata");

} // namespace oresforge::runtime
