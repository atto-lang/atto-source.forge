#include "runtime/scalar_actor.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
using Actor = oresforge::runtime::ScalarActor;
using Frames = oresforge::runtime::ActorFrameStack;
#define REQUIRE(x) do { if (!(x)) throw std::runtime_error("scalar actor frame test failure line " + std::to_string(__LINE__)); } while (0)

namespace {
Actor* current_actor = nullptr;
Frames::Frame suspended;
void initialize(OresActorI64Heap* heap, std::uint64_t domain, std::uint64_t epoch) {
    auto& frames = current_actor->frame_stack();
    const auto token = frames.current_turn();
    suspended = frames.push(token, 8);
    const std::array<std::uint8_t, 1> initial{3};
    frames.write(token, suspended, 0, initial);
    ores_actor_i64_heap_field_init_mutable(heap, domain, epoch, 0, 0);
}
std::int64_t handle(OresActorI64Heap* heap, std::uint64_t domain,
                    std::uint64_t epoch, std::int64_t message) {
    auto& frames = current_actor->frame_stack();
    const auto token = frames.current_turn();
    std::array<std::uint8_t, 1> previous{};
    frames.read(token, suspended, 0, previous);
    const auto next = static_cast<std::int64_t>(previous[0]) + message;
    if (next > 255) throw std::overflow_error("frame scalar overflow");
    const std::array<std::uint8_t, 1> written{static_cast<std::uint8_t>(next)};
    frames.write(token, suspended, 0, written);
    ores_actor_i64_heap_field_write(heap, domain, epoch, 0, next);
    return ores_actor_i64_heap_field_read(heap, domain, epoch, 0);
}
std::int64_t overflow(OresActorI64Heap*, std::uint64_t, std::uint64_t,
                      std::int64_t) {
    auto& frames = current_actor->frame_stack();
    // A handler may catch its own limit failure; ScalarActor must still
    // observe poison and terminate the actor at the turn boundary.
    try {
        (void)frames.push(frames.current_turn(), Frames::max_bytes + 1);
    } catch (const std::length_error&) {}
    return 999;
}
} // namespace
int main() {
    Actor actor(42, 1, 4, initialize, handle);
    current_actor = &actor;
    auto ref = actor.ref();
    REQUIRE(actor.try_send(ref, 7) == Actor::Send::Accepted);
    REQUIRE(actor.run_one().value == 10);
    bool carrier_deny = false;
    try { (void)actor.frame_stack().current_turn(); }
    catch (const std::logic_error&) { carrier_deny = true; }
    REQUIRE(carrier_deny);
    REQUIRE(actor.try_send(ref, 9) == Actor::Send::Accepted);
    std::atomic<std::int64_t> value = 0;
    std::thread migrated([&] { value = actor.run_one().value; });
    migrated.join();
    REQUIRE(value == 19 && !actor.frame_stack().retired());
    actor.stop();
    REQUIRE(actor.done() && actor.frame_stack().retired());
    REQUIRE(actor.try_send(ref, 1) == Actor::Send::Closed);

    Actor bad(43, 1, 4, initialize, overflow);
    current_actor = &bad;
    REQUIRE(bad.try_send(bad.ref(), 1) == Actor::Send::Accepted);
    bool stopped = false;
    try { (void)bad.run_one(); } catch (const std::logic_error&) { stopped = true; }
    REQUIRE(stopped && bad.done() && bad.frame_stack().retired());
    REQUIRE(bad.run_one().status == Actor::Turn::Stopped);
    // A poisoned actor cannot compromise a sibling's separate heap/stack.
    Actor sibling(44, 1, 4, initialize, handle);
    current_actor = &sibling;
    REQUIRE(sibling.try_send(sibling.ref(), 5) == Actor::Send::Accepted);
    REQUIRE(sibling.run_one().value == 8);
    sibling.stop();
    REQUIRE(sibling.done());
    std::puts("Forge ScalarActor private heap + frame storage: live callback, migration, poison, sibling survival PASS");
}
