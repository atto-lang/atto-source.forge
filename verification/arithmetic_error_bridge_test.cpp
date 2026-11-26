#include "runtime/ArithmeticErrorBridge.hpp"
#include <cassert>
#include <optional>

using namespace oresforge::runtime;

int main() {
  using Arena = ErrorArena<4, 16>;
  using Bridge = ArithmeticErrorBridge<4, 16>;
  Arena arena, foreign;
  Bridge bridge(arena), other(foreign);
  for (auto code : {0ull, 1ull, 65536ull, 65539ull, 18446744073709551615ull}) {
    const auto bad = bridge.capture_new(code);
    assert(bad.status == ErrorAllocation::InvalidKind);
    assert(!bridge.forward(bad.handle));
    assert(!bridge.legacy_status(bad.handle));
  }
  assert(arena.size() == 0);
  const auto zero1 = bridge.capture_new(65538);
  const auto zero2 = bridge.capture_new(65538);
  const auto overflow = bridge.capture_new(65537);
  assert(zero1.status == ErrorAllocation::Created);
  assert(zero2.status == ErrorAllocation::Created);
  assert(overflow.status == ErrorAllocation::Created);
  assert(!arena.same_identity(zero1.handle, zero2.handle));
  assert(arena.view(zero1.handle)->kind == ErrorKind::DivisionByZero);
  assert(arena.view(zero1.handle)->message() == "division by zero");
  assert(arena.view(overflow.handle)->kind == ErrorKind::ArithmeticOverflow);
  assert(arena.view(overflow.handle)->message() == "integer overflow");
  assert(bridge.legacy_status(zero1.handle) == 65538);
  assert(bridge.legacy_status(overflow.handle) == 65537);
  const auto general = arena.create(ErrorKind::Error, "other");
  assert(general.status == ErrorAllocation::Created);
  assert(!bridge.legacy_status(general.handle));
  assert(bridge.forward(general.handle));
  const auto forwarded = bridge.forward(zero1.handle);
  assert(forwarded && arena.same_identity(*forwarded, zero1.handle));
  assert(arena.size() == 4);
  assert(bridge.capture_new(65538).status == ErrorAllocation::Exhausted);
  assert(bridge.forward(zero1.handle));
  assert(!other.forward(zero1.handle));
  assert(!other.legacy_status(zero1.handle));
  assert(foreign.size() == 0);
  ErrorArena<1, 3> small;
  ArithmeticErrorBridge<1, 3> small_bridge(small);
  assert(small_bridge.capture_new(65537).status == ErrorAllocation::MessageTooLong);
  assert(small.size() == 0);
  std::optional<Arena> reused(std::in_place);
  const auto stale = Bridge(*reused).capture_new(65538).handle;
  reused.reset();
  reused.emplace();
  Bridge fresh(*reused);
  const auto current = fresh.capture_new(65538);
  assert(current.status == ErrorAllocation::Created);
  assert(!fresh.forward(stale));
  assert(!fresh.legacy_status(stale));
  assert(fresh.legacy_status(current.handle) == 65538);
  return 0;
}
