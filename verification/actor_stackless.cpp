#include "runtime/actor_stackless.hpp"
#include "runtime/scalar_actor.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
using Machine = oresforge::runtime::ActorStacklessI64;
using Actor = oresforge::runtime::ScalarActor;
using Frames = oresforge::runtime::ActorFrameStack;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("actor stackless test assertion line " + std::to_string(__LINE__)); } while (0)

namespace {
const std::array<Machine::Instruction, 6> countdown{{
    {Machine::Op::BranchIfZero, 5},
    {Machine::Op::DecrementArg},
    {Machine::Op::Call, 0},
    {Machine::Op::IncrementChild},
    {Machine::Op::ReturnChild},
    {Machine::Op::ReturnLiteral, 0, 0},
}};
thread_local Actor* active_actor = nullptr;
void initialize(OresActorI64Heap* heap, std::uint64_t domain, std::uint64_t epoch) {
    ores_actor_i64_heap_field_init_mutable(heap, domain, epoch, 0, 0);
}
std::int64_t handle(OresActorI64Heap* heap, std::uint64_t domain,
                    std::uint64_t epoch, std::int64_t input) {
    CHECK(active_actor != nullptr);
    auto value = Machine::run(active_actor->frame_stack(), countdown, input);
    ores_actor_i64_heap_field_write(heap, domain, epoch, 0, value);
    return ores_actor_i64_heap_field_read(heap, domain, epoch, 0);
}
std::int64_t invoke(Actor& actor, std::int64_t input) {
    active_actor = &actor;
    CHECK(actor.try_send(actor.ref(), input) == Actor::Send::Accepted);
    const auto result = actor.run_one();
    active_actor = nullptr;
    CHECK(result.status == Actor::Turn::Executed);
    return result.value;
}
template<class E, class F> void rejects(F&& f) {
    bool yes = false;
    try { f(); } catch (const E&) { yes = true; }
    CHECK(yes);
}
}
int main() {
    // Actual bounded frame storage, iterative call lowering and checked unwind.
    Frames arena(101, 1);
    {
        Frames::Turn turn(arena);
        CHECK(Machine::run(arena, countdown, 0) == 0);
        CHECK(Machine::run(arena, countdown, 1) == 1);
        CHECK(Machine::run(arena, countdown, 32) == 32);
        CHECK(Machine::run(arena, countdown, 63) == 63);
        CHECK(arena.depth(arena.current_turn()) == 0);
        const std::array<Machine::Instruction, 1> invalid_target{{
            {Machine::Op::Jump, 9}
        }};
        rejects<std::invalid_argument>([&] {
            (void)Machine::run(arena, invalid_target, 0);
        });
        CHECK(arena.depth(arena.current_turn()) == 0);
        // Host callers cannot disable the instruction ceiling with a huge budget.
        rejects<std::invalid_argument>([&] {
            (void)Machine::run(arena, countdown, 0, 65537);
        });
        CHECK(arena.depth(arena.current_turn()) == 0);
        const std::array<Machine::Instruction, 1> bad_opcode{{
            {static_cast<Machine::Op>(255)}
        }};
        rejects<std::invalid_argument>([&] {
            (void)Machine::run(arena, bad_opcode, 0);
        });
        CHECK(arena.depth(arena.current_turn()) == 0);
    }

    Actor good(102, 1, 8, initialize, handle);
    CHECK(invoke(good, 40) == 40);
    CHECK(good.frame_stack().retired() == false);
    // Carrier migration does not change the frame ownership domain.
    std::atomic<bool> migrated = false;
    std::thread carrier([&] {
        migrated = invoke(good, 20) == 20;
    });
    carrier.join();
    CHECK(migrated && good.queued() == 0);
    good.stop();
    CHECK(good.done() && good.frame_stack().retired());

    Actor bad(103, 1, 8, initialize, handle);
    // 64 calls require 65 live frames. Denial poisons only this actor.
    rejects<std::length_error>([&] { (void)invoke(bad, 64); });
    CHECK(bad.done() && bad.frame_stack().retired());
    CHECK(bad.run_one().status == Actor::Turn::Stopped);
    Actor sibling(104, 1, 8, initialize, handle);
    CHECK(invoke(sibling, 7) == 7);
    CHECK(invoke(sibling, 5) == 5);
    sibling.stop();
    CHECK(sibling.done());

    Frames loop(105, 1);
    const std::array<Machine::Instruction, 1> infinite{{
        {Machine::Op::Jump, 0}
    }};
    {
        Frames::Turn turn(loop);
        rejects<std::length_error>([&] {
            (void)Machine::run(loop, infinite, 1, 16);
        });
    }
    CHECK(loop.poisoned());
    rejects<std::logic_error>([&] { Frames::Turn turn(loop); });

    Frames underflow(106, 1);
    const std::array<Machine::Instruction, 2> decrement{{
        {Machine::Op::DecrementArg},
        {Machine::Op::ReturnArg}
    }};
    {
        Frames::Turn turn(underflow);
        rejects<std::length_error>([&] {
            (void)Machine::run(underflow, decrement,
                                std::numeric_limits<std::int64_t>::min());
        });
    }
    CHECK(underflow.poisoned());
    std::puts("ATTO_FORGE_STACKLESS_HOST_EXECUTED|iterative=1|sibling_survival=1|migration=1|quota=1|budget=1|guest=NOT_EXECUTED");
}
