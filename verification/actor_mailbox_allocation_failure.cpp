#include "runtime/actor.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <vector>

// Test-only single-shot allocation fault. Never enable in runtime builds.
namespace {
std::atomic<bool> fail_next_allocation{false};

void require(bool ok, const char* diagnostic) {
  if (!ok) throw std::runtime_error(diagnostic);
}

template<class F>
void rejects_invalid(F fn) {
  try { fn(); }
  catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("invalid mailbox configuration was accepted");
}

template<class F>
void rejects_stale(F fn) {
  try { fn(); }
  catch (const std::runtime_error&) { return; }
  throw std::runtime_error("stale mailbox capability was accepted");
}
}

// This is a deterministic allocation hook, not a test of allocator internals.
void* operator new(std::size_t bytes) {
  if (fail_next_allocation.exchange(false, std::memory_order_acq_rel))
    throw std::bad_alloc();
  if (void* result = std::malloc(bytes ? bytes : 1)) return result;
  throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

int main() {
  try {
    using oresforge::runtime::Mailbox;
    using oresforge::runtime::ActorRef;
    static_assert(!std::is_trivially_copyable_v<ActorRef>,
                  "ActorRef authority must not become raw wire bytes");
    rejects_invalid([] { Mailbox invalid(0, 1, 1); });
    rejects_invalid([] { Mailbox invalid(1, 4097, 1); });
    rejects_invalid([] { Mailbox invalid(1, 1, 16 * 1024 * 1024 + 1); });

    Mailbox box(42, 2, 16);
    const ActorRef first = box.ref();
    const std::array<std::uint8_t, 3> data{1, 2, 3};
    box.send(first, data);
    fail_next_allocation.store(true, std::memory_order_release);
    bool failed_restart = false;
    try { (void)box.restart(); }
    catch (const std::bad_alloc&) { failed_restart = true; }
    fail_next_allocation.store(false, std::memory_order_release);
    require(failed_restart, "injected restart allocation did not fail");
    require(box.queued() == 1,
            "restart OOM discarded previously admitted message");
    box.send(first, data);
    require(box.queued() == 2,
            "restart OOM invalidated prior actor authority");
    auto a = box.receive();
    auto b = box.receive();
    require(a && b && *a == std::vector<std::uint8_t>(data.begin(), data.end())
               && *b == *a && box.queued() == 0,
            "restart OOM mutated queue or lost contents");

    // Copy allocation failure may not consume count/byte quota.
    fail_next_allocation.store(true, std::memory_order_release);
    bool failed_send = false;
    try { box.send(first, data); }
    catch (const std::bad_alloc&) { failed_send = true; }
    fail_next_allocation.store(false, std::memory_order_release);
    require(failed_send && box.queued() == 0,
            "failed message copy incorrectly reserved queue capacity");
    box.send(first, data);
    box.send(first, data);
    require(box.queued() == 2, "failed copy leaked one queue slot");
    auto fresh = box.restart();
    require(box.queued() == 0, "successful restart retained old DATA");
    rejects_stale([&] { box.send(first, data); });
    box.send(fresh, data);
    require(box.queued() == 1, "fresh authority rejected after restart");

    // Failed constructor allocation must not publish an ActorRef or leak
    // a partially constructed Mailbox.
    fail_next_allocation.store(true, std::memory_order_release);
    bool failed_ctor = false;
    try { Mailbox unused(999, 1, 1); }
    catch (const std::bad_alloc&) { failed_ctor = true; }
    fail_next_allocation.store(false, std::memory_order_release);
    require(failed_ctor, "constructor capability allocation did not fail");

    std::cout << "actor mailbox allocation failure: pass\n";
    return 0;
  } catch (const std::exception& e) {
    fail_next_allocation.store(false, std::memory_order_release);
    std::cerr << "actor mailbox allocation failure: " << e.what() << '\n';
    return 1;
  }
}
