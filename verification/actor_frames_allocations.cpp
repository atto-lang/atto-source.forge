#include "runtime/actor_frames.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <new>

namespace {
std::atomic<std::size_t> allocations{0};
}

// Count ordinary C++ allocation entrypoints, not malloc or OS page mapping.
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    using oresforge::runtime::ActorFrameStack;
    // Check that construction really traverses the instrumented allocator.
    const auto before_construction = allocations.load();
    ActorFrameStack stack(1, 1);
    if (allocations.load() <= before_construction) return 1;
    constexpr std::size_t cycles = 16384;
    std::array<std::uint8_t, 8> payload{42, 1, 2, 3, 4, 5, 6, 7}, read{};
    const auto before = allocations.load();
    for (std::size_t i = 0; i < cycles; ++i) {
        ActorFrameStack::Frame frame;
        {
            ActorFrameStack::Turn turn(stack);
            auto token = stack.current_turn();
            frame = stack.push(token, payload.size());
            stack.read(token, frame, 0, read);
            if (read != std::array<std::uint8_t, 8>{}) return 2;
            stack.write(token, frame, 0, payload);
        }
        {
            ActorFrameStack::Turn resumed(stack);
            auto token = stack.current_turn();
            stack.read(token, frame, 0, read);
            if (read != payload) return 3;
            stack.pop(token, frame);
            if (stack.depth(token) != 0) return 4;
        }
    }
    const auto delta = allocations.load() - before;
    if (delta != 0) {
        std::fprintf(stderr, "unexpected ordinary C++ allocations: %zu\n", delta);
        return 5;
    }
    std::printf("FORGE_HOST_FRAME_ORDINARY_NEW_CALLS=0 cycles=%zu physical_turns=%zu\n",
                cycles, cycles * 2);
    return 0;
}
