#include "runtime/CooperationBudget.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>

static void require(bool ok) {
  if (!ok) throw std::runtime_error("forge cooperation budget invariant failed");
}
int main() {
  using oresforge::runtime::CooperationBudget;
  CooperationBudget budget{10'000};
  int polls = 0;
  for (int i = 1; i <= 2048; ++i) {
    const bool due = budget.charge_backedge();
    require(due == (i % 128 == 0));
    if (due) ++polls;
  }
  require(polls == 16);
  require(budget.remaining_fuel() == 7952);

  CooperationBudget carried{256};
  for (int dispatch = 0; dispatch < 8; ++dispatch) {
    carried.begin_dispatch();
    for (int i = 0; i < 32; ++i)
      require(!carried.charge_backedge());
  }
  require(carried.remaining_fuel() == 0);
  carried.begin_dispatch();
  bool exhausted = false;
  try { (void)carried.charge_backedge(); }
  catch (const std::runtime_error&) { exhausted = true; }
  require(exhausted);

  CooperationBudget helper{6};
  helper.charge_operation(4);
  require(helper.remaining_fuel() == 2);
  require(!helper.charge_backedge(2));
  exhausted = false;
  try { helper.charge_operation(1); }
  catch (const std::runtime_error&) { exhausted = true; }
  require(exhausted);
  bool invalid = false;
  try { CooperationBudget forbidden{0}; }
  catch (const std::invalid_argument&) { invalid = true; }
  require(invalid);
  invalid = false;
  try { helper.charge_operation(0); }
  catch (const std::invalid_argument&) { invalid = true; }
  require(invalid);
  require(helper.remaining_fuel() == 0);
  std::cout << "forge 128-tick cooperation budget PASS (host-only)\n";
}
