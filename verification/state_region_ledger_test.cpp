#include "runtime/state_region_ledger.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

using namespace oresforge::runtime;
using Access = StateRegionAccess;
using Receipt = StateRegionLoanProvenance;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; std::abort(); } } while (false)

static Receipt claim(std::uint64_t loan, std::uint64_t execution, Access mode) {
  return {9, 3, 7, execution, execution, 1, loan, "inventory.stock", mode, StateRegionAction::borrow};
}

static void shared_and_exclusive() {
  StateRegionLoanLedger<2> gate(9, 3, 7, "inventory.stock");
  auto a = claim(1, 101, Access::read), b = claim(2, 102, Access::read);
  auto c = claim(3, 103, Access::read), w = claim(3, 103, Access::write);
  CHECK(gate.try_borrow(a)); CHECK(gate.try_borrow(b));
  CHECK(gate.active_count() == 2); CHECK(!gate.try_borrow(c)); CHECK(!gate.try_borrow(w));
  CHECK(gate.allows_access(a, Access::read)); CHECK(!gate.allows_access(a, Access::write));
  CHECK(gate.release(a.release_record())); CHECK(!gate.try_borrow(w));
  CHECK(gate.release(b.release_record())); CHECK(gate.try_borrow(w));
  CHECK(gate.allows_access(w, Access::read)); CHECK(gate.allows_access(w, Access::write));
  CHECK(!gate.try_borrow(claim(4, 104, Access::read)));
  CHECK(gate.release(w.release_record())); CHECK(gate.active_count() == 0);
  CHECK(!gate.try_borrow(w)); CHECK(!gate.release(w.release_record()));
}

static void identity_and_reentry() {
  StateRegionLoanLedger<> gate(9, 3, 7, "inventory.stock");
  auto a = claim(1, 101, Access::read);
  CHECK(gate.try_borrow(a));
  CHECK(!gate.try_borrow(claim(2, 101, Access::read)));
  auto replaced = claim(2, 102, Access::read); replaced.execution_slot = 101; replaced.execution_generation = 2;
  CHECK(!gate.try_borrow(replaced));
  for (unsigned i = 0; i < 9; ++i) {
    auto bad = a;
    switch (i) {
      case 0: ++bad.region_id; break;
      case 1: ++bad.region_generation; break;
      case 2: ++bad.runtime_domain; break;
      case 3: ++bad.execution_id; break;
      case 4: ++bad.execution_slot; break;
      case 5: ++bad.execution_generation; break;
      case 6: ++bad.loan_id; break;
      case 7: bad.place = "inventory.other"; break;
      case 8: bad.kind = Access::write; break;
    }
    CHECK(!gate.allows_access(bad, Access::read));
    CHECK(!gate.release(bad.release_record()));
    CHECK(gate.active_count() == 1);
  }
  CHECK(gate.release(a.release_record()));
  CHECK(!gate.allows_access(a, Access::read)); CHECK(!gate.try_borrow(a));
  CHECK(gate.try_borrow(replaced)); CHECK(gate.release(replaced.release_record()));
}

static void retirement_and_owned_place() {
  StateRegionLoanLedger<> gate(9, 3, 7, "inventory.stock");
  StateRegionLoanLedger<> independent(9, 4, 7, "inventory.stock");
  auto a = claim(1, 101, Access::read);
  auto b = claim(2, 102, Access::read); b.region_generation = 4;
  {
    std::string temporary = "inventory.stock";
    auto transient = a; transient.place = temporary;
    CHECK(gate.try_borrow(transient));
    temporary.assign(1000, 'x');
  }
  CHECK(independent.try_borrow(b)); CHECK(!gate.release(b.release_record()));
  CHECK(gate.allows_access(a, Access::read));
  CHECK(gate.retire()); CHECK(!gate.retire()); CHECK(!gate.quiescent_after_retire());
  CHECK(!gate.allows_access(a, Access::read)); CHECK(!gate.try_borrow(claim(3, 103, Access::read)));
  CHECK(gate.release(a.release_record())); CHECK(gate.quiescent_after_retire());
  CHECK(!gate.release(a.release_record())); CHECK(independent.allows_access(b, Access::read));
  CHECK(independent.release(b.release_record())); CHECK(independent.retire()); CHECK(independent.quiescent_after_retire());
}

static void invalid_and_exhausted_identity() {
  StateRegionLoanLedger<> gate(9, 3, 7, "inventory.stock");
  auto a = claim(1, 101, Access::read);
  CHECK(!gate.try_borrow(a.release_record())); CHECK(!gate.try_borrow({}));
  auto malformed = a; malformed.kind = static_cast<Access>(255);
  CHECK(!gate.try_borrow(malformed));
  malformed = a; malformed.action = static_cast<StateRegionAction>(255);
  CHECK(!gate.try_borrow(malformed));
  auto last = claim(std::numeric_limits<std::uint64_t>::max(), 101, Access::write);
  CHECK(gate.try_borrow(last)); CHECK(!gate.allows_access(last, static_cast<Access>(255)));
  CHECK(gate.release(last.release_record())); CHECK(!gate.try_borrow(a)); CHECK(!gate.try_borrow(last));
  CHECK(gate.retire()); CHECK(gate.quiescent_after_retire());
}

int main() {
  shared_and_exclusive(); identity_and_reentry(); retirement_and_owned_place(); invalid_and_exhausted_identity();
  std::cout << "PASS Forge state region ledger: 4 groups\n";
}
