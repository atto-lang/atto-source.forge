#include "runtime/ActorFrameArena.hpp"
#include "runtime/scalar_actor.hpp"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

using oresforge::runtime::ActorFrameArena;
using oresforge::runtime::ScalarActor;

template<class F> void denies(F f) {
    bool caught = false;
    try { f(); } catch (const std::exception&) { caught = true; }
    assert(caught);
}
static void init(OresActorI64Heap*, std::uint64_t, std::uint64_t) {
    auto access = ActorFrameArena::current();
    assert(access);
    auto frame = access->arena->push(access->turn, 4);
    access->arena->write(access->turn, frame, 0, 42);
    assert(access->arena->read(access->turn, frame, 0) == 42);
    access->arena->pop(access->turn, frame);
}
static std::int64_t handle(OresActorI64Heap*, std::uint64_t, std::uint64_t, std::int64_t x) {
    auto access = ActorFrameArena::current();
    assert(access);
    auto frame = access->arena->push(access->turn, 4);
    access->arena->write(access->turn, frame, 0, static_cast<std::uint8_t>(x));
    access->arena->pop(access->turn, frame);
    return x + 1;
}
static std::int64_t swallow_overflow(OresActorI64Heap*, std::uint64_t, std::uint64_t, std::int64_t x) {
    auto access = ActorFrameArena::current();
    assert(access);
    try {
        (void)access->arena->push(access->turn, 64 * 1024);
        (void)access->arena->push(access->turn, 1);
    } catch (const ActorFrameArena::Exhausted&) {}
    return x; // Actor must stop even if this host callback swallows the trap.
}
int main() {
    // Same numeric actor ID with different incarnation: independent domains.
    ActorFrameArena a(7, 1, 3, 12), b(7, 2, 3, 12);
    auto ta = a.enter(100), tb = b.enter(100);
    auto fa = a.push(ta, 8), fb = b.push(tb, 8);
    a.write(ta, fa, 0, 10); b.write(tb, fb, 0, 22);
    assert(a.read(ta, fa, 0) == 10 && b.read(tb, fb, 0) == 22);
    a.pop(ta, fa); b.pop(tb, fb); a.leave(ta); b.leave(tb);
    assert(a.used_bytes() == 0 && b.used_bytes() == 0);

    // Typed frame quota fault affects this actor, not its sibling.
    auto t = a.enter(101);
    auto f = a.push(t, 8);
    denies([&] { (void)a.push(t, 5); });
    assert(a.poisoned());
    denies([&] { a.write(t, f, 0, 1); });
    auto tb2 = b.enter(101); auto fb2 = b.push(tb2, 2);
    b.write(tb2, fb2, 0, 49);
    assert(b.read(tb2, fb2, 0) == 49);
    b.pop(tb2, fb2); b.leave(tb2);
    assert(!a.cancel_quiesced()); // cannot reclaim a still-admitted carrier

    // Retained bytes and stale-turn poison during suspension.
    ActorFrameArena c(8, 1, 3, 32);
    auto tc = c.enter(901); auto fc = c.push(tc, 4);
    c.write(tc, fc, 0, 77);
    auto suspended = c.suspend(tc); (void)suspended;
    assert(c.depth() == 1 && c.used_bytes() == 4);
    denies([&] { (void)c.read(tc, fc, 0); });
    assert(c.poisoned());
    assert(c.cancel_quiesced());

    // A valid suspended logical execution resumes on a different carrier.
    ActorFrameArena d(8, 1, 3, 32);
    auto td = d.enter(902); auto fd = d.push(td, 4);
    d.write(td, fd, 0, 78);
    auto held = d.suspend(td);
    std::atomic<int> migrated{0};
    std::thread worker([&] {
        auto new_turn = d.resume(held);
        assert(d.read(new_turn, fd, 0) == 78);
        d.pop(new_turn, fd);
        d.leave(new_turn);
        migrated.store(1);
    });
    worker.join();
    assert(migrated.load() == 1 && d.depth() == 0);

    // Non-LIFO pop and overflow poison only the offending frame arena.
    ActorFrameArena e(9, 1, 3, 12);
    auto te = e.enter(123);
    auto fe1 = e.push(te, 2), fe2 = e.push(te, 2);
    denies([&] { e.pop(te, fe1); });
    assert(e.poisoned());
    (void)fe2;
    ActorFrameArena g(9, 2, 3, 12);
    auto tg = g.enter(123); auto fg = g.push(tg, 12); (void)fg;
    denies([&] { (void)g.push(tg, 1); });
    assert(g.poisoned());

    // Normal cancel zeroizes and invalidates old continuation references.
    ActorFrameArena h(10, 1, 3, 12);
    auto th = h.enter(77); auto fh = h.push(th, 3); auto sh = h.suspend(th);
    assert(h.cancel_quiesced());
    assert(h.depth() == 0);
    auto th2 = h.enter(78);
    denies([&] { (void)h.read(th2, fh, 0); });
    assert(h.poisoned());
    ActorFrameArena i(10, 2, 3, 12);
    denies([&] { (void)i.resume(sh); });
    assert(i.poisoned());

    // Scoped current dispatch never permits nested actor impersonation.
    ActorFrameArena j(11, 1);
    auto tj = j.enter(9);
    {
        ActorFrameArena::ScopedCurrent scope(j, tj);
        auto active = ActorFrameArena::current();
        assert(active && active->arena == &j);
        ActorFrameArena k(12, 1); auto tk = k.enter(10);
        denies([&] { ActorFrameArena::ScopedCurrent nested(k, tk); });
        assert(ActorFrameArena::current()->arena == &j);
        k.leave(tk);
    }
    assert(!ActorFrameArena::current());
    j.leave(tj);
    assert(j.retire_quiesced());
    denies([&] { (void)j.enter(10); });

    // Real ScalarActor callback bridge: sibling survives swallowed overflow.
    ScalarActor live(77, 2, 4, init, handle);
    auto live_ref = live.ref();
    assert(live.run_one().status == ScalarActor::Turn::Initialized);
    assert(!ActorFrameArena::current());
    assert(live.try_send(live_ref, 41) == ScalarActor::Send::Accepted);
    assert(live.run_one().value == 42);
    assert(!ActorFrameArena::current());
    ScalarActor bad(78, 2, 4, init, swallow_overflow);
    auto bad_ref = bad.ref();
    assert(bad.run_one().status == ScalarActor::Turn::Initialized);
    assert(bad.try_send(bad_ref, 7) == ScalarActor::Send::Accepted);
    denies([&] { (void)bad.run_one(); });
    assert(bad.try_send(bad_ref, 8) == ScalarActor::Send::Closed);
    assert(live.try_send(live_ref, 22) == ScalarActor::Send::Accepted);
    assert(live.run_one().value == 23);
    live.stop(); bad.stop();

    std::cout << "ATTO_FORGE_ACTOR_FRAME_HOST_EXECUTED|cases=20|guest=NOT_EXECUTED\n";
}
