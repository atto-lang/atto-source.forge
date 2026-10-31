#include "runtime/actor.hpp"
#include "runtime/shared_i64_table.hpp"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>

using oresforge::runtime::ActorContext;
using oresforge::runtime::ActorExecutionLease;
using oresforge::runtime::ActorLifetimeResource;
using oresforge::runtime::CapabilityToken;
using oresforge::runtime::Mailbox;
using oresforge::shared::SharedI64Table;
using oresforge::shared::TableAccess;
using oresforge::shared::TableError;
using oresforge::shared::TableErrorCode;

static_assert(!std::is_default_constructible_v<ActorExecutionLease>);
static_assert(!std::is_copy_constructible_v<ActorExecutionLease>);
static_assert(!std::is_constructible_v<ActorExecutionLease, ActorContext,
              std::shared_ptr<const CapabilityToken>>,
              "guest code must not mint an actor execution lease");

namespace {
void check(bool condition, const char* description) {
  if (!condition) throw std::runtime_error(description);
}
template<class F>
void expect_denied(F&& operation) {
  bool denied = false;
  try { operation(); }
  catch (const TableError& e) { denied = e.code == TableErrorCode::Denied; }
  check(denied, "expected stale authority denial");
}
struct Observer final : ActorLifetimeResource {
  std::atomic<unsigned> calls{0};
  void actor_owner_stopped() noexcept override {
    calls.fetch_add(1, std::memory_order_release);
  }
};

void moved_from_lease_is_not_authority() {
  Mailbox box{100};
  auto admitted = box.execution_lease();
  auto successor = std::move(admitted);
  bool rejected = false;
  try { (void)admitted.context(); }
  catch (const std::runtime_error&) { rejected = true; }
  check(rejected, "moved-from execution lease exposed unpinned context");
  check(successor.context().actor() == 100,
        "moved execution lease lost its admitted incarnation");
}

void close_waits_for_lease() {
  Mailbox owner{101};
  const auto first = owner.context();
  const auto observer = std::make_shared<Observer>();
  check(first.attach_owned_resource(observer), "cannot register observer");
  SharedI64Table<1> table(first, TableAccess::Protected);
  const auto cap = table.owner_capability(first);
  table.insert(cap, first, 1, 7);
  std::latch started{1};
  std::atomic<bool> finished{false};
  std::thread closer;
  {
    auto lease = owner.execution_lease();
    check(lease.context() == first, "execution lease changed actor identity");
    closer = std::thread([&] {
      started.count_down();
      owner.close();
      finished.store(true, std::memory_order_release);
    });
    started.wait();
    // The actor must remain valid until all admitted leases are released.
    check(lease.context().valid(), "close invalidated an in-flight lease");
    check(table.lookup(cap, lease.context(), 1) == 7,
          "close retired a table before an admitted operation completed");
    check(!finished.load(std::memory_order_acquire),
          "close completed with an outstanding execution lease");
    check(observer->calls.load(std::memory_order_acquire) == 0,
          "retirement callback ran before quiescence");
  }
  closer.join();
  check(finished.load(std::memory_order_acquire) && !first.valid(),
        "close did not fence old incarnation");
  check(observer->calls.load(std::memory_order_acquire) == 1,
        "lifetime callback did not execute exactly once");
  bool refused = false;
  try { (void)owner.execution_lease(); }
  catch (const std::runtime_error&) { refused = true; }
  check(refused, "closed actor admitted a fresh lease");
  expect_denied([&] { (void)table.lookup(cap, first, 1); });
}

void restart_waits_for_lease() {
  Mailbox box{102};
  const auto old = box.context();
  SharedI64Table<1> table(old, TableAccess::Protected);
  const auto cap = table.owner_capability(old);
  table.insert(cap, old, 9, 41);
  std::latch started{1};
  std::atomic<bool> completed{false};
  std::thread restarter;
  {
    auto lease = box.execution_lease();
    restarter = std::thread([&] {
      started.count_down();
      (void)box.restart();
      completed.store(true, std::memory_order_release);
    });
    started.wait();
    check(lease.context().valid() &&
          table.lookup(cap, lease.context(), 9) == 41,
          "restart invalidated an admitted table operation");
    check(!completed.load(std::memory_order_acquire),
          "restart completed before lease quiescence");
  }
  restarter.join();
  const auto next = box.execution_lease();
  check(completed.load(std::memory_order_acquire) &&
        next.context().incarnation() == old.incarnation() + 1,
        "new incarnation did not begin after old lease retired");
  expect_denied([&] {
    (void)table.lookup(cap, next.context(), 9);
  });
}

void concurrent_lease_admission_and_shutdown() {
  // Repeated interleavings use latches, not sleep-based assumptions.
  for (std::uint64_t round = 0; round < 128; ++round) {
    Mailbox box{1000 + round};
    const auto context = box.context();
    SharedI64Table<1> table(context);
    const auto cap = table.owner_capability(context);
    table.insert(cap, context, 1, 42);
    std::latch start{1};
    std::atomic<bool> failed{false};
    std::thread worker([&] {
      start.wait();
      try {
        auto lease = box.execution_lease();
        (void)table.lookup(cap, lease.context(), 1);
      } catch (const std::runtime_error&) {
        // Shutdown won admission; no operation was authorized.
      } catch (const TableError& e) {
        if (e.code != TableErrorCode::Closed && e.code != TableErrorCode::Denied)
          failed.store(true, std::memory_order_release);
      } catch (...) {
        failed.store(true, std::memory_order_release);
      }
    });
    start.count_down();
    box.close();
    worker.join();
    check(!failed.load(std::memory_order_acquire),
          "unexpected lease/close race outcome");
    check(!context.valid(), "closed actor remained active");
  }
}
} // namespace

int main() {
  try {
    moved_from_lease_is_not_authority();
    close_waits_for_lease();
    restart_waits_for_lease();
    concurrent_lease_admission_and_shutdown();
    std::cout << "PASS: runtime-minted lease, close/restart quiescence, "
                 "owner retirement and 128 admission/shutdown races\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
