#include "runtime/BatchedCheckpointV1.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using oresforge::runtime::BatchedCheckpointV1;
static void require(bool ok) {
  if (!ok) throw std::runtime_error("Forge batched checkpoint differential invariant failed");
}
static void check(std::uint32_t interval, std::uint64_t fuel) {
  std::vector<std::pair<std::uint64_t, std::uint64_t>> events;
  BatchedCheckpointV1 q(fuel, [&](auto charged, auto remaining) {
    events.emplace_back(charged, remaining);
  }, interval);
  for (std::uint64_t committed = 1; committed <= fuel; ++committed) {
    const bool expectedPoll = (committed % interval == 0) || (committed == fuel);
    require(q.completedWorkUnit() == expectedPoll);
    require(q.remainingFuel() == fuel - committed);
  }
  std::vector<std::pair<std::uint64_t, std::uint64_t>> expected;
  for (std::uint64_t at = interval; at < fuel; at += interval)
    expected.emplace_back(interval, fuel - at);
  if (fuel) expected.emplace_back(fuel % interval == 0 ? interval : fuel % interval, 0);
  require(events == expected);
  bool exhausted = false;
  try { q.completedWorkUnit(); } catch (const std::runtime_error&) { exhausted = true; }
  require(exhausted);
  bool terminal = false;
  try { q.completedWorkUnit(); } catch (const std::logic_error&) { terminal = true; }
  require(terminal);
  require(events == expected); // no extra callback after failure
}
int main() {
  constexpr std::array<std::uint32_t,4> intervals{64u, 128u, 256u, 512u};
  constexpr std::array<std::uint64_t,19> lengths{
    0u, 1u, 2u, 63u, 64u, 65u, 127u, 128u, 129u, 255u,
    256u, 257u, 511u, 512u, 513u, 1023u, 1024u, 1025u, 4097u
  };
  std::uint32_t seed = 0x1eaf4b37u;
  for (const auto interval : intervals) {
    for (const auto fuel : lengths) check(interval, fuel);
    for (int n = 0; n < 32; ++n) {
      seed = seed * 1664525u + 1013904223u;
      check(interval, seed % 2049u);
    }
  }
  // A second logical task must not obtain additional fuel merely because
  // another task cooperated or exhausted its independent allowance.
  BatchedCheckpointV1 a(129, [](auto, auto) {});
  BatchedCheckpointV1 b(257, [](auto, auto) {});
  for (int i = 0; i < 129; ++i) { a.completedWorkUnit(); b.completedWorkUnit(); }
  require(a.remainingFuel() == 0u && b.remainingFuel() == 128u);
  for (int i = 0; i < 128; ++i) b.completedWorkUnit();
  require(b.remainingFuel() == 0u);
  std::cout << "PASS Forge 204 deterministic batching / final-fuel scenarios (host-only)\n";
}
