#include "runtime/actor_arena.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>

using oresforge::runtime::ActorArena;
static void check(bool ok) {
  if (!ok) throw std::runtime_error("arena domain reuse invariant");
}
template <typename F> static void denied(F&& f) {
  try { f(); } catch (const std::logic_error&) { return; }
  throw std::runtime_error("stale or active arena operation admitted");
}

int main() {
  constexpr std::uint64_t cycles = 16384;
  std::optional<ActorArena> storage;
  std::optional<ActorArena::Turn> previous_turn;
  std::optional<ActorArena::Handle> previous_handle;
  std::optional<ActorArena::Scratch> previous_scratch;
  ActorArena sibling(42, 1);
  const auto sibling_turn = sibling.begin_turn(42, 1);
  const auto sibling_handle = sibling.allocate(sibling_turn, 1);
  sibling.write(sibling_turn, sibling_handle, std::array{std::byte{42}});
  for (std::uint64_t i = 0; i < cycles; ++i) {
    // Reconstruct at the same address with the same public numeric IDs.
    auto& arena = storage.emplace(7, 1);
    const auto turn = arena.begin_turn(7, 1);
    const auto handle = arena.allocate(turn, 1);
    const auto scratch = arena.scratch_alloc(turn, 1);
    std::array out{std::byte{255}};
    arena.read(turn, handle, out);
    check(out[0] == std::byte{0});
    arena.scratch_read(turn, scratch, out);
    check(out[0] == std::byte{0});
    if (previous_turn) {
      check(turn.domain > previous_turn->domain);
      denied([&] { arena.read(*previous_turn, handle, out); });
      denied([&] { arena.read(turn, *previous_handle, out); });
      denied([&] { arena.scratch_read(turn, *previous_scratch, out); });
    }
    denied([&] { arena.read(turn, sibling_handle, out); });
    denied([&] { arena.retire(); });
    arena.write(turn, handle, std::array{std::byte{99}});
    arena.scratch_write(turn, scratch, std::array{std::byte{99}});
    arena.end_turn(turn);
    arena.retire();
    denied([&] { (void)arena.begin_turn(7, 1); });
    previous_turn = turn;
    previous_handle = handle;
    previous_scratch = scratch;
    storage.reset();
    sibling.read(sibling_turn, sibling_handle, out);
    check(out[0] == std::byte{42});
  }
  sibling.end_turn(sibling_turn);
  sibling.retire();
  std::cout << "PASS ARENA_REUSE_CYCLES=16384 STALE_DOMAIN_DENIED=1 ACTIVE_RETIRE_DENIED=1 ZEROED=1 SIBLING=42\n";
}
