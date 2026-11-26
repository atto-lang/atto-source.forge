// Native mailbox lifetime regression, not a physical actor-retirement receipt.
#include "runtime/actor.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using namespace oresforge::runtime;
static void check(bool ok) {
  if (!ok) throw std::runtime_error("mailbox incarnation reuse invariant");
}

int main() {
  Mailbox actor(1001, 1, 1);
  Mailbox foreign(1001, 1, 1);
  Mailbox sibling(1002, 1, 1);
  const auto initial = actor.ref();
  const auto sibling_ref = sibling.ref();
  auto current = initial;
  const std::array<std::uint8_t, 1> old_payload{99}, fresh_payload{7}, sibling_payload{42};
  for (std::uint64_t i = 0; i < 100000; ++i) {
    check(actor.try_send(current, old_payload));
    check(!actor.try_send(current, old_payload));
    actor.close();
    check(actor.queued() == 0 && !actor.receive() && !actor.authorizes(current));
    auto next = actor.restart();
    check(next.generation == i + 2 && actor.authorizes(next));
    check(!actor.authorizes(current) && !actor.authorizes(initial));
    // Even matching public numeric fields do not restore retired or foreign authority.
    auto old = current;
    old.generation = next.generation;
    auto other = foreign.ref();
    other.generation = next.generation;
    check(!actor.authorizes(old) && !actor.authorizes(other));
    check(actor.try_send(next, fresh_payload));
    check(!actor.try_send(next, fresh_payload));
    auto received = actor.receive();
    check(received && received->size() == 1 && (*received)[0] == 7);
    check(actor.queued() == 0 && !actor.receive());
    current = next;
  }
  actor.close();
  check(sibling.try_send(sibling_ref, sibling_payload));
  auto received = sibling.receive();
  check(received && received->size() == 1 && (*received)[0] == 42);
  check(!actor.authorizes(current));
  std::cout << "PASS FORGE_MAILBOX_REUSE_CYCLES=100000 STALE_AUTHORITY=0 SIBLING=42\n";
}
