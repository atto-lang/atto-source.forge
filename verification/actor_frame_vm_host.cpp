#include "runtime/actor_frame_vm.hpp"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>

using VM = oresforge::runtime::ActorScalarFrameVM;
using Frames = oresforge::runtime::ActorFrameStack;
using Op = VM::Op;
using I = VM::Instruction;
using Fn = VM::Function;

static void require(bool condition) {
    if (!condition) throw std::runtime_error("host frame VM assertion failed");
}
template<class E, class F> static void rejects(F action) {
    bool denied = false;
    try { action(); } catch (const E&) { denied = true; }
    require(denied);
}

int main() {
    VM fact({Fn{{I{Op::BranchArgumentLe, 1, 4}, I{Op::Call, -1, 0},
                 I{Op::MultiplyArgument}, I{Op::Return},
                 I{Op::LoadConstant, 1}, I{Op::Return}}}});
    VM fib({Fn{{I{Op::BranchArgumentLe, 1, 6}, I{Op::Call, -1, 0},
                I{Op::SaveTemporary}, I{Op::Call, -2, 0}, I{Op::AddTemporary},
                I{Op::Return}, I{Op::LoadArgument}, I{Op::Return}}}});
    VM descend({Fn{{I{Op::BranchArgumentLe, 0, 3}, I{Op::Call, -1, 0},
                    I{Op::Return}, I{Op::LoadConstant, 42}, I{Op::Return}}}});
    VM add({Fn{{I{Op::LoadConstant, 17}, I{Op::AddArgument}, I{Op::Return}}}});
    VM mul({Fn{{I{Op::LoadConstant, -7}, I{Op::MultiplyArgument}, I{Op::Return}}}});
    Frames healthy(1, 1);
    Frames::Token stale;
    {
        Frames::Turn turn(healthy);
        stale = healthy.current_turn();
        std::int64_t expected = 1;
        for (std::int64_t n = 0; n <= 20; ++n) {
            if (n > 1) expected *= n;
            require(fact.execute(healthy, 0, n) == expected);
            require(healthy.depth(stale) == 0);
        }
        std::int64_t a = 0, b = 1;
        for (std::int64_t n = 0; n <= 16; ++n) {
            require(fib.execute(healthy, 0, n) == a);
            const auto next = a + b; a = b; b = next;
        }
        for (std::int64_t n = -512; n <= 512; ++n) {
            require(add.execute(healthy, 0, n) == n + 17);
            require(mul.execute(healthy, 0, n) == n * -7);
        }
        require(descend.execute(healthy, 0, 63) == 42);
        require(healthy.depth(stale) == 0);
    }
    std::thread migration([&] {
        Frames::Turn turn(healthy);
        rejects<std::logic_error>([&] { (void)healthy.depth(stale); });
        require(descend.execute(healthy, 0, 63) == 42);
    });
    migration.join();
    {
        Frames bad(2, 1); Frames::Turn turn(bad);
        rejects<std::length_error>([&] { (void)descend.execute(bad, 0, 64); });
        require(bad.poisoned());
    }
    {
        Frames bad(3, 1); Frames::Turn turn(bad);
        rejects<std::overflow_error>([&] { (void)fact.execute(bad, 0, 21); });
        require(bad.retired());
    }
    {
        Frames bad(4, 1); Frames::Turn turn(bad);
        rejects<std::overflow_error>([&] {
            (void)add.execute(bad, 0, std::numeric_limits<std::int64_t>::max());
        });
        require(bad.retired());
    }
    rejects<std::invalid_argument>([] { (void)VM({}); });
    rejects<std::invalid_argument>([] { (void)VM({Fn{{I{Op::Call, 0, 1}, I{Op::Return}}}}); });
    rejects<std::invalid_argument>([] { (void)VM({Fn{{I{Op::BranchArgumentLe, 0, 0}, I{Op::Return}}}}); });
    rejects<std::invalid_argument>([] { (void)VM({Fn{{I{Op::Return}, I{static_cast<Op>(255)}, I{Op::Return}}}}); });
    {
        Frames::Turn turn(healthy);
        require(fact.execute(healthy, 0, 5) == 120);
        require(healthy.depth(healthy.current_turn()) == 0);
    }
    healthy.retire();
    std::puts("FORGE_HOST_VM: 2050 arithmetic cases, factorial, Fibonacci, depth, migration and denial PASS");
}
