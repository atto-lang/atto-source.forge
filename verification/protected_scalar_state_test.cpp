#include "runtime/protected_scalar_state.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace oresforge::runtime;
using Access = StateRegionAccess;
using Receipt = StateRegionLoanProvenance;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; std::abort(); } } while (false)
static Receipt claim(std::uint64_t loan, std::uint64_t execution, Access mode, std::uint64_t generation = 1) {
  return {1, generation, 7, execution, execution, 1, loan, "count", mode, StateRegionAction::borrow};
}
static void permissions_and_isolation() {
  ProtectedScalarState<2> a(1, 1, 7, "count", 40), b(1, 2, 7, "count", -3);
  auto r = claim(1, 1, Access::read), r2 = claim(2, 2, Access::read);
  auto w = claim(3, 3, Access::write), foreign = claim(1, 1, Access::write, 2);
  CHECK(!a.read(r)); CHECK(a.try_borrow(r)); CHECK(a.try_borrow(r2));
  CHECK(a.read(r) == 40); CHECK(!a.write(r, 99)); CHECK(!a.add_checked(r, 1));
  CHECK(!a.try_borrow(w)); CHECK(b.try_borrow(foreign)); CHECK(b.read(foreign) == -3);
  CHECK(!a.read(foreign)); CHECK(!a.write(foreign, 9));
  CHECK(a.release(r.release_record())); CHECK(!a.try_borrow(w));
  CHECK(a.release(r2.release_record())); CHECK(a.try_borrow(w));
  CHECK(a.add_checked(w, 2) == 42); CHECK(a.read(w) == 42);
  CHECK(!a.read(r)); CHECK(!a.write(r, 0)); CHECK(b.read(foreign) == -3);
  CHECK(a.release(w.release_record())); CHECK(b.release(foreign.release_record()));
}
static void checked_boundaries() {
  constexpr auto lo = std::numeric_limits<std::int64_t>::min();
  constexpr auto hi = std::numeric_limits<std::int64_t>::max();
  ProtectedScalarState<> a(1, 1, 7, "count", 0);
  auto w = claim(1, 1, Access::write); CHECK(a.try_borrow(w));
  for (auto initial : {lo, hi}) {
    CHECK(a.write(w, initial)); bool overflow = false;
    try { (void)a.add_checked(w, initial == lo ? -1 : 1); }
    catch (const std::overflow_error&) { overflow = true; }
    CHECK(overflow); CHECK(a.read(w) == initial);
  }
  CHECK(a.write(w, 0)); CHECK(a.add_checked(w, lo) == lo);
  CHECK(a.add_checked(w, hi) == -1); CHECK(a.add_checked(w, 1) == 0);
  CHECK(a.release(w.release_record()));
  auto r = claim(2, 1, Access::read); CHECK(a.try_borrow(r)); CHECK(a.read(r) == 0);
  CHECK(a.release(r.release_record()));
}
static void retirement_and_committed_state() {
  ProtectedScalarState<> a(1, 1, 7, "count", 0);
  auto w = claim(1, 1, Access::write); CHECK(a.try_borrow(w)); CHECK(a.write(w, 42));
  CHECK(a.release(w.release_record()));
  auto r = claim(2, 1, Access::read); CHECK(a.try_borrow(r)); CHECK(a.read(r) == 42);
  CHECK(a.retire()); CHECK(!a.retire()); CHECK(!a.quiescent_after_retire());
  CHECK(!a.read(r)); CHECK(!a.write(w, 9)); CHECK(!a.try_borrow(claim(3, 3, Access::read)));
  CHECK(a.release(r.release_record())); CHECK(a.quiescent_after_retire()); CHECK(!a.release(r.release_record()));
}
int main() {
  permissions_and_isolation(); checked_boundaries(); retirement_and_committed_state();
  std::cout << "PASS Forge protected scalar: 3 groups\n";
}
