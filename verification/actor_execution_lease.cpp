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
#include <vector>

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

struct ReentrantObserver final : ActorLifetimeResource {
  Mailbox& box;
  std::atomic<unsigned> callbacks{0};
  std::atomic<bool> sync_close_rejected{false};
  std::atomic<bool> sync_restart_rejected{false};
  explicit ReentrantObserver(Mailbox& m) : box(m) {}
  void actor_owner_stopped() noexcept override {
    callbacks.fetch_add(1, std::memory_order_release);
    try { box.close(); }
    catch (const std::runtime_error&) {
      sync_close_rejected.store(true, std::memory_order_release);
    }
    try { (void)box.restart(); }
    catch (const std::runtime_error&) {
      sync_restart_rejected.store(true, std::memory_order_release);
    }
    box.request_close(); // Nonblocking stop is reentrant and idempotent.
  }
};

void retirement_callback_reentry_is_fail_closed() {
  Mailbox box{109};
  const auto first = box.context();
  const auto observer = std::make_shared<ReentrantObserver>(box);
  check(first.attach_owned_resource(observer), "cannot attach reentrant observer");
  box.close();
  check(observer->callbacks.load(std::memory_order_acquire) == 1,
        "reentrant retirement callback executed more than once");
  check(observer->sync_close_rejected.load(std::memory_order_acquire) &&
        observer->sync_restart_rejected.load(std::memory_order_acquire),
        "blocking lifecycle was allowed from retirement callback");
  check(!first.valid(), "callback unexpectedly resurrected actor authority");
  box.close();
  check(observer->callbacks.load(std::memory_order_acquire) == 1,
        "repeated close delivered duplicate retirement callback");
}

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

void actor_self_shutdown_is_nonblocking() {
  Mailbox owner{103};
  Mailbox reader{104};
  const auto current = owner.context();
  const auto reader_context = reader.context();
  const auto observer = std::make_shared<Observer>();
  check(current.attach_owned_resource(observer), "cannot attach self-close observer");
  SharedI64Table<1> table(current, TableAccess::Public);
  const auto owner_cap = table.owner_capability(current);
  const auto reader_cap = table.delegate(owner_cap, current, reader_context);
  table.insert(owner_cap, current, 1, 91);

  {
    auto lease = owner.execution_lease();
    bool rejected_close = false;
    try { owner.close(); }
    catch (const std::runtime_error&) { rejected_close = true; }
    check(rejected_close, "synchronous self-close must fail rather than self-join");
    bool rejected_restart = false;
    try { (void)owner.restart(); }
    catch (const std::runtime_error&) { rejected_restart = true; }
    check(rejected_restart, "synchronous self-restart must fail rather than self-join");
    check(table.lookup(owner_cap, lease.context(), 1) == 91,
          "self-close rejection modified the active table");

    owner.request_close(); // The actor may request its own shutdown.
    std::atomic<bool> unleased_denied{false};
    std::thread unleased([&] {
      if (current.valid()) return;
      try { (void)table.lookup(owner_cap, current, 1); }
      catch (const TableError& e) {
        if (e.code == TableErrorCode::Denied)
          unleased_denied.store(true, std::memory_order_release);
      }
    });
    unleased.join();
    check(unleased_denied.load(std::memory_order_acquire),
          "stopping actor authorized an unleased foreign-thread caller");
    check(observer->calls.load(std::memory_order_acquire) == 0,
          "actor retired its table while leased code was still running");
    check(table.lookup(owner_cap, lease.context(), 1) == 91,
          "request_close retired a table before the final lease release");
    bool admission_rejected = false;
    try { (void)owner.execution_lease(); }
    catch (const std::runtime_error&) { admission_rejected = true; }
    check(admission_rejected, "shutdown request admitted a new lease");
    bool context_rejected = false;
    try { (void)owner.context(); }
    catch (const std::runtime_error&) { context_rejected = true; }
    check(context_rejected, "shutdown request published a new actor context");
    auto late = std::make_shared<Observer>();
    check(!current.attach_owned_resource(late),
          "resource attachment succeeded after stop request");
  }
  check(observer->calls.load(std::memory_order_acquire) == 1,
        "final lease did not retire actor resources exactly once");
  check(!current.valid(), "self-stopped actor authority remained valid");
  bool closed = false;
  try { (void)table.lookup(reader_cap, reader_context, 1); }
  catch (const TableError& e) { closed = e.code == TableErrorCode::Closed; }
  check(closed, "owner death did not retire delegated table access");
  owner.close(); // Idempotent synchronous finish after deferred cleanup.
  check(observer->calls.load(std::memory_order_acquire) == 1,
        "repeated external close delivered duplicate retirement");
}

void multiple_leases_quiesce_in_last_release() {
  Mailbox owner{105};
  const auto current = owner.context();
  auto observer = std::make_shared<Observer>();
  check(current.attach_owned_resource(observer), "cannot attach multi-lease observer");
  {
    auto first = owner.execution_lease();
    {
      auto second = owner.execution_lease();
      owner.request_close();
      check(second.context().valid(), "nested lease revoked early");
      check(observer->calls.load(std::memory_order_acquire) == 0,
            "nested stop retired before quiescence");
    }
    check(first.context().valid(), "first lease not preserved after second release");
    check(observer->calls.load(std::memory_order_acquire) == 0,
          "stop retired while one active lease remained");
  }
  check(observer->calls.load(std::memory_order_acquire) == 1,
        "last of nested leases did not finish teardown");
}

void lease_budget_is_bounded() {
  Mailbox box{108};
  std::vector<ActorExecutionLease> leases;
  leases.reserve(oresforge::runtime::kMaxActorExecutionLeases);
  for (std::size_t i=0; i<oresforge::runtime::kMaxActorExecutionLeases; ++i)
    leases.push_back(box.execution_lease());
  bool rejected = false;
  try { (void)box.execution_lease(); }
  catch (const std::runtime_error&) { rejected = true; }
  check(rejected, "exhausted actor execution lease budget was not enforced");
  leases.clear();
  const auto resumed = box.execution_lease();
  check(resumed.context().valid(), "lease budget not released on quiescence");
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

void external_close_and_self_request_do_not_deadlock() {
  Mailbox box{106};
  const auto old = box.context();
  auto observer = std::make_shared<Observer>();
  check(old.attach_owned_resource(observer), "cannot attach external-close observer");
  std::latch starting{1};
  std::atomic<bool> done{false};
  std::thread closer;
  {
    auto lease = box.execution_lease();
    closer = std::thread([&] {
      starting.count_down();
      box.close();
      done.store(true, std::memory_order_release);
    });
    starting.wait();
    // Either ordering is allowed; both paths must complete without waiting
    // on a mailbox mutex held by a concurrent synchronous closer.
    box.request_close();
    check(lease.context().valid(), "external close invalidated a pinned lease");
    check(!done.load(std::memory_order_acquire),
          "external close returned before final lease release");
  }
  closer.join();
  check(done.load(std::memory_order_acquire), "external close did not finish");
  check(!old.valid() && observer->calls.load(std::memory_order_acquire) == 1,
        "racing close delivered incomplete or duplicate lifecycle cleanup");
}

void supervisor_restart_aborted_by_leased_actor_shutdown() {
  Mailbox box{107};
  const auto old = box.context();
  auto observer = std::make_shared<Observer>();
  check(old.attach_owned_resource(observer), "cannot attach restart observer");
  std::latch starting{1};
  std::atomic<int> outcome{0};
  std::thread supervisor;
  {
    auto lease = box.execution_lease();
    supervisor = std::thread([&] {
      starting.count_down();
      try {
        (void)box.restart();
        outcome.store(1, std::memory_order_release);
      } catch (const std::runtime_error&) {
        outcome.store(2, std::memory_order_release);
      }
    });
    starting.wait();
    // Wait for the supervisor's restart transition to reject admission.
    // No time-based sleeps or unchecked assumptions about thread ordering.
    bool restart_started = false;
    for (unsigned i=0; i<1000000; ++i) {
      try { (void)box.context(); }
      catch (const std::runtime_error&) { restart_started = true; break; }
      std::this_thread::yield();
    }
    if (!restart_started) {
      // Release the lease before joining; a run with no observable restart
      // admission boundary is an explicit failure, not a hang.
      box.request_close();
    } else {
      box.request_close();
      check(lease.context().valid(), "restart closed leased actor prematurely");
    }
    check(outcome.load(std::memory_order_acquire) == 0,
          "supervisor restart completed before final lease release");
  }
  supervisor.join();
  check(outcome.load(std::memory_order_acquire) == 2,
        "shutdown request failed to abort in-progress actor restart");
  check(!old.valid() && observer->calls.load(std::memory_order_acquire) == 1,
        "aborted restart failed to retire old resources once");
  bool denied = false;
  try { (void)box.context(); }
  catch (const std::runtime_error&) { denied = true; }
  check(denied, "aborted restart resurrected a closed actor");
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
    actor_self_shutdown_is_nonblocking();
    multiple_leases_quiesce_in_last_release();
    lease_budget_is_bounded();
    retirement_callback_reentry_is_fail_closed();
    external_close_and_self_request_do_not_deadlock();
    supervisor_restart_aborted_by_leased_actor_shutdown();
    close_waits_for_lease();
    restart_waits_for_lease();
    concurrent_lease_admission_and_shutdown();
    std::cout << "PASS: self-shutdown request, nested lease quiescence, "
                 "owner retirement and 128 admission/shutdown races\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
