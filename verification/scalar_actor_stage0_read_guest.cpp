#include "runtime/scalar_actor.hpp"
#include <cstdlib>
#include <cstdint>
using Actor = oresforge::runtime::ScalarActor;
using Frames = oresforge::runtime::ActorFrameStack;
extern "C" void oresforge_actor_init(OresActorI64Heap*, std::uint64_t, std::uint64_t, Frames&);
extern "C" std::int64_t oresforge_actor_handle(OresActorI64Heap*, std::uint64_t, std::uint64_t, std::int64_t, Frames&);
static void check(bool value) { if (!value) std::abort(); }
int main() {
    Actor a(1, 1, 2, oresforge_actor_init, oresforge_actor_handle);
    Actor b(1, 1, 2, oresforge_actor_init, oresforge_actor_handle);
    for (auto* actor : {&a, &b}) {
        auto ref = actor->ref();
        check(actor->try_send(ref, 100) == Actor::Send::Accepted);
        check(actor->try_send(ref, -100) == Actor::Send::Accepted);
        check(actor->run_one().value == 42);
        actor->stop();
        check(actor->run_one().value == 42);
        check(actor->done());
    }
}
