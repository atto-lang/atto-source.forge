#include "compiler/actor_subset.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using oresforge::actors::ActorExecutionContext;
using oresforge::actors::compile;
namespace {
void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
template<class E, class F> void reject(F&& fn) {
    bool failed = false;
    try { fn(); } catch (const E&) { failed = true; }
    check(failed, "required negative control was accepted");
}
constexpr const char* counter = R"atto(
define actor Counter as
  state { count: int = 0; }
  pub fnc on_message(int delta): int {
    self.count = self.count + delta;
    return self.count;
  }
end
)atto";
constexpr const char* recursion = R"atto(
define actor Counter as
  pub fnc on_message(int n): int { return countdown(n); }
  pub fnc countdown(int n): int {
    if n == 0 then return 0; else return countdown(n - 1); fi
  }
end
)atto";
}
int main() {
    const auto p = compile(counter);
    auto& ledger = oresforge::runtime::ActorMemoryAggregateBudgetV1::process();
    const auto initial_reserved = ledger.reserved();
    {
        ActorExecutionContext actor(p, 401);
        check(ledger.reserved() == initial_reserved + 128 * 1024,
              "queued actor did not reserve process memory");
        const auto cap = actor.incarnation_ref();
        check(!actor.dispatch_next(), "empty mailbox executed a callback");
        actor.enqueue(cap, 3);
        actor.enqueue(cap, 4);
        check(actor.dispatch_next() == 3, "queued actor first FIFO reply");
        check(actor.dispatch_next() == 7, "queued actor second FIFO reply");
        check(!actor.dispatch_next(), "queue did not empty after processing");
        for (int i = 0; i < 8; ++i) actor.enqueue(cap, 1);
        reject<std::length_error>([&] { actor.enqueue(cap, 1); });
        for (std::int64_t i = 8; i <= 15; ++i)
            check(actor.dispatch_next() == i, "bounded queued actor order");
        check(!actor.dispatch_next(), "bounded queue retained a message");
        // No scalar/actor heap data may be used by a foreign incarnation.
        ActorExecutionContext neighbor(p, 402);
        const auto other = neighbor.incarnation_ref();
        reject<std::runtime_error>([&] { actor.enqueue(other, 4); });
        check(!actor.dispatch_next(), "foreign send entered actor mailbox");
        neighbor.enqueue(other, 8);
        check(neighbor.dispatch_next() == 8, "healthy peer actor callback");
        // The host transport does not execute callbacks before the worker
        // explicitly pumps the queue; sends remain allowed across carriers.
        ActorExecutionContext migrate(p, 403);
        auto migrate_cap = migrate.incarnation_ref();
        std::thread producer([&] { migrate.enqueue(migrate_cap, 9); });
        producer.join();
        std::atomic<bool> worked = false;
        std::thread worker([&] {
            try { worked = migrate.dispatch_next() == 9; }
            catch (...) { worked = false; }
        });
        worker.join();
        check(worked, "different carrier could not run a queued turn");
        // Direct host callbacks and the queued source-main transport must
        // not silently mix reply identities on the same incarnation.
        reject<std::logic_error>([&] { (void)actor.receive(1); });
        ActorExecutionContext direct(p, 404);
        check(direct.receive(1) == 1, "direct legacy profile regressed");
        reject<std::logic_error>([&] {
            direct.enqueue(direct.incarnation_ref(), 1);
        });
        actor.stop();
        check(actor.done(), "STOP did not quiesce queued actor");
        reject<std::runtime_error>([&] { actor.enqueue(cap, 1); });
        reject<std::logic_error>([&] { (void)actor.dispatch_next(); });
        // A new actor reusing the numeric id has a different opaque
        // mailbox authority; old generations cannot be revalidated.
        ActorExecutionContext restarted(p, 401);
        reject<std::runtime_error>([&] { restarted.enqueue(cap, 123); });
        restarted.enqueue(restarted.incarnation_ref(), 2);
        check(restarted.dispatch_next() == 2, "restart inherited stale state");
        restarted.stop();
        direct.stop(); neighbor.stop(); migrate.stop();
    }
    check(ledger.reserved() == initial_reserved,
          "queued actor memory reservation failed to refund");

    // Concurrent publishers share a bounded FIFO admission domain. Each
    // publisher enqueues two identical messages; aggregate checked state
    // is deterministic even when cross-publisher scheduling order varies.
    {
        ActorExecutionContext actor(p, 410);
        const auto capability = actor.incarnation_ref();
        std::atomic<bool> go = false, producers_ok = true;
        std::vector<std::thread> producers;
        for (int t = 0; t < 4; ++t) {
            producers.emplace_back([&] {
                while (!go.load(std::memory_order_acquire))
                    std::this_thread::yield();
                try {
                    actor.enqueue(capability, 1);
                    actor.enqueue(capability, 1);
                } catch (...) { producers_ok = false; }
            });
        }
        go.store(true, std::memory_order_release);
        for (auto& thread : producers) thread.join();
        check(producers_ok, "concurrent bounded publisher denied unexpectedly");
        for (int i = 1; i <= 8; ++i)
            check(actor.dispatch_next() == i, "concurrent queued FIFO aggregate");
        check(!actor.dispatch_next(), "concurrent producer queue left residual work");
        actor.stop();
    }
    check(ledger.reserved() == initial_reserved,
          "concurrent publisher actor leaked budget");

    // Every terminal actor frame fault revokes queued ingress, drops accepted
    // pending payloads, and leaves other actor incarnations in the process live.
    {
        ActorExecutionContext bad(compile(recursion), 420);
        ActorExecutionContext good(p, 421);
        const auto bad_ref = bad.incarnation_ref();
        bad.enqueue(bad_ref, 64);
        bad.enqueue(bad_ref, 1); // pending, must be wiped at terminal fault
        good.enqueue(good.incarnation_ref(), 11);
        reject<std::length_error>([&] { (void)bad.dispatch_next(); });
        check(bad.done(), "faulting actor retained a pending mailbox payload");
        reject<std::runtime_error>([&] { bad.enqueue(bad_ref, 3); });
        check(good.dispatch_next() == 11,
              "neighbor actor did not survive terminal frame exhaustion");
        good.enqueue(good.incarnation_ref(), -2);
        check(good.dispatch_next() == 9, "neighbor actor state lost after fault");
        good.stop();
    }
    check(ledger.reserved() == initial_reserved,
          "terminal actor fault did not refund budget at teardown");

    // All 64 bits of signed message payloads must survive mailbox transport.
    {
        ActorExecutionContext actor(p, 440);
        actor.enqueue(actor.incarnation_ref(), std::numeric_limits<std::int64_t>::min());
        check(actor.dispatch_next() == std::numeric_limits<std::int64_t>::min(),
              "negative i64 message bits corrupted");
        actor.enqueue(actor.incarnation_ref(), std::numeric_limits<std::int64_t>::max());
        check(actor.dispatch_next() == -1,
              "positive i64 message bits corrupted");
        actor.stop();
    }
    check(ledger.reserved() == initial_reserved,
          "signed-message actor leaked budget");
    std::puts("ATTO_FORGE_ACTOR_QUEUED_MAILBOX|capacity=8|fifo=1|concurrent_publishers=4|fault_fence=1|same_id_restart=1|cross_carrier=1|process_budget_refund=1|async_guest=NOT_EXECUTED|native_aot=NOT_EXECUTED");
}
