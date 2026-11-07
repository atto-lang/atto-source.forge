#include "runtime/scalar_actor.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>
using Actor = oresforge::runtime::ScalarActor;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "scalar actor failure:%d\n", __LINE__); return 1; } } while (0)
#if defined(ORES_ARENA_TEST_ALLOCATIONS)
static bool forbid_allocation;
extern "C" {
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
void* __wrap_malloc(size_t n) { if (forbid_allocation) abort(); return __real_malloc(n); }
void* __wrap_calloc(size_t n, size_t s) { if (forbid_allocation) abort(); return __real_calloc(n, s); }
void* __wrap_realloc(void* p, size_t n) { if (forbid_allocation) abort(); return __real_realloc(p, n); }
void __wrap_free(void* p) { if (forbid_allocation) abort(); __real_free(p); }
}
void* operator new(std::size_t n) {
    if (forbid_allocation) std::abort();
    if (void* p = __real_malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { if (forbid_allocation) std::abort(); __real_free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#endif
static std::atomic<std::uint64_t> first_domain, second_domain;
static void initialize(OresActorI64Heap* h, std::uint64_t domain, std::uint64_t epoch) {
    ores_actor_i64_heap_field_init_mutable(h, domain, epoch, 0, 0);
    ores_actor_i64_heap_field_init(h, domain, epoch, 1, 42);
    std::uint64_t empty = 0;
    if (!first_domain.compare_exchange_strong(empty, domain)) second_domain = domain;
}
static std::int64_t sum(OresActorI64Heap* h, std::uint64_t domain, std::uint64_t epoch, std::int64_t n) {
    const auto total = ores_actor_i64_heap_field_read(h, domain, epoch, 0) + n;
    ores_actor_i64_heap_field_write(h, domain, epoch, 0, total);
    return total;
}
static std::int64_t fifo(OresActorI64Heap* h, std::uint64_t domain, std::uint64_t epoch, std::int64_t n) {
    const auto total = ores_actor_i64_heap_field_read(h, domain, epoch, 0) * 10 + n;
    ores_actor_i64_heap_field_write(h, domain, epoch, 0, total);
    return total;
}
static std::atomic<bool> entered, finish;
static std::int64_t blocked(OresActorI64Heap* h, std::uint64_t domain, std::uint64_t epoch, std::int64_t n) {
    entered = true;
    while (!finish.load()) std::this_thread::yield();
    return sum(h, domain, epoch, n);
}
static void broken_init(OresActorI64Heap*, std::uint64_t, std::uint64_t) { throw std::runtime_error("constructor failure"); }
static std::int64_t broken_message(OresActorI64Heap*, std::uint64_t, std::uint64_t, std::int64_t) { throw std::runtime_error("handler failure"); }
int main() {
    // Invalid budgets/callbacks must fail before any actor code executes.
    for (int invalid = 0; invalid < 7; ++invalid) {
        bool denied = false;
        try {
            Actor actor(invalid == 0 ? 0 : 1,
                invalid == 1 ? 0 : invalid == 2 ? 4097 : 2,
                invalid == 3 ? 0 : invalid == 4 ? 4097 : 2,
                invalid == 5 ? nullptr : initialize,
                invalid == 6 ? nullptr : sum);
        } catch (const std::invalid_argument&) { denied = true; }
        CHECK(denied);
    }
    // Failed initializer callbacks run on an admitted turn, unwind carrier
    // ownership and seal the actor before destruction reclaims its heap.
    bool failed = false;
    try { Actor broken(1, 2, 2, broken_init, sum); (void)broken.run_one(); }
    catch (const std::runtime_error&) { failed = true; }
    CHECK(failed);
    {
        std::optional<oresforge::runtime::ScalarActorRef> stale;
        { Actor old(17, 2, 2, initialize, fifo); stale = old.ref(); }
        Actor replacement(17, 2, 2, initialize, fifo);
        CHECK(replacement.try_send(*stale, 9) == Actor::Send::Foreign);
        CHECK(replacement.try_send(replacement.ref(), 7) == Actor::Send::Accepted);
        CHECK(replacement.run_one().value == 7);
    }
    {
        first_domain = second_domain = 0;
        Actor first(17, 2, 2, initialize, fifo), second(17, 2, 2, initialize, fifo);
        const auto a = first.ref(), b = second.ref();
        CHECK(first.try_send(b, 9) == Actor::Send::Foreign);
        CHECK(first.try_send(a, 1) == Actor::Send::Accepted);
        CHECK(first.try_send(a, 2) == Actor::Send::Accepted);
        CHECK(first.try_send(a, 3) == Actor::Send::Full);
        CHECK(second.try_send(b, 8) == Actor::Send::Accepted);
        CHECK(first.run_one().value == 1);
        std::int64_t migrated = 0;
        std::thread carrier([&] { migrated = first.run_one().value; });
        carrier.join();
        CHECK(migrated == 12 && second.run_one().value == 8);
        CHECK(first_domain != 0 && second_domain != 0 && first_domain != second_domain);
        first.stop();
        CHECK(first.done() && first.run_one().status == Actor::Turn::Stopped);
        CHECK(first.try_send(a, 1) == Actor::Send::Closed);
        CHECK(first.try_send(b, 1) == Actor::Send::Foreign);
        CHECK(second.run_one().status == Actor::Turn::Empty);
    }
    {
        Actor cold(22, 2, 8, initialize, sum);
        CHECK(cold.run_one().status == Actor::Turn::Initialized);
        CHECK(cold.run_one().status == Actor::Turn::Empty);
        CHECK(cold.try_send(cold.ref(), 9) == Actor::Send::Accepted);
        CHECK(cold.run_one().value == 9);
        cold.stop();
        CHECK(cold.done());
        Actor canceled(23, 2, 8, broken_init, sum);
        canceled.stop(); // cancel before startup must not execute initializer
        CHECK(canceled.run_one().status == Actor::Turn::Stopped && canceled.done());
    }
    {
        Actor actor(18, 2, 8, initialize, blocked);
        const auto ref = actor.ref();
        CHECK(actor.try_send(ref, 1) == Actor::Send::Accepted);
        Actor::Result result{Actor::Turn::Empty};
        std::thread carrier([&] { result = actor.run_one(); });
        while (!entered.load()) std::this_thread::yield();
        CHECK(actor.run_one().status == Actor::Turn::Busy);
        CHECK(actor.try_send(ref, 2) == Actor::Send::Accepted);
        actor.stop(); // no guest callback holds the ingress/lifecycle mutex
        CHECK(!actor.done() && actor.queued() == 1);
        CHECK(actor.try_send(ref, 3) == Actor::Send::Closed);
        finish = true;
        carrier.join();
        CHECK(result.status == Actor::Turn::Executed && result.value == 1);
        CHECK(actor.run_one().value == 3 && actor.done());
    }
    {
        Actor actor(19, 2, 4096, initialize, sum);
        const auto ref = actor.ref();
        std::vector<std::thread> producers;
        std::atomic<bool> bad = false;
        for (int i = 0; i < 4; ++i) producers.emplace_back([&] {
            for (int j = 0; j < 128; ++j)
                if (actor.try_send(ref, 1) != Actor::Send::Accepted) bad = true;
        });
        for (auto& p : producers) p.join();
        CHECK(!bad && actor.queued() == 512);
        actor.stop();
        std::atomic<unsigned> completed = 0;
        std::atomic<std::int64_t> results = 0;
        std::vector<std::thread> workers;
        for (int i = 0; i < 4; ++i) workers.emplace_back([&] {
            while (!actor.done()) {
                const auto result = actor.run_one();
                if (result.status == Actor::Turn::Executed) { ++completed; results += result.value; }
                else std::this_thread::yield();
            }
        });
        for (auto& w : workers) w.join();
        CHECK(completed == 512 && results == 512 * 513 / 2 && actor.done());
    }
    {
        Actor actor(20, 2, 8, initialize, sum);
        const auto ref = actor.ref();
#if defined(ORES_ARENA_TEST_ALLOCATIONS)
        forbid_allocation = true;
#endif
        for (int i = 1; i <= 16384; ++i) {
            CHECK(actor.try_send(ref, 1) == Actor::Send::Accepted);
            const auto result = actor.run_one();
            CHECK(result.status == Actor::Turn::Executed && result.value == i);
        }
        actor.stop();
        CHECK(actor.done());
#if defined(ORES_ARENA_TEST_ALLOCATIONS)
        forbid_allocation = false;
#endif
    }
    {
        Actor actor(21, 2, 8, initialize, broken_message);
        const auto ref = actor.ref();
        CHECK(actor.try_send(ref, 1) == Actor::Send::Accepted);
        CHECK(actor.try_send(ref, 2) == Actor::Send::Accepted);
        failed = false;
        try { (void)actor.run_one(); } catch (const std::runtime_error&) { failed = true; }
        CHECK(failed && actor.done() && actor.queued() == 0);
        CHECK(actor.run_one().status == Actor::Turn::Stopped);
        CHECK(actor.try_send(ref, 3) == Actor::Send::Closed);
    }
    std::puts("Forge scalar actor: isolation, FIFO, migration, stop/drain, 512 contended messages and 16384 allocation-free turns passed");
    return 0;
}
