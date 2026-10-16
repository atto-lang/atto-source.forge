#include "runtime/actor.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

using oresforge::runtime::ActorRef;
using oresforge::runtime::Mailbox;

namespace {
void expect(bool result, const char* message) {
  if (!result) throw std::runtime_error(message);
}
std::uint64_t next(std::uint64_t& state) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}
void run_model(std::uint64_t seed) {
  const std::size_t message_capacity = 1 + seed % 23;
  const std::size_t byte_capacity = 1 + (seed * 17) % 97;
  const std::uint64_t id = 5000 + seed;
  Mailbox box(id, message_capacity, byte_capacity);
  Mailbox foreign(id, message_capacity, byte_capacity);
  ActorRef ref = box.ref();
  ActorRef stale = ref;
  bool have_stale = false;
  bool closed = false;
  std::deque<std::vector<std::uint8_t>> oracle;
  std::size_t bytes = 0;
  std::uint64_t rng = seed * UINT64_C(0x9e3779b97f4a7c15) + 1;

  for (int step = 0; step < 4000; ++step) {
    const auto operation = next(rng) % 12;
    if (operation <= 5) {
      const auto size = static_cast<std::size_t>(
          next(rng) % (byte_capacity + 14));
      std::vector<std::uint8_t> payload(size);
      for (auto& byte : payload)
        byte = static_cast<std::uint8_t>(next(rng));
      const bool full = oracle.size() >= message_capacity ||
          size > byte_capacity - bytes;
      enum class Outcome { admitted, full, stopped };
      const auto expected = closed ? Outcome::stopped :
          (full ? Outcome::full : Outcome::admitted);
      // Capture the actual result before asserting: assertion failures must
      // never be mistaken for expected std::runtime_error send failures.
      Outcome observed = Outcome::admitted;
      try { box.send(ref, payload); }
      catch (const std::length_error&) { observed = Outcome::full; }
      catch (const std::runtime_error&) { observed = Outcome::stopped; }
      expect(observed == expected,
             "mailbox admission differs from reference state machine");
      if (observed == Outcome::admitted) {
        oracle.push_back(payload);
        bytes += payload.size();
        if (!payload.empty()) payload.front() ^= 0xff;
      }
    } else if (operation == 6) {
      auto actual = box.receive();
      expect(actual.has_value() == !oracle.empty(),
             "message presence diverged");
      if (actual) {
        expect(*actual == oracle.front(), "message was lost or mutated");
        bytes -= oracle.front().size();
        oracle.pop_front();
      }
    } else if (operation == 7) {
      box.close();
      oracle.clear();
      bytes = 0;
      closed = true;
    } else if (operation == 8) {
      stale = ref;
      have_stale = true;
      ref = box.restart();
      oracle.clear();
      bytes = 0;
      closed = false;
    } else if (operation == 9) {
      // Numeric identities are public metadata, not send authority.
      // The only valid references originate from Mailbox::ref/restart.
      expect(!std::is_constructible_v<ActorRef, std::uint64_t, std::uint64_t>,
             "numeric-only actor reference is constructible");
    } else if (operation == 10) {
      bool rejected = false;
      try { box.send(foreign.ref(), std::vector<std::uint8_t>{}); }
      catch (const std::runtime_error&) { rejected = true; }
      expect(rejected, "same-ID foreign mailbox gained authority");
    } else if (have_stale) {
      bool rejected = false;
      try { box.send(stale, std::vector<std::uint8_t>{}); }
      catch (const std::runtime_error&) { rejected = true; }
      expect(rejected, "revoked pre-restart capability was accepted");
    }
    expect(box.queued() == oracle.size(), "message count diverged");
    expect(bytes <= byte_capacity, "model exceeded payload budget");
  }
  while (!oracle.empty()) {
    auto actual = box.receive();
    expect(actual && *actual == oracle.front(),
           "drain lost message ordering or bytes");
    oracle.pop_front();
  }
  expect(box.queued() == 0, "mailbox failed to drain");
}
} // namespace

int main() {
  try {
    for (std::uint64_t seed = 1; seed <= 32; ++seed) run_model(seed);
    std::cout << "mailbox deterministic state machine: 128000 steps passed\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << "mailbox model FAILED: " << failure.what() << '\n';
    return 1;
  }
}
