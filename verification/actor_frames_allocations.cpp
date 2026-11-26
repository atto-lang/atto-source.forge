#include "runtime/actor_frames.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace {
std::atomic<std::size_t> allocations{0};
std::atomic<std::size_t> aligned_allocations{0};
}

// Count ordinary and over-aligned C++ allocation entrypoints, not direct malloc or OS page mapping.
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

void* operator new(std::size_t size, std::align_val_t alignment) {
    aligned_allocations.fetch_add(1, std::memory_order_relaxed);
    const auto a = static_cast<std::size_t>(alignment);
    size = size ? size : 1;
#if defined(_WIN32)
    if (auto* p = _aligned_malloc(size, a)) return p;
#else
    if (size <= std::numeric_limits<std::size_t>::max() - (a - 1)) {
        if (auto* p = std::aligned_alloc(a, (size + a - 1) / a * a)) return p;
    }
#endif
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void* p, std::align_val_t) noexcept {
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }

int main() {
    using oresforge::runtime::ActorFrameStack;
    // Check that construction really traverses the instrumented allocator.
    const auto before_construction = allocations.load();
    ActorFrameStack stack(1, 1);
    if (allocations.load() <= before_construction) return 1;
    const auto aligned_control = aligned_allocations.load();
    using AlignedAlloc = void* (*)(std::size_t, std::align_val_t);
    using AlignedFree = void (*)(void*, std::align_val_t) noexcept;
    AlignedAlloc volatile allocate_control = &::operator new;
    AlignedFree volatile free_control = &::operator delete;
    auto* control = allocate_control(65, std::align_val_t{64});
    free_control(control, std::align_val_t{64});
    if (aligned_allocations.load() != aligned_control + 1) return 6;
    constexpr std::size_t cycles = 16384;
    std::array<std::uint8_t, 8> payload{42, 1, 2, 3, 4, 5, 6, 7}, read{};
    const auto before = allocations.load();
    const auto aligned_before = aligned_allocations.load();
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
    // Exercise both inclusive capacity limits in a suspended nested chain.
    constexpr std::size_t nested_cycles = 256;
    constexpr auto frame_bytes = ActorFrameStack::max_bytes / ActorFrameStack::max_frames;
    std::array<ActorFrameStack::Frame, ActorFrameStack::max_frames> frames{};
    std::array<std::uint8_t, frame_bytes> full{}, actual{};
    for (std::size_t cycle = 0; cycle < nested_cycles; ++cycle) {
        {
            ActorFrameStack::Turn turn(stack);
            auto token = stack.current_turn();
            for (std::size_t i = 0; i < frames.size(); ++i) {
                frames[i] = stack.push(token, frame_bytes);
                stack.read(token, frames[i], 0, actual);
                if (actual != std::array<std::uint8_t, frame_bytes>{}) return 7;
                full.fill(static_cast<std::uint8_t>(i + 1));
                stack.write(token, frames[i], 0, full);
            }
            if (stack.depth(token) != frames.size()) return 8;
        }
        {
            ActorFrameStack::Turn resumed(stack);
            auto token = stack.current_turn();
            for (std::size_t i = frames.size(); i > 0; --i) {
                full.fill(static_cast<std::uint8_t>(i));
                stack.read(token, frames[i - 1], 0, actual);
                if (actual != full) return 9;
                stack.pop(token, frames[i - 1]);
            }
            if (stack.depth(token) != 0) return 10;
        }
    }
    const auto aligned_delta = aligned_allocations.load() - aligned_before;
    if (aligned_delta != 0) {
        std::fprintf(stderr, "unexpected aligned C++ allocations: %zu\n", aligned_delta);
        return 11;
    }
    const auto delta = allocations.load() - before;
    if (delta != 0) {
        std::fprintf(stderr, "unexpected ordinary C++ allocations: %zu\n", delta);
        return 5;
    }
    std::printf("FORGE_HOST_FRAME_ORDINARY_NEW_CALLS=0 cycles=%zu physical_turns=%zu\n",
                cycles, cycles * 2);
    std::printf("FORGE_HOST_FRAME_ALIGNED_NEW_CALLS=0 nested_cycles=%zu frames=%zu bytes=%zu\n",
                nested_cycles, ActorFrameStack::max_frames, ActorFrameStack::max_bytes);
    return 0;
}
