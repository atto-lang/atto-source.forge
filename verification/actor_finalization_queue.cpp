#include "runtime/actor_finalizer.hpp"
#include "runtime/shared_i64_table.hpp"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>

using oresforge::runtime::ActorFinalizationQueue;
using oresforge::runtime::ActorLifetimeResource;
using oresforge::runtime::Mailbox;
using oresforge::shared::SharedI64Table;
using oresforge::shared::TableAccess;
using oresforge::shared::TableError;
using oresforge::shared::TableErrorCode;

namespace {
void check(bool yes, const char* why) {
  if (!yes) throw std::runtime_error(why);
}
struct Observer final : ActorLifetimeResource {
  std::atomic<int> retired{0};
  void actor_owner_stopped() noexcept override {
    retired.fetch_add(1,std::memory_order_release);
  }
};

void duplicate_requests_and_supervisor_completion() {
  ActorFinalizationQueue<2> queue;
  auto actor = std::make_shared<Mailbox>(201);
  const auto first = actor->context();
  auto observer = std::make_shared<Observer>();
  check(first.attach_owned_resource(observer), "cannot attach finalization observer");
  SharedI64Table<1> table(first, TableAccess::Public);
  const auto owner = table.owner_capability(first);
  table.insert(owner,first,1,9);

  check(queue.try_request_close(actor) == ActorFinalizationQueue<2>::Admission::Queued,
        "first stop request not queued");
  check(queue.try_request_close(actor) == ActorFinalizationQueue<2>::Admission::AlreadyQueued,
        "duplicate stop request generated another ticket");
  check(queue.pending() == 1, "duplicate request consumed another queue slot");
  check(observer->retired.load(std::memory_order_acquire) == 0,
        "queue admission ran owner callbacks");
  check(!first.valid(), "queue request did not fence old authority");
  check(queue.run_one(), "supervisor did not process queued stop");
  check(queue.pending() == 0 && queue.completed() == 1 && queue.stale() == 0,
        "supervisor did not record exactly one completion");
  check(observer->retired.load(std::memory_order_acquire) == 1,
        "supervisor completion missed owner observer");
  check(!queue.run_one(), "completed actor was finalized twice");
  actor->close();
  check(observer->retired.load(std::memory_order_acquire) == 1,
        "double close emitted duplicate observer");
}

void bounded_admission_cannot_lose_stop() {
  ActorFinalizationQueue<1> queue;
  auto first = std::make_shared<Mailbox>(202);
  auto second = std::make_shared<Mailbox>(203);
  const auto second_before = second->context();
  check(queue.try_request_close(first) == ActorFinalizationQueue<1>::Admission::Queued,
        "first ticket was not admitted");
  check(queue.try_request_close(second) == ActorFinalizationQueue<1>::Admission::Full,
        "capacity exhaustion was not reported");
  check(second_before.valid() && second->execution_lease().context().valid(),
        "capacity rejection stopped an actor without a retained ticket");
  check(queue.try_request_close(first) == ActorFinalizationQueue<1>::Admission::AlreadyQueued,
        "duplicate request not recognized when queue is full");
  check(queue.drain_ready() == 1, "first accepted ticket was dropped");
  check(queue.try_request_close(second) == ActorFinalizationQueue<1>::Admission::Queued,
        "slot did not reopen after supervisor completion");
  check(queue.drain_ready() == 1 && queue.completed() == 2,
        "second actor not completed exactly once");
}

void stale_ticket_does_not_stop_restarted_actor() {
  ActorFinalizationQueue<2> queue;
  auto actor = std::make_shared<Mailbox>(204);
  const auto old = actor->context();
  check(queue.try_request_close(actor) == ActorFinalizationQueue<2>::Admission::Queued,
        "unable to enqueue prior incarnation");
  // A different trusted supervisor may have finalized/restarted the actor
  // before this queue's older ticket is processed.
  auto replacement = actor->restart();
  const auto renewed = actor->context();
  check(replacement.generation == old.incarnation() + 1 && renewed.valid(),
        "supervisor restart did not create a new actor incarnation");
  check(queue.run_one(), "stale request was not consumed");
  check(queue.stale() == 1 && queue.completed() == 0,
        "stale ticket incorrectly counted as a completion");
  check(renewed.valid() && actor->execution_lease().context() == renewed,
        "old finalization ticket shut down restarted actor");
}

void leased_shutdown_is_not_observer_callback() {
  ActorFinalizationQueue<2> queue;
  auto actor = std::make_shared<Mailbox>(205);
  auto observer = std::make_shared<Observer>();
  const auto owner = actor->context();
  check(owner.attach_owned_resource(observer), "cannot attach running observer");
  std::latch ready{1};
  std::latch release{1};
  std::atomic<bool> completed{false};
  std::thread supervisor;
  {
    auto lease = actor->execution_lease();
    check(queue.try_request_close(actor) == ActorFinalizationQueue<2>::Admission::Queued,
          "leased actor stop was rejected");
    check(observer->retired.load(std::memory_order_acquire) == 0,
          "stop request synchronously ran a callback");
    supervisor = std::thread([&] {
      ready.count_down();
      (void)queue.run_one();
      completed.store(true,std::memory_order_release);
    });
    ready.wait();
    check(lease.context().valid(), "supervisor fenced pinned actor before drain");
    check(!completed.load(std::memory_order_acquire),
          "supervisor ran callbacks while lease was active");
  }
  supervisor.join();
  check(completed.load(std::memory_order_acquire) &&
        observer->retired.load(std::memory_order_acquire) == 1 &&
        queue.pending() == 0,
        "supervisor did not complete owner cleanup after lease release");
}

void rejection_and_generation_reuse() {
  ActorFinalizationQueue<1> queue;
  check(queue.try_request_close({}) == ActorFinalizationQueue<1>::Admission::Invalid,
        "null actor was accepted");
  auto actor = std::make_shared<Mailbox>(206);
  check(queue.try_request_close(actor) == ActorFinalizationQueue<1>::Admission::Queued,
        "initial stop refused");
  check(queue.drain_ready() == 1, "initial stop not drained");
  (void)actor->restart();
  check(queue.try_request_close(actor) == ActorFinalizationQueue<1>::Admission::Queued,
        "new incarnation was incorrectly deduplicated");
  check(queue.drain_ready() == 1 && queue.completed() == 2,
        "second generation was not independently retired");
}
}

int main() {
  try {
    duplicate_requests_and_supervisor_completion();
    bounded_admission_cannot_lose_stop();
    stale_ticket_does_not_stop_restarted_actor();
    leased_shutdown_is_not_observer_callback();
    rejection_and_generation_reuse();
    std::cout << "PASS: bounded supervisor tickets, callback-free admission, "
                 "exactly-once finalization, stop/restart generation fencing\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
