#include "runtime/actor.hpp"
#include "runtime/shared_i64_table.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using oresforge::runtime::Mailbox;
using oresforge::runtime::ActorDispatchLease;
using oresforge::runtime::ActorContext;
using oresforge::runtime::ActorLifetimeResource;
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
// Host resource callbacks must never run with the actor capability's
// registration mutex or Mailbox::mutex_ held. These probes re-enter both
// through existing public, non-lifecycle operations during STOP/restart.
struct NoopResource final : ActorLifetimeResource {
  void actor_owner_stopped() noexcept override {}
};
struct ReentrantObserver final : ActorLifetimeResource {
  Mailbox& mailbox;
  ActorContext owner;
  std::shared_ptr<ActorLifetimeResource> attempted;
  std::atomic<int> callbacks{0};
  std::atomic<bool> safe{false};
  ReentrantObserver(Mailbox& box, ActorContext bound,
                    std::shared_ptr<ActorLifetimeResource> next)
      : mailbox(box), owner(std::move(bound)), attempted(std::move(next)) {}
  void actor_owner_stopped() noexcept override {
    try {
      safe.store(mailbox.queued() == 0 && !owner.valid() &&
                 !owner.attach_owned_resource(attempted));
    } catch (...) { safe.store(false); }
    callbacks.fetch_add(1);
  }
};
void lifecycle_callbacks_do_not_hold_registration_or_mailbox_locks() {
  Mailbox box{8001};
  auto original = box.context();
  auto target = std::make_shared<NoopResource>();
  auto first = std::make_shared<ReentrantObserver>(box, original, target);
  check(original.attach_owned_resource(first),
        "cannot register first lifecycle probe");
  box.close();
  check(first->callbacks.load() == 1 && first->safe.load(),
        "STOP callback ran under token or mailbox lock");
  check(!original.valid(), "STOP retained an old actor authority");

  (void)box.restart();
  auto current = box.context();
  auto second = std::make_shared<ReentrantObserver>(box, current, target);
  check(current.attach_owned_resource(second),
        "cannot register restarted lifecycle probe");
  (void)box.restart();
  check(second->callbacks.load() == 1 && second->safe.load(),
        "restart callback ran under token or mailbox lock");
  check(!current.valid(), "restart retained an old actor authority");
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
    lifecycle_callbacks_do_not_hold_registration_or_mailbox_locks();
    std::cout << "PASS: scheduler-bound actor identity, no caller spoofing, "
                 "nested dispatch isolation, restart and owner retirement\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
