#pragma once
#include <cstdint>
#include <string_view>

#include "runtime/state_region_provenance.hpp"

namespace oresforge::compiler {

/*
 * Symbolic compiler requirement only. Concrete runtime execution IDs,
 * execution generations, and region incarnations are intentionally absent.
 */
struct StateRegionLoanRequirement {
  std::string_view region_binding{};
  bool bind_current_logical_execution = true;
  bool require_generation_fence = true;
  std::uint64_t loan_id = 0;
  std::string_view place{};
  runtime::StateRegionAccess kind = runtime::StateRegionAccess::read;
  runtime::StateRegionAction action = runtime::StateRegionAction::borrow;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return !region_binding.empty() && bind_current_logical_execution &&
           require_generation_fence && loan_id && !place.empty();
  }

  [[nodiscard]] constexpr bool authorizes_release(
      const StateRegionLoanRequirement& release) const noexcept {
    return valid() && release.valid() &&
           action == runtime::StateRegionAction::borrow &&
           release.action == runtime::StateRegionAction::release &&
           region_binding == release.region_binding &&
           bind_current_logical_execution ==
               release.bind_current_logical_execution &&
           require_generation_fence == release.require_generation_fence &&
           loan_id == release.loan_id &&
           place == release.place &&
           kind == release.kind;
  }

  [[nodiscard]] constexpr StateRegionLoanRequirement release_requirement()
      const noexcept {
    auto out = *this;
    out.action = runtime::StateRegionAction::release;
    return out;
  }
};

static_assert(sizeof(StateRegionLoanRequirement) <= 64,
              "compiler state-region requirement must remain compact");

} // namespace oresforge::compiler
