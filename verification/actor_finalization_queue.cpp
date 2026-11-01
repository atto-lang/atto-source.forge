#include "runtime/actor_finalizer.hpp"
#include "runtime/shared_i64_table.hpp"
#include <atomic>
#include <array>
#include <exception>
#include <cstdlib>
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ORES_FORGE_ASAN_BUILD 1
#endif
#endif
#if defined(__unix__) && !defined(__SANITIZE_ADDRESS__) && !defined(ORES_FORGE_ASAN_BUILD)
#include <sys/wait.h>
#include <unistd.h>
#endif
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

void invalid_cause_does_not_stop_actor() {
  ActorFinalizationQueue<1> queue;
  auto actor = std::make_shared<Mailbox>(212);
  const auto prior = actor->context();
  auto observer = std::make_shared<Observer>();
  check(prior.attach_owned_resource(observer), "failed to attach invalid-cause observer");
  check(queue.try_request_close(actor, static_cast<ActorStopCause>(255)) ==
            ActorFinalizationQueue<1>::Admission::Invalid,
        "unknown stop cause was accepted");
  check(prior.valid() && actor->execution_lease().context().valid() &&
        observer->retired.load(std::memory_order_acquire) == 0 &&
        queue.pending() == 0,
        "invalid stop reason changed actor authority or consumed ticket");
}

void undrained_queue_destructor_is_fail_stop() {
#if defined(__unix__) && !defined(__SANITIZE_ADDRESS__) && !defined(ORES_FORGE_ASAN_BUILD)
  const pid_t child = fork();
  check(child >= 0, "cannot fork pending-ticket destructor death test");
  if (child == 0) {
    std::set_terminate([] { std::_Exit(74); });
    {
      ActorFinalizationQueue<1> queue;
      auto actor = std::make_shared<Mailbox>(213);
      if (queue.try_request_close(actor) !=
          ActorFinalizationQueue<1>::Admission::Queued)
        std::_Exit(75);
      // Queue destruction with retained cleanup must fail-stop instead of
      // silently losing the last strong actor ownership reference.
    }
    std::_Exit(0);
  }
  int status = 0;
  check(waitpid(child, &status, 0) == child,
        "pending-ticket destructor child was not reaped");
  check(WIFEXITED(status) && WEXITSTATUS(status) == 74,
        "undrained finalizer queue did not fail-stop");
#endif
}

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
  // A full queue must still recognize a previously completed incarnation,
  // and not convert its harmless retry into capacity backpressure.
  auto unrelated = std::make_shared<Mailbox>(214);
  check(queue.try_request_close(unrelated) ==
            ActorFinalizationQueue<1>::Admission::Queued,
        "cannot fill finalization queue for completed-stop test");
  check(queue.try_request_close(actor) ==
            ActorFinalizationQueue<1>::Admission::AlreadyFinalized &&
        queue.pending() == 1,
        "full queue misclassified an already retired incarnation");
  check(queue.drain_ready() == 1 && queue.completed() == 2,
        "unrelated full-capacity ticket did not complete");
  (void)actor->restart();
  check(queue.try_request_close(actor) == ActorFinalizationQueue<1>::Admission::Queued,
        "new incarnation incorrectly rejected as already finalized");
  check(queue.run_one(&receipt) && receipt.incarnation == 2 &&
        receipt.finalized && queue.completed() == 3,
        "new incarnation was not independently finalized");
  check(observer->retired.load(std::memory_order_acquire) == 1,
        "old incarnation observer was delivered twice");
}

void external_supervisor_completion_is_a_deduplicated_ticket() {
  ActorFinalizationQueue<1> queue;
  auto actor = std::make_shared<Mailbox>(809);
  auto observer = std::make_shared<Observer>();
  check(actor->context().attach_owned_resource(observer),
        "could not attach external-supervisor observer");
  check(queue.try_request_close(actor) == ActorFinalizationQueue<1>::Admission::Queued,
        "could not retain ticket before direct supervisor close");
  actor->close(); // A different trusted supervisor wins retirement.
  ActorFinalizationQueue<1>::Completion receipt;
  check(queue.run_one(&receipt) && !receipt.finalized && receipt.deduplicated &&
        receipt.actor_id == 809 && receipt.incarnation == 1 &&
        queue.completed() == 0 && queue.duplicates() == 1 &&
        queue.stale() == 0 && queue.pending() == 0 &&
        observer->retired.load(std::memory_order_acquire) == 1,
        "direct supervisor completion was misreported as a stale incarnation");
  check(queue.try_request_close(actor) ==
            ActorFinalizationQueue<1>::Admission::AlreadyFinalized,
        "post-completion admission produced a second ticket");
}

void concurrent_direct_close_and_queue_drain_do_not_double_retire() {
  ActorFinalizationQueue<1> queue;
  auto actor = std::make_shared<Mailbox>(810);
  std::latch callback_entered{1}, release_callback{1};
  auto observer = std::make_shared<BlockingObserver>(
      callback_entered, release_callback);
  check(actor->context().attach_owned_resource(observer),
        "could not attach competing supervisors observer");
  check(queue.try_request_close(actor) == ActorFinalizationQueue<1>::Admission::Queued,
        "could not queue competing-supervisors stop");
  std::thread direct([&] { actor->close(); });
  callback_entered.wait(); // Direct supervisor won cleanup and is in callback.
  ActorFinalizationQueue<1>::Completion receipt;
  std::atomic<bool> queue_processed{false};
  std::thread worker([&] {
    queue_processed.store(queue.run_one(&receipt), std::memory_order_release);
  });
  release_callback.count_down();
  worker.join();
  direct.join();
  check(queue_processed.load(std::memory_order_acquire) &&
        !receipt.finalized && receipt.deduplicated &&
        observer->callbacks.load(std::memory_order_acquire) == 1 &&
        queue.completed() == 0 && queue.duplicates() == 1 &&
        queue.stale() == 0 && queue.pending() == 0,
        "concurrent direct close/queue drain produced duplicate retirement");
}

void competing_independent_queues_issue_one_completion() {
  // Independent supervisors must not each claim a successful retirement
  // receipt for the same actor incarnation, even under simultaneous drain.
  for (std::uint64_t round = 0; round < 32; ++round) {
    auto actor = std::make_shared<Mailbox>(900 + round);
    auto observer = std::make_shared<Observer>();
    check(actor->context().attach_owned_resource(observer),
          "multi-supervisor observer admission failed");
    std::array<ActorFinalizationQueue<1>,4> queues;
    std::array<ActorFinalizationQueue<1>::Completion,4> receipts{};
    for (auto& queue : queues) {
      check(queue.try_request_close(actor) ==
                ActorFinalizationQueue<1>::Admission::Queued,
            "independent supervisor failed to retain its stop ticket");
    }
    std::latch go{1};
    std::array<std::thread,4> workers;
    std::atomic<unsigned> processed{0};
    for (std::size_t i = 0; i < queues.size(); ++i) {
      workers[i] = std::thread([&,i] {
        go.wait();
        if (queues[i].run_one(&receipts[i]))
          processed.fetch_add(1, std::memory_order_release);
      });
    }
    go.count_down();
    for (auto& worker : workers) worker.join();
    unsigned successful = 0;
    unsigned duplicates = 0;
    for (std::size_t i=0; i<queues.size(); ++i) {
      const auto& receipt = receipts[i];
      check(receipt.actor_id == 900 + round && receipt.incarnation == 1,
            "independent supervisor produced an incorrect incarnation receipt");
      if (receipt.finalized) {
        ++successful;
        check(!receipt.deduplicated,
              "retirement winner was also marked as duplicate");
      } else {
        ++duplicates;
        check(receipt.deduplicated,
              "cross-supervisor loser was incorrectly classified as stale");
      }
      check(queues[i].pending() == 0,
            "multi-supervisor queue leaked an already claimed ticket");
    }
    check(processed.load(std::memory_order_acquire) == 4 &&
          successful == 1 && duplicates == 3 &&
          observer->retired.load(std::memory_order_acquire) == 1,
          "independent supervisors double-reported successful finalization");
    unsigned unique_completions = 0;
    unsigned duplicate_completions = 0;
    for (const auto& queue : queues) {
      check(queue.completed() + queue.duplicates() + queue.stale() == 1 &&
            queue.stale() == 0,
            "independent supervisor misclassified its ticket outcome");
      unique_completions += static_cast<unsigned>(queue.completed());
      duplicate_completions += static_cast<unsigned>(queue.duplicates());
    }
    check(unique_completions == 1 && duplicate_completions == 3,
          "independent queues counted duplicate retirements as successful");
  }
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

void stale_generation_stop_has_no_side_effects() {
  Mailbox actor{1200};
  const auto old = actor.ref().generation;
  const auto replacement = actor.restart();
  const auto current = actor.context();
  auto observer = std::make_shared<Observer>();
  check(current.attach_owned_resource(observer),
        "could not attach replacement generation observer");
  check(old != replacement.generation &&
        !actor.request_close_if_generation(0) &&
        !actor.request_close_if_generation(old),
        "stale or zero generation unexpectedly admitted shutdown");
  check(current.valid() && actor.execution_lease().context() == current &&
        observer->retired.load(std::memory_order_acquire) == 0,
        "stale generation request changed replacement's authority");
  check(actor.request_close_if_generation(replacement.generation),
        "current generation could not request shutdown");
  check(!current.valid() && observer->retired.load(std::memory_order_acquire) == 0,
        "admitted request ran cleanup callbacks or exposed the stopped actor");
  check(actor.finalize_stop(replacement.generation) &&
        !actor.finalize_stop(replacement.generation) &&
        observer->retired.load(std::memory_order_acquire) == 1,
        "same generation did not finalize exactly once");
}

void concurrent_restart_stop_never_fences_replacement() {
  for (std::uint64_t round=0; round<96; ++round) {
    auto actor = std::make_shared<Mailbox>(1300 + round);
    const auto prior = actor->ref().generation;
    std::latch start{1};
    std::atomic<bool> restart_succeeded{false};
    std::atomic<bool> restart_cancelled{false};
    std::atomic<bool> request_accepted{false};
    std::atomic<bool> unexpected{false};
    std::thread restart([&] {
      start.wait();
      try {
        (void)actor->restart();
        restart_succeeded.store(true, std::memory_order_release);
      } catch (const std::runtime_error&) {
        restart_cancelled.store(true, std::memory_order_release);
      } catch (...) {
        unexpected.store(true, std::memory_order_release);
      }
    });
    std::thread stop([&] {
      start.wait();
      request_accepted.store(actor->request_close_if_generation(prior),
                             std::memory_order_release);
    });
    start.count_down();
    restart.join();
    stop.join();
    check(!unexpected.load() &&
          (restart_succeeded.load() != restart_cancelled.load()),
          "concurrent restart did not reach one terminal result");
    const auto after = actor->ref().generation;
    if (restart_succeeded.load()) {
      // Whether old stop won early or its snapshot became stale,
      // a successfully published new generation must stay live.
      check(after == prior + 1 && actor->context().valid(),
            "stale stop request killed successfully restarted actor");
      const auto fresh = actor->execution_lease();
      check(fresh.context().incarnation() == after,
            "restart did not admit new-generation execution");
    } else {
      check(request_accepted.load() && after == prior,
            "restart was cancelled without a current-generation stop");
      bool rejected = false;
      try { (void)actor->context(); }
      catch (const std::runtime_error&) { rejected = true; }
      check(rejected, "cancelled restart left stopped generation executable");
    }
  }
}

void queue_restart_race_never_shuts_new_generation() {
  for (std::uint64_t round=0; round<64; ++round) {
    ActorFinalizationQueue<1> queue;
    auto actor = std::make_shared<Mailbox>(1500 + round);
    std::latch go{1};
    std::atomic<bool> restart_succeeded{false};
    std::atomic<bool> restart_cancelled{false};
    std::atomic<int> admission{-1};
    std::thread restarter([&] {
      go.wait();
      try {
        (void)actor->restart();
        restart_succeeded.store(true, std::memory_order_release);
      } catch (const std::runtime_error&) {
        restart_cancelled.store(true, std::memory_order_release);
      }
    });
    std::thread requester([&] {
      go.wait();
      const auto result = queue.try_request_close(actor);
      admission.store(static_cast<int>(result), std::memory_order_release);
    });
    go.count_down();
    restarter.join();
    requester.join();
    using Admission = ActorFinalizationQueue<1>::Admission;
    const auto outcome = static_cast<Admission>(admission.load());
    check(outcome == Admission::Queued || outcome == Admission::Raced ||
          outcome == Admission::AlreadyFinalized,
          "queue restart race returned unexpected admission outcome");
    if (restart_succeeded.load())
      check(actor->ref().generation == 2,
            "restart reported success without advancing the generation");
    else
      check(restart_cancelled.load() && outcome == Admission::Queued &&
            actor->ref().generation == 1,
            "queue canceled restart without retaining matching stop ticket");

    ActorFinalizationQueue<1>::Completion receipt;
    const bool processed = queue.run_one(&receipt);
    if (outcome == Admission::Queued) {
      check(processed, "an admitted stop lost its finalization ticket");
      if (restart_succeeded.load() && receipt.incarnation == 1) {
        // Old ticket is stale; no later generation may be affected.
        check(!receipt.finalized && !receipt.deduplicated &&
              actor->context().valid(),
              "stale ticket revoked a successfully restarted actor");
      } else if (restart_succeeded.load() && receipt.incarnation == 2) {
        // The requester observed generation 2 AFTER restart. Stopping that
        // generation is expected; it is not a stale-generation violation.
        check(receipt.finalized && !receipt.deduplicated &&
              actor->stop_finalized(2),
              "current-generation stop did not retire the restarted actor");
      } else {
        check(!restart_succeeded.load() && receipt.incarnation == 1 &&
              receipt.finalized, "cancelled restart lost old-generation cleanup");
      }
    } else {
      check(!processed && restart_succeeded.load() &&
            actor->context().valid(),
            "raced or already finalized admission affected a new actor");
    }
    check(queue.pending() == 0, "queue race retained an undrained ticket");
  }
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
  check(queue.stale() == 1 && queue.completed() == 0 &&
        queue.duplicates() == 0,
        "obsolete generation ticket was incorrectly counted as a duplicate");
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
    invalid_cause_does_not_stop_actor();
    undrained_queue_destructor_is_fail_stop();
    completed_stop_cannot_create_duplicate_receipt();
    external_supervisor_completion_is_a_deduplicated_ticket();
    concurrent_direct_close_and_queue_drain_do_not_double_retire();
    competing_independent_queues_issue_one_completion();
    parallel_duplicate_stop_admission();
    processing_ticket_is_reserved_and_escalatable();
    self_leased_finalizer_retries_without_losing_ticket();
    competing_supervisor_drainers_are_exclusive();
    duplicate_requests_and_supervisor_completion();
    stop_cause_receipts_preserve_priority();
    retirement_callback_reentry_does_not_deadlock_queue();
    queue_owns_actor_until_completion();
    bounded_admission_cannot_lose_stop();
    stale_generation_stop_has_no_side_effects();
    concurrent_restart_stop_never_fences_replacement();
    queue_restart_race_never_shuts_new_generation();
    stale_ticket_does_not_stop_restarted_actor();
    leased_shutdown_is_not_observer_callback();
    rejection_and_generation_reuse();
    std::cout << "PASS: bounded finalization, 32x8 duplicate races, "
                 "32x4 supervisor races, priority escalation, "
                 "completed-generation dedup, fail-stop ticket retention\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
