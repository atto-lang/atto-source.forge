#include "runtime/ActorIncarnationOwnerV1.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace oresforge::runtime;

static void require(bool ok, const char* reason) {
  if (!ok) throw std::runtime_error(reason);
}
template <typename F> static void denied(F&& f, const char* reason) {
  try { f(); } catch (const std::exception&) { return; }
  throw std::runtime_error(reason);
}

int main() {
  ActorMemoryAggregateBudgetV1 budget(1024);
  const auto initial = ActorMemoryAggregateBudgetV1::process().reserved();
  // Each actor reserves exactly 256 heap bytes + 64 frame bytes.
  ActorIncarnationOwnerV1 actorA(8401, budget, 256, 64, 4, 2);
  ActorIncarnationOwnerV1 actorB(8402, budget, 256, 64, 4, 2);
  const auto refA = actorA.ref();
  const auto refB = actorB.ref();
  require(budget.reserved() == 640 &&
          ActorMemoryAggregateBudgetV1::process().reserved() == initial + 640,
          "independent actor incarnation budgets were not reserved");

  std::optional<ActorMemoryHeapHandle> staleHeap;
  std::array<std::uint8_t,3> msgA{42, 8, 3};
  actorA.send(refA, msgA);
  msgA[0] = 1; // mailbox must own a deep copy
  require(actorA.queued() == 1, "one bounded actor message expected");
  require(actorA.dispatch_one(refA, [&](auto& ctx,
                                      std::span<const std::uint8_t> message) {
    require(message.size() == 3 && message[0] == 42,
            "mailbox message was not copied");
    auto h = ctx.heap_alloc(8);
    staleHeap = h;
    ctx.heap_write(h, 0, std::array<std::uint8_t,1>{42});
    auto e = ctx.begin_frames();
    auto frame = ctx.frame_push(e, 16);
    std::array<std::uint8_t,1> result{};
    ctx.heap_read(h, 0, result);
    require(result[0] == 42, "actor heap bytes missing");
    ctx.frame_write(e, frame, 0, result);
    ctx.frame_read(e, frame, 0, result);
    require(result[0] == 42, "actor frame bytes missing");

    // All actor memory operations require the original carrier and turn.
    std::atomic<int> rejected{0};
    std::thread outsider([&] {
      std::array<std::uint8_t,1> foreignResult{};
      // All three operations must authenticate the carrier before touching
      // private native state, including allocating fresh heap credits.
      try { ctx.heap_read(h, 0, foreignResult); }
      catch (const std::logic_error&) { ++rejected; }
      try { (void)ctx.heap_alloc(1); }
      catch (const std::logic_error&) { ++rejected; }
      try { ctx.frame_read(e, frame, 0, foreignResult); }
      catch (const std::logic_error&) { ++rejected; }
    });
    outsider.join();
    require(rejected == 3,
            "foreign carrier used heap read/allocation or guest frame access");
    ctx.frame_read(e, frame, 0, result);
    require(result[0] == 42,
            "rejected off-carrier operations corrupted the valid actor frame");

    // Nested dispatch may not reenter an actor on the same carrier.
    denied([&] { actorA.dispatch_one(refA,
          [](auto&, auto) {}); }, "reentrant actor dispatch was admitted");
    ctx.frame_pop(e, frame);
    ctx.finish_frames(e);
  }), "actor A's message was not dispatched");
  require(actorA.queued() == 0 && !actorA.stopped(),
          "healthy message dispatch did not complete");

  actorB.send(refB, std::array<std::uint8_t,1>{9});
  require(actorB.dispatch_one(refB, [&](auto& ctx, auto) {
    auto e = ctx.begin_frames();
    auto frame = ctx.frame_push(e, 8);
    ctx.frame_write(e, frame, 0, std::array<std::uint8_t,1>{42});
    std::array<std::uint8_t,1> answer{};
    ctx.frame_read(e, frame, 0, answer);
    require(answer[0] == 42, "sibling actor frame corrupted");
    ctx.frame_pop(e, frame);
    ctx.finish_frames(e);
  }), "actor B's message not processed");

  // STOP races an actively executing handler. Mailbox authority is revoked
  // immediately, but native buffers must remain reserved until the carrier
  // leaves the physical turn. No UAF, no early quota refund.
  std::atomic<bool> entered{false}, release{false}, stoppedAccess{false};
  std::exception_ptr workerError;
  actorA.send(refA, std::array<std::uint8_t,1>{1});
  std::thread worker([&] {
    try {
      actorA.dispatch_one(refA, [&](auto& ctx, auto) {
        auto h = ctx.heap_alloc(8);
        entered.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire))
          std::this_thread::yield();
        try { std::array<std::uint8_t,1> out{};
              ctx.heap_read(h, 0, out); }
        catch (const std::logic_error&) {
          stoppedAccess.store(true, std::memory_order_release);
        }
      });
    } catch (...) { workerError = std::current_exception(); }
  });
  const auto admissionDeadline = std::chrono::steady_clock::now() +
                                 std::chrono::seconds(5);
  while (!entered.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < admissionDeadline)
    std::this_thread::yield();
  if (!entered.load(std::memory_order_acquire)) {
    release.store(true, std::memory_order_release);
    worker.join();
    if (workerError) std::rethrow_exception(workerError);
    throw std::runtime_error("actor worker failed to enter dispatch turn");
  }
  actorA.stop();
  require(actorA.stopped() && budget.reserved() == 640,
          "STOP reclaimed native actor heap during active turn");
  denied([&] { actorA.send(refA, std::array<std::uint8_t,1>{2}); },
         "STOP allowed stale message admission");
  denied([&] { actorA.restart(); },
         "supervisor restarted before carrier quiescence");
  release.store(true, std::memory_order_release);
  worker.join();
  if (workerError) std::rethrow_exception(workerError);
  require(stoppedAccess.load(std::memory_order_acquire) &&
          budget.reserved() == 320,
          "STOP failed to revoke access and defer reclamation until unwind");

  // Supervisor restart creates a new generation and zeroed native memory.
  const auto refA2 = actorA.restart();
  require(refA2.generation == refA.generation + 1 &&
          budget.reserved() == 640, "restart did not reissue actor memory");
  denied([&] { actorA.send(refA, std::array<std::uint8_t,1>{1}); },
         "old generation continued to send after restart");
  denied([&] { actorA.dispatch_one(refA, [](auto&, auto) {}); },
         "old generation started a post-restart actor turn");
  actorA.send(refA2, std::array<std::uint8_t,1>{3});
  require(actorA.dispatch_one(refA2, [&](auto& ctx, auto) {
    std::array<std::uint8_t,1> result{};
    denied([&] { ctx.heap_read(*staleHeap, 0, result); },
           "restarted actor inherited old-generation allocation handle");
    auto h = ctx.heap_alloc(8);
    ctx.heap_read(h, 0, result);
    require(result[0] == 0, "new incarnation heap was not zero-initialized");
  }), "restarted actor did not admit a valid new message");

  // A caught terminal frame fault still stops actor A at the dispatch
  // boundary; B must continue executing independently on the same host.
  actorA.send(refA2, std::array<std::uint8_t,1>{4});
  denied([&] {
    actorA.dispatch_one(refA2, [&](auto& ctx, auto) {
      auto e = ctx.begin_frames();
      (void)ctx.frame_push(e, 48);
      denied([&] { (void)ctx.frame_push(e, 24); },
             "actor frame quota failed to reject overflow");
      // Swallow the quota exception; post-handler validation must still
      // treat this actor as failed and quarantine its memory.
    });
  }, "swallowed actor quota exception escaped handler validation");
  require(actorA.stopped() && budget.reserved() == 320,
          "terminal stack failure did not reclaim only failed actor");

  actorB.send(refB, std::array<std::uint8_t,1>{5});
  require(actorB.dispatch_one(refB, [&](auto& ctx, auto) {
    auto e = ctx.begin_frames();
    auto frame = ctx.frame_push(e, 8);
    ctx.frame_write(e, frame, 0, std::array<std::uint8_t,1>{42});
    std::array<std::uint8_t,1> out{};
    ctx.frame_read(e, frame, 0, out);
    require(out[0] == 42, "actor B failed after actor A stack exhaustion");
    ctx.frame_pop(e, frame);
    ctx.finish_frames(e);
  }), "healthy sibling stopped after failing actor");
  actorB.stop();
  actorA.stop();
  require(budget.reserved() == 0 &&
          ActorMemoryAggregateBudgetV1::process().reserved() == initial,
          "STOP and restart leaked native reservations");

  // A failed successor allocation cannot leave a half-admitted
  // incarnation, steal another actor's quota, or revive prior references.
  ActorMemoryAggregateBudgetV1 restartBudget(320);
  ActorIncarnationOwnerV1 restarting(8403, restartBudget, 256, 64, 4, 2);
  const auto beforeRestart = restarting.ref();
  restarting.stop();
  require(restartBudget.reserved() == 0,
          "stopped actor did not release its native reservation");
  ActorIncarnationOwnerV1 budgetHolder(8404, restartBudget, 256, 64, 4, 2);
  denied([&] { restarting.restart(); },
         "restart succeeded despite aggregate native-memory exhaustion");
  require(restarting.stopped() && restartBudget.reserved() == 320,
          "failed successor allocation left a partially active actor or leaked credit");
  denied([&] { (void)restarting.ref(); },
         "failed restart exposed a live actor reference");
  budgetHolder.stop();
  const auto recovered = restarting.restart();
  require(recovered.generation >= beforeRestart.generation + 2 &&
          restartBudget.reserved() == 320,
          "successor recovery reused denied generation or lost native budget");
  denied([&] { restarting.send(beforeRestart,
                            std::array<std::uint8_t,1>{9}); },
         "old actor reference was revived after quota-denied restart");

  // A thrown native host handler must scrub its message, stop only its
  // incarnation and refund its bytes, even without a frame protocol fault.
  restarting.send(recovered, std::array<std::uint8_t,1>{7});
  denied([&] {
    restarting.dispatch_one(recovered, [](auto&, auto) {
      throw std::runtime_error("synthetic trusted handler exception");
    });
  }, "trusted handler error escaped without actor-local containment");
  require(restarting.stopped() && restartBudget.reserved() == 0 &&
          ActorMemoryAggregateBudgetV1::process().reserved() == initial,
          "handler failure leaked native credits or corrupted process reservation");

  // Sustained actual host-owner lifecycle: keep B alive throughout repeated
  // frame faults in A, validate budget refund at every physical turn exit,
  // and require fresh heap/stack capabilities on every reincarnation.
  {
    ActorMemoryAggregateBudgetV1 lifetimeBudget(640);
    ActorIncarnationOwnerV1 a(8405, lifetimeBudget, 256, 64, 4, 2);
    ActorIncarnationOwnerV1 b(8406, lifetimeBudget, 256, 64, 4, 2);
    auto live = a.ref();
    const auto sibling = b.ref();
    const auto first = live;
    std::optional<ActorMemoryHeapHandle> previousHeap;
    for (std::uint64_t cycle = 0; cycle < 100000; ++cycle) {
      require(lifetimeBudget.reserved() == 640, "active lifetime budget changed");
      a.send(live, std::array<std::uint8_t,1>{7});
      require(a.dispatch_one(live, [&](auto& ctx, auto payload) {
        require(payload.size() == 1 && payload[0] == 7, "new message mismatch");
        std::array<std::uint8_t,1> out{255};
        if (previousHeap) denied([&] { ctx.heap_read(*previousHeap, 0, out); },
                                "retired heap capability was accepted");
        auto heap = ctx.heap_alloc(8);
        ctx.heap_read(heap, 0, out);
        require(out[0] == 0, "replacement heap not zeroed");
        ctx.heap_write(heap, 0, std::array<std::uint8_t,1>{99});
        previousHeap = heap;
        auto e = ctx.begin_frames();
        auto f = ctx.frame_push(e, 16);
        ctx.frame_read(e, f, 0, out);
        require(out[0] == 0, "replacement logical stack not zeroed");
        ctx.frame_pop(e, f);
        ctx.finish_frames(e);
      }), "healthy replacement failed to dispatch");
      a.send(live, std::array<std::uint8_t,1>{8});
      bool faulted = false;
      try {
        a.dispatch_one(live, [](auto& ctx, auto) {
          auto e = ctx.begin_frames();
          (void)ctx.frame_push(e, 64);
          (void)ctx.frame_push(e, 1);
        });
      } catch (const std::runtime_error& error) {
        faulted = std::string_view(error.what()) == "actor logical frame quota exceeded";
      }
      require(faulted, "expected terminal frame quota fault");
      require(a.stopped() && a.queued() == 0 && lifetimeBudget.reserved() == 320,
              "failed actor did not retire while sibling kept its budget");
      const auto retired = live;
      live = a.restart();
      require(live.generation == retired.generation + 1,
              "lifetime restart generation mismatch");
      denied([&] { a.send(retired, std::array<std::uint8_t,1>{1}); },
             "retired incarnation accepted DATA");
      denied([&] { a.send(first, std::array<std::uint8_t,1>{1}); },
             "initial incarnation revived");
      b.send(sibling, std::array<std::uint8_t,1>{42});
      require(b.dispatch_one(sibling, [](auto&, auto payload) {
        require(payload.size() == 1 && payload[0] == 42, "sibling result mismatch");
      }), "sibling stopped progressing");
    }
    a.stop();
    b.stop();
    require(lifetimeBudget.reserved() == 0 &&
            ActorMemoryAggregateBudgetV1::process().reserved() == initial,
            "lifetime test leaked aggregate native credits");
    std::cout << "FORGE_ACTOR_OWNER_LIFETIME_PASS cycles=100000 sibling=42\n";
  }

  std::cout << "FORGE_ACTOR_OWNER_HOST_PASS"
            << "|turn=1|stop_restart=1|sibling=42|guest_atto=0|physical_stack=0\n";
}
