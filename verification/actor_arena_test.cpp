#include "runtime/actor_arena.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <thread>
using oresforge::runtime::ActorArena;
template <typename F> static void rejects(F&& f) {
  bool caught = false;
  try { f(); } catch (const std::exception&) { caught = true; }
  assert(caught);
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
  assert(output == value);
  auto scratch = a.scratch_alloc(ta, 2);
  a.scratch_write(ta, scratch, value);
  a.end_turn(ta);
  b.end_turn(tb);
  auto t2 = a.begin_turn(10, 1);
  rejects([&] { a.scratch_read(t2, scratch, output); });
  auto fresh = a.scratch_alloc(t2, 2);
  a.scratch_read(t2, fresh, output);
  assert(output == (std::array<std::byte, 2>{}));
  a.read(t2, h, output);
  assert(output == value);
  a.drop(t2, h);
  rejects([&] { a.read(t2, h, output); });
  auto replacement = a.allocate(t2, 2);
  rejects([&] { a.read(t2, h, output); });
  a.read(t2, replacement, output);
  assert(output == (std::array<std::byte, 2>{}));
  rejects([&] { a.retire(); });
  a.end_turn(t2);
  a.retire(); a.retire();
  rejects([&] { a.begin_turn(10, 1); });
  ActorArena restarted(10, 2);
  auto tr = restarted.begin_turn(10, 2);
  rejects([&] { restarted.read(tr, replacement, output); });
  restarted.end_turn(tr); restarted.retire();
}
