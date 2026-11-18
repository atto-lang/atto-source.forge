#include "runtime/actor.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace {
constexpr std::size_t kBytes = 4093;
constexpr std::size_t kSlots = 3;
constexpr std::uint8_t fill = 0xa5;
std::array<void*, kSlots> tracked{};
std::array<bool, kSlots> should_wipe{};
std::size_t expected = 0, captured = 0, released = 0;
bool armed = false, clean = true, fail_next_new = false;

// Inspect each owned allocation while it is still live, immediately before free.
// The test deliberately targets payload allocations of an uncommon exact size.
void check_free(void* p) noexcept {
  for (std::size_t slot = 0; slot < captured; ++slot) {
    if (tracked[slot] != p) continue;
    const auto* bytes = static_cast<const std::uint8_t*>(p);
    const std::uint8_t want = should_wipe[slot] ? 0 : fill;
    for (std::size_t i = 0; i < kBytes; ++i)
      if (bytes[i] != want) clean = false;
    tracked[slot] = nullptr;
    ++released;
    break;
  }
  std::free(p);
}
void arm(std::array<bool, kSlots> expectations, std::size_t count) {
  if (armed || captured != released || count == 0 || count > kSlots)
    throw std::runtime_error("a previous payload-tracking session is incomplete");
  tracked.fill(nullptr);
  should_wipe = expectations;
  expected = count;
  captured = released = 0;
  clean = true;
  armed = true;
}
void check(const char* label) {
  if (armed || captured != expected || released != expected || !clean)
    throw std::runtime_error(label);
}
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
} // namespace

void* operator new(std::size_t size) {
  if (fail_next_new) {
    fail_next_new = false;
    throw std::bad_alloc{};
  }
  void* p = std::malloc(size ? size : 1);
  if (!p) throw std::bad_alloc{};
  if (armed && size == kBytes && captured < expected) {
    tracked[captured++] = p;
    if (captured == expected) armed = false;
  }
  return p;
}
void operator delete(void* p) noexcept { check_free(p); }
void operator delete(void* p, std::size_t) noexcept { check_free(p); }
void* operator new[](std::size_t size) {
  void* p = std::malloc(size ? size : 1);
  if (!p) throw std::bad_alloc{};
  return p;
}
void operator delete[](void* p) noexcept { check_free(p); }
void operator delete[](void* p, std::size_t) noexcept { check_free(p); }

int main() {
  using oresforge::runtime::Mailbox;
  static_assert(std::is_nothrow_destructible_v<Mailbox>);
  try {
    std::array<std::uint8_t, kBytes> sender{};
    sender.fill(fill);
    {
      Mailbox box(99001);
      arm({true, true, true}, 3);
      for (int i = 0; i < 3; ++i) box.send(box.ref(), sender);
    }
    check("destructor failed to erase all three undelivered allocations");
    {
      Mailbox box(99002);
      arm({true, true, true}, 3);
      for (int i = 0; i < 3; ++i) box.send(box.ref(), sender);
      box.close();
      box.close(); // idempotent teardown
    }
    check("close failed to erase all queued allocations");
    {
      Mailbox box(99003);
      const auto stale = box.ref();
      arm({true, true, true}, 3);
      for (int i = 0; i < 3; ++i) box.send(stale, sender);
      const auto fresh = box.restart();
      check("restart failed to erase all queued allocations");
      bool rejected = false;
      try { box.send(stale, sender); }
      catch (const std::runtime_error&) { rejected = true; }
      require(rejected, "restart accepted the previous authority");
      box.send(fresh, std::span<const std::uint8_t>{});
      const auto empty = box.receive();
      require(empty.has_value() && empty->empty(), "restart lost empty DATA");
    }
    {
      Mailbox box(99004);
      arm({false, true, true}, 3);
      for (int i = 0; i < 3; ++i) box.send(box.ref(), sender);
      auto delivered = box.receive();
      require(delivered && delivered->size() == kBytes,
              "delivery failed to transfer owned storage");
      box.close();
      require((*delivered)[0] == fill, "close erased recipient-owned payload");
      delivered.reset();
    }
    check("mixed ownership: delivered bytes or pending erasure incorrect");

    // A failed authority allocation must not advance the generation,
    // revoke current authority, reopen/close the mailbox, or drop messages.
    {
      Mailbox box(99005);
      const auto previous = box.ref();
      box.send(previous, sender);
      fail_next_new = true;
      bool rejected = false;
      try { (void)box.restart(); }
      catch (const std::bad_alloc&) { rejected = true; }
      require(rejected && !fail_next_new, "restart did not propagate injected OOM");
      require(box.queued() == 1, "failed restart dropped queued bytes");
      box.send(previous, std::span<const std::uint8_t>{});
      const auto original = box.receive();
      const auto empty = box.receive();
      require(original && original->size() == kBytes && original->at(0) == fill,
              "failed restart lost original payload");
      require(empty && empty->empty(), "failed restart revoked old authority");
      const auto fresh = box.restart();
      bool stale_rejected = false;
      try { box.send(previous, sender); }
      catch (const std::runtime_error&) { stale_rejected = true; }
      require(stale_rejected, "successful retry failed to revoke old authority");
      box.send(fresh, std::span<const std::uint8_t>{});
    }

    // Failure while copying a payload must not consume queue/byte credits.
    {
      Mailbox box(99006, 2, 2 * kBytes);
      const auto ref = box.ref();
      fail_next_new = true;
      bool rejected = false;
      try { box.send(ref, sender); }
      catch (const std::bad_alloc&) { rejected = true; }
      require(rejected && !fail_next_new, "send did not propagate injected OOM");
      require(box.queued() == 0, "failed send consumed a queue slot");
      box.send(ref, sender);
      box.send(ref, sender);
      bool full_rejected = false;
      try { box.send(ref, sender); }
      catch (const std::length_error&) { full_rejected = true; }
      require(full_rejected && box.queued() == 2, "byte/slot cap not enforced");
      require(box.receive().has_value(), "receive failed to reclaim credit");
      box.send(ref, sender);
    }
    for (auto x : sender)
      require(x == fill, "mailbox mutated sender-owned bytes");
    std::cout
      << "PASS multi-payload destructor/close/restart erase, mixed ownership, "
         "restart/send OOM rollback and queue-credit recovery\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "mailbox lifecycle hardening FAILED: " << e.what() << '\n';
    return 1;
  }
}
