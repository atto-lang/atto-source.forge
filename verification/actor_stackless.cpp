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
// A native callback failure must not strand a thread-local actor pointer.
struct ActiveActorScope final {
    Actor* previous;
    explicit ActiveActorScope(Actor& actor) : previous(active_actor) {
        CHECK(previous == nullptr);
        active_actor = &actor;
    }
    ~ActiveActorScope() { active_actor = previous; }
};
std::int64_t invoke(Actor& actor, std::int64_t input) {
    ActiveActorScope scope(actor);
    CHECK(actor.try_send(actor.ref(), input) == Actor::Send::Accepted);
    const auto result = actor.run_one();
    CHECK(result.status == Actor::Turn::Executed);
    return result.value;
}
std::int64_t swallow_quota(OresActorI64Heap*, std::uint64_t,
                           std::uint64_t, std::int64_t) {
    CHECK(active_actor != nullptr);
    try {
        (void)Machine::run(active_actor->frame_stack(), countdown, 64);
    } catch (const std::length_error&) {}
    return 999; // the bridge must still notice poisoned actor frames
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
        // Even unreachable bytecode is completely validated before execution.
        const std::array<Machine::Instruction, 2> unreachable_bad_target{{
            {Machine::Op::ReturnLiteral, 0, 42},
            {Machine::Op::Jump, 200},
        }};
        rejects<std::invalid_argument>([&] {
            (void)Machine::run(arena, unreachable_bad_target, 0);
        });
        const std::array<Machine::Instruction, 1> smuggled_literal{{
            {Machine::Op::Jump, 0, 99}
        }};
        rejects<std::invalid_argument>([&] {
            (void)Machine::run(arena, smuggled_literal, 0);
        });
        const std::array<Machine::Instruction, 1> smuggled_target{{
            {Machine::Op::ReturnLiteral, 1, 42}
        }};
        rejects<std::invalid_argument>([&] {
            (void)Machine::run(arena, smuggled_target, 0);
        });
        const std::array<Machine::Instruction, 1> smuggled_unused_operand{{
            {Machine::Op::ReturnArg, 1, 0}
        }};
        rejects<std::invalid_argument>([&] {
            (void)Machine::run(arena, smuggled_unused_operand, 0);
        });
        const std::array<Machine::Instruction, 0> empty_code{};
        rejects<std::invalid_argument>([&] {
            (void)Machine::run(arena, empty_code, 0);
        });
        rejects<std::invalid_argument>([&] {
            (void)Machine::run(arena, countdown, 0, 0);
        });
        CHECK(arena.depth(arena.current_turn()) == 0);
        CHECK(!arena.poisoned()); // no actor state changed on static rejection
        // Table-style stress: every valid depth, repeated without leaked frames.
        for (int repetition = 0; repetition < 16; ++repetition)
            for (std::int64_t n = 0; n <= 63; ++n) {
                CHECK(Machine::run(arena, countdown, n) == n);
                CHECK(arena.depth(arena.current_turn()) == 0);
            }
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
    CHECK(active_actor == nullptr); // exception restores carrier-local context
    CHECK(bad.run_one().status == Actor::Turn::Stopped);
    // A malicious callback catching its own limit cannot commit the turn.
    Actor swallowed(107, 1, 8, initialize, swallow_quota);
    rejects<std::logic_error>([&] { (void)invoke(swallowed, 1); });
    CHECK(active_actor == nullptr);
    CHECK(swallowed.done() && swallowed.frame_stack().retired());
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

    // Malformed reachable control flow: no child result may be forged.
    const std::array<Machine::Instruction, 1> premature_return{{
        {Machine::Op::ReturnChild}
    }};
    const std::array<Machine::Instruction, 2> premature_increment{{
        {Machine::Op::IncrementChild},
        {Machine::Op::ReturnChild}
    }};
    Frames no_child(108, 1);
    {
        Frames::Turn turn(no_child);
        rejects<std::length_error>([&] {
            (void)Machine::run(no_child, premature_return, 0);
        });
    }
    CHECK(no_child.poisoned());
    Frames no_child_math(109, 1);
    {
        Frames::Turn turn(no_child_math);
        rejects<std::length_error>([&] {
            (void)Machine::run(no_child_math, premature_increment, 0);
        });
    }
    CHECK(no_child_math.poisoned());

    // Independent byte quota: the actor can have spare frame *slots*
    // while the next 32-byte call cannot fit in its 48-byte byte budget.
    Frames bytes(110, 1, 64, 48);
    {
        Frames::Turn turn(bytes);
        rejects<std::length_error>([&] {
            (void)Machine::run(bytes, countdown, 1);
        });
    }
    CHECK(bytes.poisoned());
    Frames still_alive(111, 1);
    {
        Frames::Turn turn(still_alive);
        const std::array<Machine::Instruction, 1> immediate{{
            {Machine::Op::ReturnLiteral, 0, 314}
        }};
        CHECK(Machine::run(still_alive, immediate, 0, 1) == 314);
        CHECK(still_alive.depth(still_alive.current_turn()) == 0);
    }

    std::puts("ATTO_FORGE_STACKLESS_HOST_EXECUTED|iterative=1|sibling_survival=1|migration=1|quota=1|budget=1|guest=NOT_EXECUTED");
    std::puts("ATTO_FORGE_STACKLESS_NEGATIVE_CONTROLS|bytecode=7|uninitialized_child=2|budget=2|byte_quota=1|recoverable_sibling=1|guest=NOT_EXECUTED");
}
