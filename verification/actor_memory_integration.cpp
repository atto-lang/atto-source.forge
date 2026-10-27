#include "runtime/actor.hpp"
#include "runtime/actor_arena.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

using oresforge::runtime::ActorArena;
using oresforge::runtime::Mailbox;
static void check(bool ok) {
  if (!ok) throw std::runtime_error("actor memory integration failed");
}
template<class F> static void denied(F&& f) {
  bool rejected = false;
  try { f(); } catch (const std::exception&) { rejected = true; }
  check(rejected);
}
int main() {
  // Real current-main host mailbox + private arena. No guest scheduler claim.
  Mailbox mailbox(401, 1, 2);
  ActorArena a(401, 1), b(402, 1);
  auto ref = mailbox.ref();
  std::vector<std::uint8_t> sent{4, 7};
  mailbox.send(ref, sent);
  sent[0] = 99;
  denied([&] { mailbox.send(ref, sent); });
  auto received = mailbox.receive();
  check(received && *received == std::vector<std::uint8_t>({4, 7}));
  check(mailbox.queued() == 0);
  auto ta = a.begin_turn(401, 1);
  auto tb = b.begin_turn(402, 1);
  auto ha = a.allocate(ta, 2);
  auto hb = b.allocate(tb, 2);
  std::array<std::byte, 2> av{std::byte{4}, std::byte{7}};
  std::array<std::byte, 2> bv{std::byte{8}, std::byte{9}}, out{};
  a.write(ta, ha, av);
  b.write(tb, hb, bv);
  denied([&] { a.read(ta, hb, out); });
  denied([&] { b.read(tb, ha, out); });
  a.read(ta, ha, out); check(out == av);
  b.read(tb, hb, out); check(out == bv);
  b.end_turn(tb);
  b.retire();

  // CONTROL may seal while a carrier is active. The old carrier retains only
  // leave authority: no byte operations or allocation after retirement.
  std::atomic<bool> sealed{false};
  std::thread control([&] { a.retire(); sealed = true; });
  control.join();
  check(sealed);
  denied([&] { a.read(ta, ha, out); });
  denied([&] { a.write(ta, ha, av); });
  denied([&] { (void)a.allocate(ta, 2); });
  denied([&] { (void)a.scratch_alloc(ta, 2); });
  denied([&] { a.drop(ta, ha); });
  denied([&] { (void)a.begin_turn(401, 1); });
  // A stolen token still cannot leave from another carrier, even after STOP.
  std::thread foreign([&] { denied([&] { a.end_turn(ta); }); });
  foreign.join();
  a.end_turn(ta);
  denied([&] { a.end_turn(ta); });
  a.retire();

  mailbox.close();
  denied([&] { mailbox.send(ref, sent); });
  auto next = mailbox.restart();
  denied([&] { mailbox.send(ref, sent); });
  mailbox.send(next, sent);
  check(mailbox.receive().has_value());
  ActorArena restarted(401, 2);
  auto rt = restarted.begin_turn(401, 2);
  denied([&] { restarted.read(rt, ha, out); });
  auto fresh = restarted.allocate(rt, 2);
  restarted.read(rt, fresh, out);
  check(out == (std::array<std::byte, 2>{}));
  restarted.end_turn(rt);
  restarted.retire();
  mailbox.close();
}
