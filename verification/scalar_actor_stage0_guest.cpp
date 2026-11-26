#include "runtime/scalar_actor.hpp"
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>

using Frames = oresforge::runtime::ActorFrameStack;
using Actor = oresforge::runtime::ScalarActor;
extern "C" void oresforge_actor_init(OresActorI64Heap*, std::uint64_t, std::uint64_t, Frames&);
extern "C" std::int64_t oresforge_actor_handle(OresActorI64Heap*, std::uint64_t, std::uint64_t, std::int64_t, Frames&);
static void require(bool ok) { if (!ok) throw std::runtime_error("stage0 guest assertion"); }
int main() {
    Actor a(1, 1, 2, oresforge_actor_init, oresforge_actor_handle);
    Actor b(1, 1, 2, oresforge_actor_init, oresforge_actor_handle);
    const auto ar = a.ref(), br = b.ref();
    require(a.try_send(br, 2) == Actor::Send::Foreign);
    require(a.try_send(ar, 2) == Actor::Send::Accepted);
    require(a.try_send(ar, 3) == Actor::Send::Accepted);
    require(a.try_send(ar, 4) == Actor::Send::Full);
    require(a.run_one().value == FIRST);
    require(b.try_send(br, 1) == Actor::Send::Accepted);
    require(b.run_one().value == PEER_FIRST);
    Actor::Result result{};
    std::thread carrier([&] { result = a.run_one(); });
    carrier.join();
    require(result.status == Actor::Turn::Executed && result.value == SECOND);
    require(a.try_send(ar, OVERFLOW_INPUT) == Actor::Send::Accepted);
    require(a.try_send(ar, 1) == Actor::Send::Accepted);
    bool overflow = false;
    try { (void)a.run_one(); } catch (const std::overflow_error&) { overflow = true; }
    require(overflow && a.done() && a.queued() == 0 && a.frame_stack().retired());
    require(a.try_send(ar, 1) == Actor::Send::Closed);
    require(b.try_send(br, 2) == Actor::Send::Accepted);
    b.stop();
    require(b.try_send(br, 1) == Actor::Send::Closed);
    require(b.run_one().value == PEER_SECOND);
    require(b.done() && b.frame_stack().retired());
    std::puts("FORGE_STAGE0_NATIVE_GUEST=PASS");
}
