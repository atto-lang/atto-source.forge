#include "runtime/state_region_provenance.hpp"
#include "compiler/state_region_requirement.hpp"

#include <cstdio>
#include <cstdlib>

using oresforge::runtime::StateRegionAccess;
using oresforge::runtime::StateRegionAction;
using oresforge::runtime::StateRegionLoanProvenance;
using oresforge::compiler::StateRegionLoanRequirement;

#define CHECK(x) do { if (!(x)) {   std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);   std::abort(); } } while (false)

static constexpr StateRegionLoanProvenance borrow{
    9, 3, 7, 1001, 41, 6, 501, "inventory.stock",
    StateRegionAccess::write, StateRegionAction::borrow};

int main() {
  static_assert(borrow.valid());
  constexpr auto release = borrow.release_record();
  static_assert(borrow.authorizes_release(release));

  auto foreign_execution = release;
  foreign_execution.execution_id = 1002;
  CHECK(!borrow.authorizes_release(foreign_execution));

  auto foreign_domain = release;
  foreign_domain.runtime_domain = 8;
  CHECK(!borrow.authorizes_release(foreign_domain));

  auto stale_execution = release;
  stale_execution.execution_generation = 5;
  CHECK(!borrow.authorizes_release(stale_execution));

  auto foreign_slot = release;
  foreign_slot.execution_slot = 42;
  CHECK(!borrow.authorizes_release(foreign_slot));

  auto foreign_region = release;
  foreign_region.region_id = 10;
  CHECK(!borrow.authorizes_release(foreign_region));

  auto stale_region = release;
  stale_region.region_generation = 2;
  CHECK(!borrow.authorizes_release(stale_region));

  auto foreign_loan = release;
  foreign_loan.loan_id = 502;
  CHECK(!borrow.authorizes_release(foreign_loan));

  auto foreign_place = release;
  foreign_place.place = "inventory.reserved";
  CHECK(!borrow.authorizes_release(foreign_place));

  auto wrong_mode = release;
  wrong_mode.kind = StateRegionAccess::read;
  CHECK(!borrow.authorizes_release(wrong_mode));

  auto invalid = borrow;
  invalid.execution_generation = 0;
  CHECK(!invalid.valid());
  CHECK(!invalid.authorizes_release(release));

  auto forged_borrow_kind = borrow;
  forged_borrow_kind.kind = static_cast<StateRegionAccess>(255);
  CHECK(!forged_borrow_kind.valid());
  CHECK(!forged_borrow_kind.authorizes_release(release));
  CHECK(!forged_borrow_kind.release_record().valid());

  auto forged_release_kind = release;
  forged_release_kind.kind = static_cast<StateRegionAccess>(255);
  CHECK(!forged_release_kind.valid());
  CHECK(!borrow.authorizes_release(forged_release_kind));

  auto forged_borrow_action = borrow;
  forged_borrow_action.action = static_cast<StateRegionAction>(255);
  CHECK(!forged_borrow_action.valid());
  CHECK(!forged_borrow_action.release_record().valid());

  auto empty_place = borrow;
  empty_place.place = " \t ";
  CHECK(!empty_place.valid());
  CHECK(!empty_place.authorizes_release(release));
  CHECK(!release.release_record().valid());
  CHECK(!StateRegionLoanProvenance{}.authorizes_release(release));

  constexpr StateRegionLoanRequirement requirement{
      "inventory", true, true, 501, "inventory.stock",
      StateRegionAccess::write, StateRegionAction::borrow};
  static_assert(requirement.valid());
  constexpr auto requirement_release = requirement.release_requirement();
  static_assert(requirement.authorizes_release(requirement_release));
  auto unfenced = requirement;
  unfenced.require_generation_fence = false;
  CHECK(!unfenced.valid());
  auto other_binding = requirement_release;
  other_binding.region_binding = "orders";
  CHECK(!requirement.authorizes_release(other_binding));

  auto forged_requirement = requirement;
  forged_requirement.kind = static_cast<StateRegionAccess>(255);
  CHECK(!forged_requirement.valid());
  CHECK(!forged_requirement.release_requirement().valid());
  auto forged_requirement_action = requirement_release;
  forged_requirement_action.action = static_cast<StateRegionAction>(255);
  CHECK(!forged_requirement_action.valid());
  CHECK(!requirement.authorizes_release(forged_requirement_action));
  CHECK(!requirement_release.release_requirement().valid());
  auto blank_region = requirement;
  blank_region.region_binding = " \t ";
  CHECK(!blank_region.valid());

  std::puts("PASS Forge state-region requirement/provenance v1");
  return 0;
}
