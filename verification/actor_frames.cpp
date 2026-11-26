#include "runtime/actor_frames.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using oresforge::runtime::ActorFrameStack;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("actor frame assertion line " + std::to_string(__LINE__)); } while (0)
template <class E, class F> void rejects(F&& operation) {
    bool failed = false;
    try { operation(); } catch (const E&) { failed = true; }
    CHECK(failed);
}
int main() {
    // All quota and identity checks precede authority construction.
    rejects<std::invalid_argument>([] { ActorFrameStack x(0, 1); });
    rejects<std::invalid_argument>([] { ActorFrameStack x(1, 0); });
    rejects<std::invalid_argument>([] { ActorFrameStack x(1, 1, 0); });
    rejects<std::invalid_argument>([] { ActorFrameStack x(1, 1, 65); });
    rejects<std::invalid_argument>([] { ActorFrameStack x(1, 1, 64, 65537); });
    ActorFrameStack a(1, 1), b(1, 1);
    ActorFrameStack::Frame suspended;
    ActorFrameStack::Token stale_turn;
    {
        ActorFrameStack::Turn active(a);
        auto token = a.current_turn(); stale_turn = token;
        auto older = a.push(token, 64);
        suspended = a.push(token, 16);
        std::array<std::uint8_t, 4> value{4, 5, 6, 7}, read{};
        a.write(token, suspended, 8, value);
        a.read(token, suspended, 8, read);
        CHECK(read == value && a.depth(token) == 2);
        rejects<std::out_of_range>([&] { a.read(token, suspended, 15, read); });
        rejects<std::logic_error>([&] { b.read(token, suspended, 8, read); });
        // Another physical carrier must not access the active actor turn.
        std::atomic<bool> turn_denied = false, token_denied = false;
        std::thread intruder([&] {
            try { ActorFrameStack::Turn turn(a); }
            catch (const std::logic_error&) { turn_denied = true; }
            try { (void)a.current_turn(); }
            catch (const std::logic_error&) { token_denied = true; }
        });
        intruder.join();
        CHECK(turn_denied && token_denied);
        // Leave two frames suspended; the owning actor retains their bytes.
        (void)older;
    }
    rejects<std::logic_error>([&] { (void)a.depth(stale_turn); });
    std::atomic<bool> migrated = false;
    std::thread carrier([&] {
        ActorFrameStack::Turn resumed(a);
        auto token = a.current_turn();
        std::array<std::uint8_t, 4> read{};
        a.read(token, suspended, 8, read);
        migrated = read == std::array<std::uint8_t, 4>{4, 5, 6, 7};
        a.pop(token, suspended);
        auto replacement = a.push(token, 16);
        rejects<std::logic_error>([&] { a.read(token, suspended, 8, read); });
        // Reuse must expose zeroed bytes, not a previous actor frame.
        a.read(token, replacement, 8, read);
        CHECK((read == std::array<std::uint8_t, 4>{}));
        a.pop(token, replacement);
        CHECK(a.depth(token) == 1);
    });
    carrier.join();
    CHECK(migrated);
    // Stack depth and used bytes are independent between actors with same id.
    {
        ActorFrameStack::Turn turn(b);
        auto t = b.current_turn();
        CHECK(b.depth(t) == 0);
        auto frame = b.push(t, 64);
        b.pop(t, frame);
    }
    // A bad out-of-order pop poisons only the offending actor incarnation.
    {
        ActorFrameStack bad(9, 1, 2, 16);
        {
            ActorFrameStack::Turn turn(bad);
            auto t = bad.current_turn();
            auto lower = bad.push(t, 4);
            (void)bad.push(t, 4);
            rejects<std::logic_error>([&] { bad.pop(t, lower); });
            rejects<std::logic_error>([&] { (void)bad.push(t, 1); });
        }
        rejects<std::logic_error>([&] { ActorFrameStack::Turn turn(bad); });
        ActorFrameStack sibling(10, 1);
        ActorFrameStack::Turn turn(sibling);
        auto t = sibling.current_turn();
        sibling.pop(t, sibling.push(t, 5));
    }
    // Overflow fails closed (never grows into the next actor's allocation).
    {
        ActorFrameStack bounded(11, 2, 2, 8);
        ActorFrameStack::Turn turn(bounded);
        auto t = bounded.current_turn();
        (void)bounded.push(t, 7);
        rejects<std::length_error>([&] { (void)bounded.push(t, 2); });
        rejects<std::logic_error>([&] { (void)bounded.push(t, 1); });
    }
    // Cross-backend boundary: 64 frames and 64 KiB are inclusive.
    // Separate controls isolate the frame-count and byte-count limits.
    {
        ActorFrameStack bounded(14, 1), sibling(15, 1);
        ActorFrameStack::Turn turn(bounded), peer(sibling);
        auto t = bounded.current_turn(), p = sibling.current_turn();
        auto live = sibling.push(p, 1);
        std::array<std::uint8_t, 1> value{42}, read{};
        sibling.write(p, live, 0, value);
        for (std::size_t i = 0; i < ActorFrameStack::max_frames; ++i)
            (void)bounded.push(t, 1);
        CHECK(bounded.depth(t) == 64);
        rejects<std::length_error>([&] { (void)bounded.push(t, 1); });
        CHECK(bounded.poisoned());
        sibling.read(p, live, 0, read);
        CHECK(read == value);
        sibling.pop(p, live);
    }
    {
        ActorFrameStack bounded(16, 1);
        ActorFrameStack::Turn turn(bounded);
        auto t = bounded.current_turn();
        auto frame = bounded.push(t, ActorFrameStack::max_bytes);
        std::array<std::uint8_t, 1> value{42}, read{};
        bounded.write(t, frame, 65535, value);
        bounded.read(t, frame, 65535, read);
        CHECK(read == value);
        rejects<std::length_error>([&] { (void)bounded.push(t, 1); });
        CHECK(bounded.poisoned());
    }
    // Repeated LIFO reuse and scrubbing without a new storage allocation.
    {
        ActorFrameStack cycles(12, 1, 2, 32);
        ActorFrameStack::Turn turn(cycles);
        auto t = cycles.current_turn();
        for (int i = 0; i < 16384; ++i) {
            auto frame = cycles.push(t, 32);
            std::array<std::uint8_t, 1> zero{};
            cycles.read(t, frame, 0, zero);
            CHECK(zero[0] == 0);
            zero[0] = 0xab;
            cycles.write(t, frame, 0, zero);
            cycles.pop(t, frame);
        }
    }
    // Retirement revokes mid-turn authority but waits for the turn to exit.
    {
        ActorFrameStack doomed(13, 1);
        ActorFrameStack::Token previous;
        {
            ActorFrameStack::Turn turn(doomed);
            previous = doomed.current_turn();
            (void)doomed.push(previous, 20);
            doomed.retire();
            rejects<std::logic_error>([&] { (void)doomed.push(previous, 1); });
        }
        doomed.retire();
        CHECK(doomed.retired());
        rejects<std::logic_error>([&] { ActorFrameStack::Turn turn(doomed); });
    }
    std::cout << "Forge host actor logical frames: quota, exclusivity, bounds, migration, stale handles, poison, retirement, sibling survival, 16384 zeroized reuse cycles PASS\n";
}
