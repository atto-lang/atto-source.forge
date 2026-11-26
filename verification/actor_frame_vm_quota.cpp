#include "runtime/actor_frame_vm.hpp"
#include <cstdio>
#include <stdexcept>
#include <vector>

using VM = oresforge::runtime::ActorScalarFrameVM;
using Frames = oresforge::runtime::ActorFrameStack;
using Op = VM::Op;
using I = VM::Instruction;
using Fn = VM::Function;

static VM exact_budget(bool one_over) {
    // 127 calls, each executing 128 instructions, plus the root's 128
    // instructions = 16384. A single additional nested return costs one.
    Fn root, leaf, longer;
    root.instructions.assign(127, I{Op::Call, 0, 1});
    root.instructions.push_back(I{Op::Return});
    leaf.instructions.assign(127, I{Op::LoadConstant, 42});
    leaf.instructions.push_back(I{Op::Return});
    longer = leaf;
    longer.instructions[0] = I{Op::Call, 0, 3};
    if (one_over) root.instructions[0].target = 2;
    return VM({root, leaf, longer, Fn{{I{Op::Return}}}});
}

int main() {
    auto exact = exact_budget(false);
    auto over = exact_budget(true);
    Frames healthy(1, 1), exhausted(2, 1);
    {
        Frames::Turn turn(healthy), failing_turn(exhausted);
        for (int n = 0; n < 4; ++n) {
            if (exact.execute(healthy, 0, n) != 42 ||
                healthy.depth(healthy.current_turn()) != 0) return 1;
        }
        bool denied = false;
        try { (void)over.execute(exhausted, 0, 0); }
        catch (const std::length_error&) { denied = true; }
        if (!denied || !exhausted.retired() || exhausted.poisoned()) return 2;
        if (exact.execute(healthy, 0, 0) != 42 ||
            healthy.depth(healthy.current_turn()) != 0) return 3;
    }
    healthy.retire();
    std::puts("FORGE_HOST_VM_QUOTA: 16384 admitted, 16385 denied, sibling survives");
}
