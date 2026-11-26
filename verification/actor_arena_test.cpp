#include "runtime/actor_arena.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <thread>
using oresforge::runtime::ActorArena;
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
  std::array<std::byte, 2> value{std::byte{42}, std::byte{9}}, output{};
  a.write(ta, h, value);
  rejects([&] { b.read(tb, h, output); });
  rejects([&] { a.read(tb, h, output); });
  std::thread foreign([&] { rejects([&] { a.read(ta, h, output); }); });
  foreign.join();
  a.read(ta, h, output);
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
  // Shared arena lifecycle v1: failed admission is accounting-neutral and
  // reclaim/restart of one actor never changes another actor's private bytes.
  {
    ActorArena first(701, 1), sibling(702, 1);
    auto t = first.begin_turn(701, 1);
    auto s = sibling.begin_turn(702, 1);
    std::array<std::byte, 1> marker{std::byte{17}}, out{};
    std::array<std::byte, 1> other{std::byte{9}};
    auto keep = first.allocate(t, 1);
    first.write(t, keep, marker);
    auto sibling_handle = sibling.allocate(s, 1);
    sibling.write(s, sibling_handle, other);
    for (std::size_t i = 1; i < ActorArena::Slots - 1; ++i)
      (void)first.allocate(t, 1);
    for (int i = 0; i < 8; ++i)
      rejects([&] { (void)first.allocate(t, ActorArena::SlotBytes + 1); });
    auto last = first.allocate(t, 1);
    first.read(t, last, out);
    ensure(out[0] == std::byte{0});
    rejects([&] { (void)first.allocate(t, 1); });
    first.read(t, keep, out);
    ensure(out == marker);
    auto scratch = first.scratch_alloc(t, ActorArena::ScratchBytes - 8);
    (void)scratch;
    for (int i = 0; i < 8; ++i)
      rejects([&] { (void)first.scratch_alloc(t, 9); });
    auto tail = first.scratch_alloc(t, 8);
    std::array<std::byte, 8> scratch_out{};
    first.scratch_read(t, tail, scratch_out);
    ensure(scratch_out == (std::array<std::byte, 8>{}));
    rejects([&] { (void)first.scratch_alloc(t, 1); });
    first.end_turn(t);
    first.retire();
    sibling.read(s, sibling_handle, out);
    ensure(out == other);
    ActorArena restarted(701, 2);
    auto r = restarted.begin_turn(701, 2);
    rejects([&] { restarted.read(r, keep, out); });
    auto fresh = restarted.allocate(r, 1);
    restarted.read(r, fresh, out);
    ensure(out[0] == std::byte{0});
    restarted.end_turn(r);
    restarted.retire();
    sibling.end_turn(s);
    sibling.retire();
  }
}
