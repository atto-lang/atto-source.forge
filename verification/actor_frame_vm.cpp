#include "runtime/actor_frame_vm.hpp"
#include "runtime/scalar_actor.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using VM = oresforge::runtime::ActorScalarFrameVM;
using Frames = oresforge::runtime::ActorFrameStack;
using Actor = oresforge::runtime::ScalarActor;
using Op = VM::Op;
using I = VM::Instruction;
using Fn = VM::Function;
#define REQUIRE(x) do { if (!(x)) throw std::runtime_error( \
    "actor frame VM test failure line " + std::to_string(__LINE__)); } while (0)

static VM factorial() {
    return VM({Fn{{I{Op::BranchArgumentLe, 1, 4}, I{Op::Call, -1, 0},
                   I{Op::MultiplyArgument}, I{Op::Return},
                   I{Op::LoadConstant, 1}, I{Op::Return}}}});
}
static VM mutual() {
    return VM({
        Fn{{I{Op::BranchArgumentLe, 0, 3}, I{Op::Call, -1, 1},
            I{Op::Return}, I{Op::LoadConstant, 1}, I{Op::Return}}},
        Fn{{I{Op::BranchArgumentLe, 0, 3}, I{Op::Call, -1, 0},
            I{Op::Return}, I{Op::LoadConstant, 0}, I{Op::Return}}}
    });
}
static VM fibonacci() {
    return VM({Fn{{I{Op::BranchArgumentLe, 1, 6},
                   I{Op::Call, -1, 0}, I{Op::SaveTemporary},
                   I{Op::Call, -2, 0}, I{Op::AddTemporary},
                   I{Op::Return}, I{Op::LoadArgument}, I{Op::Return}}}});
}
static VM runaway() {
    return VM({Fn{{I{Op::Call, 0, 0}, I{Op::Return}}}});
}
template<class F> static void expect_invalid(F&& action) {
    bool rejected = false;
    try { action(); }
    catch (const std::invalid_argument&) { rejected = true; }
    REQUIRE(rejected);
}
template<class E, class F> static void expect_fault(F&& action) {
    bool rejected = false;
    try { action(); }
    catch (const E&) { rejected = true; }
    REQUIRE(rejected);
}

static const VM* active_vm = nullptr; // immutable, shared host test program
static void initialize(OresActorI64Heap* heap, std::uint64_t d, std::uint64_t e,
                       Frames&) {
    ores_actor_i64_heap_field_init_mutable(heap, d, e, 0, 0);
}
static std::int64_t execute(OresActorI64Heap* heap, std::uint64_t d,
                            std::uint64_t e, std::int64_t msg, Frames& frames) {
    const auto result = active_vm->execute(frames, 0, msg);
    const auto previous = ores_actor_i64_heap_field_read(heap, d, e, 0);
    if (result > 0 && previous > std::numeric_limits<std::int64_t>::max() - result)
        throw std::overflow_error("actor field accumulation overflow");
    const auto next = static_cast<std::int64_t>(previous + result);
    ores_actor_i64_heap_field_write(heap, d, e, 0, next);
    return next;
}

static std::int64_t catch_quota(OresActorI64Heap*, std::uint64_t,
                                std::uint64_t, std::int64_t, Frames& frames) {
    // A user-level handler cannot make a terminal VM quota fault succeed by
    // swallowing the exception: the outer ScalarActor turn checks retirement.
    try { (void)active_vm->execute(frames, 0, 22); }
    catch (const std::length_error&) {}
    return 123;
}
int main() {
    std::size_t checks = 0;
    auto fact = factorial();
    auto even = mutual();
    auto fib = fibonacci();
    const std::array<std::int64_t, 21> results{
        1, 1, 2, 6, 24, 120, 720, 5040, 40320, 362880, 3628800,
        39916800, 479001600, 6227020800LL, 87178291200LL,
        1307674368000LL, 20922789888000LL, 355687428096000LL,
        6402373705728000LL, 121645100408832000LL, 2432902008176640000LL
    };
    // Every invocation uses actual actor-private frame reads/writes. Reusing
    // the same arena requires complete LIFO pop and zeroization after return.
    Frames frames(99, 1);
    {
        Frames::Turn turn(frames);
        const auto token = frames.current_turn();
        for (std::size_t n = 0; n < results.size(); ++n) {
            REQUIRE(fact.execute(frames, 0, static_cast<std::int64_t>(n)) == results[n]);
            REQUIRE(frames.depth(token) == 0);
            ++checks;
        }
        for (std::int64_t n = 0; n < 60; ++n) {
            REQUIRE(even.execute(frames, 0, n) == (n % 2 == 0 ? 1 : 0));
            REQUIRE(even.execute(frames, 1, n) == (n % 2 == 0 ? 0 : 1));
            REQUIRE(frames.depth(token) == 0);
            checks += 2;
        }
        const std::array<std::int64_t, 17> fibonacci_values{
            0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987
        };
        for (std::size_t n = 0; n < fibonacci_values.size(); ++n) {
            REQUIRE(fib.execute(frames, 0, static_cast<std::int64_t>(n)) == fibonacci_values[n]);
            REQUIRE(frames.depth(token) == 0);
            ++checks;
        }
        expect_fault<std::overflow_error>([&] { (void)fact.execute(frames, 0, 21); });
        REQUIRE(frames.retired()); // terminal VM fault revokes the entire actor arena
        ++checks;
    }
    frames.retire();
    REQUIRE(frames.retired());
    expect_fault<std::logic_error>([&] { (void)frames.current_turn(); });

    // Self-recursion cannot consume the physical C++ stack indefinitely: at
    // exactly the per-actor frame budget ActorFrameStack poisons the actor.
    Frames runaway_frames(100, 1);
    {
        Frames::Turn turn(runaway_frames);
        auto program = runaway();
        expect_fault<std::length_error>([&] { (void)program.execute(runaway_frames, 0, 1); });
        REQUIRE(runaway_frames.poisoned());
        ++checks;
    }
    runaway_frames.retire();

    // Large branching call trees reach the amortized execution budget while
    // staying below depth=64; quota exhaustion is not a shared-carrier hang.
    Frames quota_frames(101, 1);
    {
        Frames::Turn turn(quota_frames);
        expect_fault<std::length_error>([&] { (void)fib.execute(quota_frames, 0, 22); });
        REQUIRE(!quota_frames.poisoned() && quota_frames.retired());
        ++checks;
    }
    quota_frames.retire();

    // Property-based scalar arithmetic sweep over 2,050 fully executed cases.
    // Each evaluation must leave the same arena at depth zero for reuse.
    Frames arithmetic_frames(104, 1);
    {
        Frames::Turn turn(arithmetic_frames);
        const auto token = arithmetic_frames.current_turn();
        VM add({Fn{{I{Op::LoadConstant, 17}, I{Op::AddArgument}, I{Op::Return}}}});
        VM multiply({Fn{{I{Op::LoadConstant, -7}, I{Op::MultiplyArgument}, I{Op::Return}}}});
        for (std::int64_t n = -512; n <= 512; ++n) {
            REQUIRE(add.execute(arithmetic_frames, 0, n) == n + 17);
            REQUIRE(multiply.execute(arithmetic_frames, 0, n) == n * -7);
            REQUIRE(arithmetic_frames.depth(token) == 0);
            checks += 2;
        }
    }
    arithmetic_frames.retire();

    // Bytecode admission is closed, including invalid unreachable code.
    expect_invalid([] { (void)VM({}); }); ++checks;
    expect_invalid([] { (void)VM({Fn{{}}}); }); ++checks;
    expect_invalid([] { (void)VM({Fn{{I{Op::LoadConstant, 1}}}}); }); ++checks;
    expect_invalid([] { (void)VM({Fn{{I{Op::Call, 0, 7}, I{Op::Return}}}}); }); ++checks;
    expect_invalid([] { (void)VM({Fn{{I{Op::LoadArgument, 3, 0}, I{Op::Return}}}}); }); ++checks;
    expect_invalid([] { (void)VM({Fn{{I{Op::LoadConstant, 3, 1}, I{Op::Return}}}}); }); ++checks;
    expect_invalid([] { (void)VM({Fn{{I{Op::Return, 1, 0}}}}); }); ++checks;
    expect_invalid([] { (void)VM({Fn{{I{Op::BranchArgumentLe, 0, 0}, I{Op::Return}}}}); }); ++checks;
    expect_invalid([] { (void)VM({Fn{{I{Op::BranchArgumentLe, 0, 9}, I{Op::Return}}}}); }); ++checks;
    expect_invalid([] { (void)VM({Fn{{I{Op::Return}, I{static_cast<Op>(255)}, I{Op::Return}}}}); }); ++checks;
    expect_invalid([] { (void)VM({Fn{{I{static_cast<Op>(99)}, I{Op::Return}}}}); }); ++checks;
    expect_invalid([] {
        Fn fn; fn.instructions.resize(VM::max_instructions_per_function+1, I{Op::LoadArgument});
        fn.instructions.back() = I{Op::Return};
        (void)VM({std::move(fn)});
    }); ++checks;
    expect_invalid([] {
        std::vector<Fn> funcs(VM::max_functions+1, Fn{{I{Op::Return}}});
        (void)VM(std::move(funcs));
    }); ++checks;
    // Runtime overflow and call-argument underflow: signed arithmetic never
    // reaches UB. Actor quiescence must be explicit after a thrown guest fault.
    {
        Frames bad(102, 1);
        Frames::Turn turn(bad);
        VM add({Fn{{I{Op::LoadConstant, 1}, I{Op::AddArgument}, I{Op::Return}}}});
        expect_fault<std::overflow_error>([&] {
            (void)add.execute(bad, 0, std::numeric_limits<std::int64_t>::max());
        });
        ++checks;
    }
    {
        Frames bad(103, 1);
        Frames::Turn turn(bad);
        VM underflow({Fn{{I{Op::Call, -1, 0}, I{Op::Return}}}});
        expect_fault<std::overflow_error>([&] {
            (void)underflow.execute(bad, 0, std::numeric_limits<std::int64_t>::min());
        });
        ++checks;
    }

    // Live ScalarActor integration: the same numeric actor id does not
    // authorize foreign ActorRefs. Overflow kills one actor, not its sibling.
    active_vm = &fact;
    Actor first(9001, 1, 8, initialize, execute);
    Actor sibling(9001, 1, 8, initialize, execute);
    const auto a = first.ref(), b = sibling.ref();
    REQUIRE(first.try_send(b, 2) == Actor::Send::Foreign);
    REQUIRE(sibling.try_send(a, 2) == Actor::Send::Foreign);
    REQUIRE(first.try_send(a, 5) == Actor::Send::Accepted);
    REQUIRE(first.run_one().value == 120);
    REQUIRE(first.try_send(a, 4) == Actor::Send::Accepted);
    std::int64_t migrated = 0;
    std::thread different_carrier([&] { migrated = first.run_one().value; });
    different_carrier.join();
    REQUIRE(migrated == 144);
    REQUIRE(sibling.try_send(b, 7) == Actor::Send::Accepted);
    REQUIRE(sibling.run_one().value == 5040);
    REQUIRE(first.try_send(a, 21) == Actor::Send::Accepted);
    REQUIRE(first.try_send(a, 2) == Actor::Send::Accepted);
    expect_fault<std::overflow_error>([&] { (void)first.run_one(); });
    REQUIRE(first.done() && first.frame_stack().retired() && first.queued() == 0);
    REQUIRE(first.try_send(a, 5) == Actor::Send::Closed);
    REQUIRE(first.run_one().status == Actor::Turn::Stopped);
    REQUIRE(sibling.try_send(b, 3) == Actor::Send::Accepted);
    REQUIRE(sibling.run_one().value == 5046);
    sibling.stop();
    REQUIRE(sibling.done() && sibling.frame_stack().retired());
    checks += 13;

    // A guest handler that deliberately catches its own VM quota exception
    // still cannot commit its turn: retired arena is an irrevocable fence.
    active_vm = &fib;
    Actor caught_quota(9002, 1, 4, initialize, catch_quota);
    REQUIRE(caught_quota.try_send(caught_quota.ref(), 99) == Actor::Send::Accepted);
    expect_fault<std::logic_error>([&] { (void)caught_quota.run_one(); });
    REQUIRE(caught_quota.done() && caught_quota.frame_stack().retired());
    REQUIRE(caught_quota.try_send(caught_quota.ref(), 1) == Actor::Send::Closed);
    Actor surviving(9002, 1, 4, initialize, execute);
    REQUIRE(surviving.try_send(surviving.ref(), 10) == Actor::Send::Accepted);
    REQUIRE(surviving.run_one().value == 55);
    surviving.stop();
    REQUIRE(surviving.done());
    checks += 6;

    // Arena instance is reentered on a new carrier, never with the old token.
    Frames migrant(200, 1);
    Frames::Token stale;
    {
        Frames::Turn turn(migrant);
        stale = migrant.current_turn();
        REQUIRE(fact.execute(migrant, 0, 8) == 40320);
    }
    std::thread transfer([&] {
        Frames::Turn turn(migrant);
        expect_fault<std::logic_error>([&] { (void)migrant.depth(stale); });
        REQUIRE(fact.execute(migrant, 0, 9) == 362880);
    });
    transfer.join();
    migrant.retire();
    checks += 2;

    std::printf("Forge iterative actor frames: %zu positive/negative checks, "
                "bounded recursion, mutation-sensitive arithmetic, "
                "cross-carrier and sibling survival PASS\n", checks);
}
