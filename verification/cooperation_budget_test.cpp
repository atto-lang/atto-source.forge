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
      require(carried.charge_backedge() == (dispatch % 4 == 3 && i == 31));
    require(carried.backedges_this_dispatch() == 32);
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
  // The malicious pattern 127 backedges then resume must still poll.
  CooperationBudget short_turns{10'000};
  int observed = 0;
  for (int dispatch = 0; dispatch < 8; ++dispatch) {
    short_turns.begin_dispatch();
    for (int i = 0; i < 127; ++i) {
      ++observed;
      require(short_turns.charge_backedge() == (observed % 128 == 0));
    }
    require(short_turns.backedges_this_dispatch() == 127);
  }
  require(short_turns.remaining_fuel() == 10'000 - 8 * 127);
  // A rejected charge cannot roll the poll phase forward or spend fuel.
  CooperationBudget rejected{129};
  for (int i = 0; i < 127; ++i) require(!rejected.charge_backedge());
  bool fail = false;
  try { (void)rejected.charge_backedge(3); }
  catch (const std::runtime_error&) { fail = true; }
  require(fail && rejected.remaining_fuel() == 2);
  require(rejected.charge_backedge(2));
  require(rejected.remaining_fuel() == 0);
  std::cout << "forge 128-tick cooperation budget PASS (host-only)\n";
}
