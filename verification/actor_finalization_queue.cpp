#include "runtime/actor_finalizer.hpp"
#include "runtime/shared_i64_table.hpp"
#include <atomic>
#include <array>
#include <vector>
#include <cstdint>
#include <iostream>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>

using oresforge::runtime::ActorFinalizationQueue;
using oresforge::runtime::ActorStopCause;
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

struct ReentrantObserver final : ActorLifetimeResource {
  ActorFinalizationQueue<2>& queue;
  std::shared_ptr<Mailbox> actor;
  std::atomic<int> callbacks{0};
  std::atomic<bool> deduplicated{false};
  ReentrantObserver(ActorFinalizationQueue<2>& q, std::shared_ptr<Mailbox> a)
      : queue(q), actor(std::move(a)) {}
  void actor_owner_stopped() noexcept override {
    callbacks.fetch_add(1, std::memory_order_release);
    try {
      deduplicated.store(
          queue.try_request_close(actor) ==
              ActorFinalizationQueue<2>::Admission::AlreadyQueued,
          std::memory_order_release);
    } catch (...) {}
  }
};

void retirement_callback_reentry_does_not_deadlock_queue() {
  ActorFinalizationQueue<2> queue;
  auto actor = std::make_shared<Mailbox>(207);
  auto observer = std::make_shared<ReentrantObserver>(queue, actor);
  check(actor->context().attach_owned_resource(observer),
        "unable to attach queue reentry observer");
  check(queue.try_request_close(actor) == ActorFinalizationQueue<2>::Admission::Queued,
        "could not enqueue reentrant stop");
  check(queue.drain_ready() == 1, "supervisor did not drain callback task");
  check(observer->callbacks.load(std::memory_order_acquire) == 1 &&
        observer->deduplicated.load(std::memory_order_acquire),
        "retirement callback could not deduplicate a processing ticket");
  check(queue.pending() == 0 && queue.completed() == 1,
        "retirement reentry generated a duplicate finalization");
}

void queue_owns_actor_until_completion() {
  ActorFinalizationQueue<1> queue;
  auto actor = std::make_shared<Mailbox>(208);
  auto observer = std::make_shared<Observer>();
  check(actor->context().attach_owned_resource(observer),
        "unable to attach retained actor observer");
  std::weak_ptr<Mailbox> weak = actor;
  check(queue.try_request_close(actor) == ActorFinalizationQueue<1>::Admission::Queued,
        "strong lifetime ticket not accepted");
  actor.reset();
  check(!weak.expired(), "pending supervisor ticket dropped actor lifetime");
  check(queue.drain_ready() == 1, "retained actor was not finalized");
  check(observer->retired.load(std::memory_order_acquire) == 1 &&
        weak.expired(), "supervisor completion did not retire and release actor");
}

void stop_cause_receipts_preserve_priority() {
  ActorFinalizationQueue<2> queue;
  auto actor = std::make_shared<Mailbox>(209);
  check(queue.try_request_close(actor, ActorStopCause::Cancelled) ==
            ActorFinalizationQueue<2>::Admission::Queued,
        "cancelled actor stop not retained");
  check(queue.try_request_close(actor, ActorStopCause::Panicked) ==
            ActorFinalizationQueue<2>::Admission::AlreadyQueued,
        "panic escalation admitted a duplicate ticket");
  check(queue.try_request_close(actor, ActorStopCause::Requested) ==
            ActorFinalizationQueue<2>::Admission::AlreadyQueued,
        "lower-priority request admitted a duplicate");
  ActorFinalizationQueue<2>::Completion receipt;
  check(queue.run_one(&receipt), "typed completion receipt not emitted");
  check(receipt.actor_id == 209 && receipt.incarnation == 1 &&
        receipt.cause == ActorStopCause::Panicked && receipt.finalized,
        "panic was not preserved as priority supervisor completion reason");

  auto stale_actor = std::make_shared<Mailbox>(210);
  check(queue.try_request_close(stale_actor, ActorStopCause::Raised) ==
            ActorFinalizationQueue<2>::Admission::Queued,
        "raised actor stop refused");
  (void)stale_actor->restart();
  check(queue.run_one(&receipt) &&
        receipt.actor_id == 210 && receipt.incarnation == 1 &&
        receipt.cause == ActorStopCause::Raised && !receipt.finalized,
        "stale completion incorrectly claimed the restarted incarnation");
}

struct BlockingObserver final : ActorLifetimeResource {
  std::latch& entered;
  std::latch& released;
  std::atomic<int> callbacks{0};
  BlockingObserver(std::latch& e, std::latch& r)
      : entered(e), released(r) {}
  void actor_owner_stopped() noexcept override {
    callbacks.fetch_add(1, std::memory_order_release);
    entered.count_down();
    released.wait();
  }
};

void completed_stop_cannot_create_duplicate_receipt() {
  ActorFinalizationQueue<1> queue;
  auto actor = std::make_shared<Mailbox>(211);
  auto observer = std::make_shared<Observer>();
  check(actor->context().attach_owned_resource(observer),
        "failed to attach completed-stop observer");
  ActorFinalizationQueue<1>::Completion receipt;
  check(queue.try_request_close(actor) == ActorFinalizationQueue<1>::Admission::Queued &&
        queue.run_one(&receipt) && receipt.finalized &&
        receipt.incarnation == 1 && queue.completed() == 1,
        "first owner stop was not completed");
  for (unsigned i = 0; i < 128; ++i) {
    check(queue.try_request_close(actor, ActorStopCause::Panicked) ==
              ActorFinalizationQueue<1>::Admission::AlreadyFinalized,
          "completed incarnation admitted a duplicate finalization ticket");
  }
  check(queue.pending() == 0 && queue.completed() == 1 && !queue.run_one() &&
        observer->retired.load(std::memory_order_acquire) == 1,
        "completed stop created a second completion receipt or owner callback");
  (void)actor->restart();
  check(queue.try_request_close(actor) == ActorFinalizationQueue<1>::Admission::Queued,
        "new incarnation incorrectly rejected as already finalized");
  check(queue.run_one(&receipt) && receipt.incarnation == 2 &&
        receipt.finalized && queue.completed() == 2,
        "new incarnation was not independently finalized");
  check(observer->retired.load(std::memory_order_acquire) == 1,
        "old incarnation observer was delivered twice");
}

void parallel_duplicate_stop_admission() {
  for (std::uint64_t round = 0; round < 32; ++round) {
    ActorFinalizationQueue<2> queue;
    auto actor = std::make_shared<Mailbox>(300 + round);
    auto observer = std::make_shared<Observer>();
    check(actor->context().attach_owned_resource(observer),
          "failed to attach contention observer");
    std::latch go{1};
    std::atomic<unsigned> queued{0}, duplicates{0}, unexpected{0};
    std::array<std::thread, 8> workers;
    for (unsigned i=0; i<workers.size(); ++i) {
      workers[i] = std::thread([&,i] {
        go.wait();
        const auto cause = i == 7 ? ActorStopCause::Panicked :
                           ActorStopCause::Requested;
        const auto outcome = queue.try_request_close(actor, cause);
        if (outcome == ActorFinalizationQueue<2>::Admission::Queued)
          queued.fetch_add(1, std::memory_order_relaxed);
        else if (outcome == ActorFinalizationQueue<2>::Admission::AlreadyQueued)
          duplicates.fetch_add(1, std::memory_order_relaxed);
        else unexpected.fetch_add(1, std::memory_order_relaxed);
      });
    }
    go.count_down();
    for (auto& worker : workers) worker.join();
    check(queued.load() == 1 && duplicates.load() == 7 &&
          unexpected.load() == 0 && queue.pending() == 1,
          "parallel duplicate stop did not preserve one retained ticket");
    ActorFinalizationQueue<2>::Completion receipt;
    check(queue.run_one(&receipt) && receipt.cause == ActorStopCause::Panicked &&
          receipt.finalized && receipt.incarnation == 1 &&
          queue.completed() == 1 && queue.pending() == 0 &&
          observer->retired.load(std::memory_order_acquire) == 1,
          "parallel stop lost panic priority or double-finalized actor");
  }
}

void processing_ticket_is_reserved_and_escalatable() {
  ActorFinalizationQueue<1> queue;
  auto actor = std::make_shared<Mailbox>(500);
  auto unrelated = std::make_shared<Mailbox>(501);
  std::latch entered{1}, release{1};
  auto observer = std::make_shared<BlockingObserver>(entered, release);
  check(actor->context().attach_owned_resource(observer),
        "cannot attach blocking supervisor observer");
  check(queue.try_request_close(actor) == ActorFinalizationQueue<1>::Admission::Queued,
        "failed to queue callback test");
  ActorFinalizationQueue<1>::Completion receipt;
  std::atomic<bool> worker_done{false};
  std::thread supervisor([&] {
    (void)queue.run_one(&receipt);
    worker_done.store(true, std::memory_order_release);
  });
  entered.wait();
  const auto escalation = queue.try_request_close(actor, ActorStopCause::Panicked);
  const auto capacity = queue.try_request_close(unrelated);
  const bool processing_exclusive = !queue.run_one();
  const bool pending_retained = queue.pending() == 1;
  const bool unrelated_still_active = unrelated->context().valid();
  release.count_down();
  supervisor.join();
  check(escalation == ActorFinalizationQueue<1>::Admission::AlreadyQueued &&
        capacity == ActorFinalizationQueue<1>::Admission::Full &&
        processing_exclusive && pending_retained && unrelated_still_active,
        "processing finalizer lost ticket, admitted duplicate or stopped outsider");
  check(worker_done.load(std::memory_order_acquire) && receipt.finalized &&
        receipt.cause == ActorStopCause::Panicked &&
        observer->callbacks.load(std::memory_order_acquire) == 1 &&
        queue.completed() == 1 && queue.pending() == 0,
        "escalation after ticket claim was not reflected in final completion");
}

void self_leased_finalizer_retries_without_losing_ticket() {
  ActorFinalizationQueue<1> queue;
  auto actor = std::make_shared<Mailbox>(502);
  auto observer = std::make_shared<Observer>();
  check(actor->context().attach_owned_resource(observer),
        "cannot register self-lease observer");
  {
    const auto lease = actor->execution_lease();
    check(queue.try_request_close(actor) == ActorFinalizationQueue<1>::Admission::Queued,
          "leased stop could not reserve a ticket");
    bool denied = false;
    try { (void)queue.run_one(); }
    catch (const std::runtime_error&) { denied = true; }
    check(denied && queue.pending() == 1 &&
          observer->retired.load(std::memory_order_acquire) == 0 &&
          lease.context().valid(),
          "self-join finalization lost its ticket or retired before quiescence");
  }
  check(queue.drain_ready() == 1 && queue.pending() == 0 &&
        observer->retired.load(std::memory_order_acquire) == 1,
        "failed self-join was not retried by external supervisor");
}

void competing_supervisor_drainers_are_exclusive() {
  for (std::uint64_t round=0; round<32; ++round) {
    ActorFinalizationQueue<4> queue;
    std::array<std::shared_ptr<Mailbox>,4> actors;
    std::array<std::shared_ptr<Observer>,4> observers;
    for (std::size_t i=0; i<actors.size(); ++i) {
      actors[i] = std::make_shared<Mailbox>(600 + round*4 + i);
      observers[i] = std::make_shared<Observer>();
      check(actors[i]->context().attach_owned_resource(observers[i]),
            "cannot register concurrent-supervisor observer");
      check(queue.try_request_close(actors[i]) ==
                ActorFinalizationQueue<4>::Admission::Queued,
            "concurrent-supervisor ticket rejected");
    }
    std::latch go{1};
    std::atomic<unsigned> drained{0};
    std::array<std::thread,4> workers;
    for (auto& worker : workers)
      worker = std::thread([&] {
        go.wait();
        if (queue.run_one()) drained.fetch_add(1, std::memory_order_relaxed);
      });
    go.count_down();
    for (auto& worker : workers) worker.join();
    unsigned retired = 0;
    for (const auto& observer : observers)
      retired += static_cast<unsigned>(observer->retired.load());
    check(drained.load() == 4 && retired == 4 && queue.completed() == 4 &&
          queue.stale() == 0 && queue.pending() == 0,
          "supervisor workers double-claimed or dropped a retained ticket");
  }
}

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
    completed_stop_cannot_create_duplicate_receipt();
    parallel_duplicate_stop_admission();
    processing_ticket_is_reserved_and_escalatable();
    self_leased_finalizer_retries_without_losing_ticket();
    competing_supervisor_drainers_are_exclusive();
    duplicate_requests_and_supervisor_completion();
    stop_cause_receipts_preserve_priority();
    retirement_callback_reentry_does_not_deadlock_queue();
    queue_owns_actor_until_completion();
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
