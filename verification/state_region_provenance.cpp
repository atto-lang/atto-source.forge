#include "runtime/state_region_provenance.hpp"

#include <cstdio>
#include <cstdlib>

using oresforge::runtime::StateRegionAccess;
using oresforge::runtime::StateRegionAction;
using oresforge::runtime::StateRegionLoanProvenance;

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

  std::puts("PASS Forge state-region provenance v1 (1 positive, 9 negative vectors)");
  return 0;
}
