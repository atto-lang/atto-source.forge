#include "runtime/actor.hpp"
#include "runtime/actor_arena.hpp"
#include "runtime/actor_pipeline_policy.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using oresforge::runtime::ActorArena;
using oresforge::runtime::ActorPipePolicy;
using oresforge::runtime::Mailbox;

static void check(bool ok, const char* failure) {
  if (!ok) throw std::runtime_error(failure);
}
template <typename E, typename F>
static void rejects(F&& action) {
  bool rejected = false;
  try { action(); } catch (const E&) { rejected = true; }
  check(rejected, "required rejection did not occur");
}
int main() {
  // Real host components, not the source-language actor compiler.
  ActorPipePolicy route(
      {{"source", "Producer", "readable", "", "Bytes"},
       {"a", "Worker", "writable", "Bytes", ""},
       {"b", "Worker", "writable", "Bytes", ""}},
      {{"source", "a", "Bytes"}, {"source", "b", "Bytes"}});
  Mailbox a(401, 1, 2), b(402, 1, 2);
  ActorArena arenaA(401, 1), arenaB(402, 1);
  auto refA = a.ref();
  auto refB = b.ref();

  // A selected route is NOT a new message capability. Only the
  // destination-specific ActorRef authorizes admission to its mailbox.
  check(route.forward_round_robin("source").target == "a", "first RR");
  std::vector<std::uint8_t> payload{4, 7};
  a.send(refA, payload);
  payload[0] = 99;
  rejects<std::length_error>([&] { a.send(refA, payload); });
  check(a.queued() == 1, "rejected admission consumed a mailbox slot");
  check(route.forward_round_robin("source").target == "b", "second RR");
  b.send(refB, std::array<std::uint8_t, 2>{8, 9});
  check(b.queued() == 1, "second route lost its mailbox credit");

  auto msgA = a.receive(), msgB = b.receive();
  check(msgA.has_value() && msgB.has_value(), "accepted mail vanished");
  check(*msgA == std::vector<std::uint8_t>({4, 7}),
        "mailbox did not snapshot sender-owned memory");
  check(*msgB == std::vector<std::uint8_t>({8, 9}), "wrong route payload");
  check(a.queued() == 0 && b.queued() == 0, "credits not returned");

  auto ta = arenaA.begin_turn(401, 1);
  auto tb = arenaB.begin_turn(402, 1);
  auto ha = arenaA.allocate(ta, 2);
  auto hb = arenaB.allocate(tb, 2);
  std::array<std::byte, 2> pa{std::byte{4}, std::byte{7}};
  std::array<std::byte, 2> pb{std::byte{8}, std::byte{9}};
  arenaA.write(ta, ha, pa);
  arenaB.write(tb, hb, pb);
  std::array<std::byte, 2> probe{};
  rejects<std::logic_error>([&] { arenaB.read(tb, ha, probe); });
  rejects<std::logic_error>([&] { arenaA.read(ta, hb, probe); });
  arenaA.read(ta, ha, probe);
  check(probe == pa, "actor a persistent memory changed");
  arenaA.end_turn(ta);
  arenaB.end_turn(tb);

  // STOP/RESTART revokes the old mailbox authority, but the arena is a
  // distinct lifecycle owner: CONTROL must explicitly retire it.
  a.close();
  rejects<std::runtime_error>([&] { a.send(refA, payload); });
  auto nextRefA = a.restart();
  rejects<std::runtime_error>([&] { a.send(refA, payload); });
  a.send(nextRefA, std::array<std::uint8_t, 2>{1, 2});
  check(a.receive().has_value(), "renewed authority failed admission");
  route.close();
  rejects<std::logic_error>([&] { (void)route.forward_round_robin("source"); });

  arenaA.retire();
  arenaB.retire();
  ActorArena newerA(401, 2);
  auto newerTurn = newerA.begin_turn(401, 2);
  rejects<std::logic_error>([&] { newerA.read(newerTurn, ha, probe); });
  newerA.end_turn(newerTurn);
  newerA.retire();
  a.close();
  b.close();
}
