#include "runtime/actor_arena.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <type_traits>
using oresforge::runtime::ActorArena;
// Public POD tickets were previously forgeable with known actor domain,
// generation and slot indices. Only the arena may now mint a new ticket.
// Copying an already issued ticket is intentional and remains supported.
static_assert(!std::is_aggregate_v<ActorArena::Turn>);
static_assert(!std::is_default_constructible_v<ActorArena::Turn>);
static_assert(!std::is_constructible_v<ActorArena::Turn, std::uint64_t, std::uint64_t>);
static_assert(std::is_copy_constructible_v<ActorArena::Turn>);
static_assert(!std::is_aggregate_v<ActorArena::Handle>);
static_assert(!std::is_default_constructible_v<ActorArena::Handle>);
static_assert(!std::is_constructible_v<ActorArena::Handle,
                                     std::uint64_t, std::uint64_t, std::size_t>);
static_assert(std::is_copy_constructible_v<ActorArena::Handle>);
static_assert(!std::is_aggregate_v<ActorArena::Scratch>);
static_assert(!std::is_default_constructible_v<ActorArena::Scratch>);
static_assert(!std::is_constructible_v<ActorArena::Scratch,
                                     std::uint64_t, std::uint64_t, std::size_t,
                                     std::size_t>);
static_assert(std::is_copy_constructible_v<ActorArena::Scratch>);
static void ensure(bool ok) { if (!ok) throw std::runtime_error("actor arena invariant failed"); }
template <typename F> static void rejects(F&& f) {
  bool caught = false;
  try { f(); } catch (const std::exception&) { caught = true; }
  ensure(caught);
}
int main() {
  ActorArena a(10, 1), b(11, 1);
  auto ta = a.begin_turn(10, 1);
  auto tb = b.begin_turn(11, 1);
  auto h = a.allocate(ta, 2);
  const auto copiedTurn = ta;
  const auto copiedHandle = h;
  std::array<std::byte, 2> value{std::byte{42}, std::byte{9}}, output{};
  a.write(ta, h, value);
  rejects([&] { b.read(tb, h, output); });
  rejects([&] { a.read(tb, h, output); });
  std::thread foreign([&] { rejects([&] { a.read(ta, h, output); }); });
  foreign.join();
  a.read(copiedTurn, copiedHandle, output);
  ensure(output == value);
  ensure(a.try_read_byte(ta,h,0).has_value());
  ensure(a.try_read_byte(ta,h,0).value() == std::byte{42});
  ensure(a.try_read_byte(ta,h,1).value() == std::byte{9});
  for (const std::int64_t invalid : {std::int64_t{-1}, std::int64_t{2},
      std::numeric_limits<std::int64_t>::min(),
      std::numeric_limits<std::int64_t>::max()})
    ensure(!a.try_read_byte(ta,h,invalid).has_value());
  rejects([&] { (void)b.try_read_byte(tb,h,-1); });
  rejects([&] { (void)a.try_read_byte(tb,h,-1); });
  auto scratch = a.scratch_alloc(ta, 2);
  a.scratch_write(ta, scratch, value);
  a.end_turn(ta);
  b.end_turn(tb);
  auto t2 = a.begin_turn(10, 1);
  rejects([&] { a.scratch_read(t2, scratch, output); });
  auto fresh = a.scratch_alloc(t2, 2);
  a.scratch_read(t2, fresh, output);
  ensure(output == (std::array<std::byte, 2>{}));
  a.read(t2, h, output);
  ensure(output == value);
  a.drop(t2, h);
  rejects([&] { (void)a.try_read_byte(t2,h,-1); });
  rejects([&] { a.read(t2, h, output); });
  auto replacement = a.allocate(t2, 2);
  rejects([&] { a.read(t2, h, output); });
  a.read(t2, replacement, output);
  ensure(output == (std::array<std::byte, 2>{}));
  rejects([&] { a.retire(); });
  a.end_turn(t2);
  a.retire(); a.retire();
  rejects([&] { a.begin_turn(10, 1); });
  ActorArena restarted(10, 2);
  auto tr = restarted.begin_turn(10, 2);
  rejects([&] { restarted.read(tr, replacement, output); });
  restarted.end_turn(tr); restarted.retire();
  // Turn authority is carrier-specific and may migrate only after end_turn.
  ActorArena migrating(200, 7);
  auto m0 = migrating.begin_turn(200, 7);
  auto mh = migrating.allocate(m0, 8);
  std::array<std::byte, 8> payload{};
  payload[0] = std::byte{41};
  migrating.write(m0, mh, payload);
  std::atomic<bool> wrongCarrierDenied{false};
  std::thread blocked([&] {
    try { std::array<std::byte, 8> buffer{}; migrating.read(m0, mh, buffer); }
    catch (const std::logic_error&) { wrongCarrierDenied = true; }
  });
  blocked.join();
  ensure(wrongCarrierDenied.load());
  migrating.end_turn(m0);
  std::atomic<bool> migrated{false};
  std::thread nextCarrier([&] {
    auto t = migrating.begin_turn(200, 7);
    std::array<std::byte, 8> copy{};
    migrating.read(t, mh, copy);
    migrated = (copy == payload);
    migrating.end_turn(t);
  });
  nextCarrier.join();
  ensure(migrated.load());
  rejects([&] { migrating.begin_turn(200, 8); });
  rejects([&] { migrating.begin_turn(201, 7); });
  auto m1 = migrating.begin_turn(200, 7);
  // 8192 deterministic generational reallocations ensure stale handles never
  // regain authority and fresh slots have their bytes zeroed.
  for (int i = 0; i < 8192; ++i) {
    auto temp = migrating.allocate(m1, 8);
    migrating.write(m1, temp, payload);
    migrating.drop(m1, temp);
    rejects([&] { std::array<std::byte, 8> result{}; migrating.read(m1, temp, result); });
    auto fresh = migrating.allocate(m1, 8);
    std::array<std::byte, 8> result{};
    migrating.read(m1, fresh, result);
    ensure(result == (std::array<std::byte,8>{}));
    migrating.drop(m1, fresh);
  }
  migrating.end_turn(m1);
  migrating.retire();
}
