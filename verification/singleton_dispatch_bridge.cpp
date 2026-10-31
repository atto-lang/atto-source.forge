#include "runtime/actor.hpp"
#include "runtime/shared_i64_table.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using oresforge::runtime::Mailbox;
using oresforge::runtime::ActorDispatchLease;
using oresforge::runtime::kMaxMessageBytes;
using oresforge::shared::DispatchI64Table;
using oresforge::shared::SharedI64Table;
using oresforge::shared::TableAccess;
using oresforge::shared::TableError;
using oresforge::shared::TableErrorCode;

static_assert(!std::is_copy_constructible_v<ActorDispatchLease>);
static_assert(!std::is_move_constructible_v<ActorDispatchLease>);

namespace {
void check(bool ok, const char* reason) {
  if (!ok) throw std::runtime_error(reason);
}
template<class F> void denied(F&& fn) {
  try { fn(); }
  catch (const TableError& e) {
    check(e.code == TableErrorCode::Denied, "unexpected table denial code");
    return;
  }
  throw std::runtime_error("unauthorized dispatch was accepted");
}
template<class F> void closed(F&& fn) {
  try { fn(); }
  catch (const TableError& e) {
    check(e.code == TableErrorCode::Closed, "unexpected closed table code");
    return;
  }
  throw std::runtime_error("retired table was still accessible");
}
void dispatch_contract() {
  Mailbox owner_box{701}, reader_box{702};
  Mailbox foreign_box{703, 1024, 4 * kMaxMessageBytes, 2};
  const auto owner = owner_box.context();
  const auto reader = reader_box.context();
  SharedI64Table<2> table(owner, TableAccess::Public);
  DispatchI64Table<2> ops(table);
  SharedI64Table<2>::Capability admin;

  denied([&] { (void)ops.owner_capability(); });
  denied([&] { (void)ops.lookup(admin, 1); });
  {
    auto dispatch = owner_box.enter_dispatch();
    admin = ops.owner_capability();
    ops.insert(admin, 1, 0);
    {
      auto nested = owner_box.enter_dispatch();
      check(ops.update_counter(admin, 1, 1) == 1,
            "nested same-actor dispatch lost authority");
    }
    bool blocked = false;
    try { auto invalid = reader_box.enter_dispatch(); (void)invalid; }
    catch (const std::logic_error&) { blocked = true; }
    check(blocked, "cross-actor nested dispatch was accepted");
    check(ops.lookup(admin, 1) == 1, "nested dispatch did not restore owner");
  }
  denied([&] { (void)ops.lookup(admin, 1); });
  const auto reader_cap = table.delegate(admin, owner, reader);
  {
    auto dispatch = reader_box.enter_dispatch();
    check(ops.lookup(reader_cap, 1) == 1, "read dispatch lost delegation");
    denied([&] { ops.insert(reader_cap, 1, 99); });
    denied([&] { (void)ops.lookup(admin, 1); });
  }
  {
    auto dispatch = foreign_box.enter_dispatch();
    denied([&] { (void)ops.lookup(reader_cap, 1); });
  }
  {
    auto dispatch = owner_box.enter_dispatch();
    (void)owner_box.restart();
    denied([&] { (void)ops.lookup(admin, 1); });
  }
  {
    auto dispatch = reader_box.enter_dispatch();
    closed([&] { (void)ops.lookup(reader_cap, 1); });
  }
  denied([&] { (void)ops.lookup(reader_cap, 1); });
}
}
int main() {
  try {
    dispatch_contract();
    std::cout << "PASS: scheduler-bound actor identity, no caller spoofing, "
                 "nested dispatch isolation, restart and owner retirement\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
