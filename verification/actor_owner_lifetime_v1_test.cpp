// Native host-owner lifecycle regression, not compiler-generated guest execution.
#include "runtime/ActorIncarnationOwnerV1.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

using namespace oresforge::runtime;
static void check(bool ok, const char* reason) {
  if (!ok) throw std::runtime_error(reason);
}
template <typename E, typename F> static void denied(F&& f, std::string_view reason) {
  try { f(); }
  catch (const E& error) {
    check(error.what() == reason, "unexpected rejection reason");
    return;
  }
  throw std::runtime_error("expected rejection did not occur");
}

int main() {
  constexpr std::uint64_t cycles = 100000;
  const auto baseline = ActorMemoryAggregateBudgetV1::process().reserved();
  ActorMemoryAggregateBudgetV1 budget(640);
  ActorIncarnationOwnerV1 a(8405, budget, 256, 64, 4, 2);
  ActorIncarnationOwnerV1 b(8406, budget, 256, 64, 4, 2);
  auto live = a.ref();
  const auto first = live;
  const auto sibling = b.ref();
  std::optional<ActorMemoryHeapHandle> previousHeap, siblingHeap;
  b.send(sibling, std::array<std::uint8_t,1>{42});
  check(b.dispatch_one(sibling, [&](auto& ctx, auto payload) {
    check(payload.size() == 1 && payload[0] == 42, "sibling init payload");
    siblingHeap = ctx.heap_alloc(1);
    ctx.heap_write(*siblingHeap, 0, payload);
  }), "sibling init not dispatched");
  for (std::uint64_t cycle = 0; cycle < cycles; ++cycle) {
    check(budget.reserved() == 640 &&
          ActorMemoryAggregateBudgetV1::process().reserved() == baseline + 640,
          "active owner budget mismatch");
    a.send(live, std::array<std::uint8_t,1>{7});
    check(a.dispatch_one(live, [&](auto& ctx, auto payload) {
      check(payload.size() == 1 && payload[0] == 7, "fresh DATA mismatch");
      std::array<std::uint8_t,1> out{255};
      if (previousHeap) denied<std::runtime_error>([&] { ctx.heap_read(*previousHeap, 0, out); },
                                             "stale or foreign actor heap capability");
      auto h = ctx.heap_alloc(8);
      ctx.heap_read(h, 0, out);
      check(out[0] == 0, "replacement heap not zeroed");
      ctx.heap_write(h, 0, std::array<std::uint8_t,1>{99});
      previousHeap = h;
      auto e = ctx.begin_frames();
      auto f = ctx.frame_push(e, 16);
      ctx.frame_read(e, f, 0, out);
      check(out[0] == 0, "replacement logical stack not zeroed");
      ctx.frame_pop(e, f);
      ctx.finish_frames(e);
    }), "healthy owner turn missing");
    a.send(live, std::array<std::uint8_t,1>{8});
    a.send(live, std::array<std::uint8_t,1>{99});
    denied<std::runtime_error>([&] {
      a.dispatch_one(live, [&](auto& ctx, auto) {
        auto e = ctx.begin_frames();
        (void)ctx.frame_push(e, 64);
        (void)ctx.frame_push(e, 1);
      });
    }, "actor logical frame quota exceeded");
    check(a.stopped() && a.queued() == 0 && budget.reserved() == 320 &&
          ActorMemoryAggregateBudgetV1::process().reserved() == baseline + 320,
          "terminal owner did not release native credits");
    b.send(sibling, std::array<std::uint8_t,1>{42});
    check(b.dispatch_one(sibling, [&](auto& ctx, auto payload) {
      std::array<std::uint8_t,1> out{};
      ctx.heap_read(*siblingHeap, 0, out);
      check(payload.size() == 1 && payload[0] == 42 && out[0] == 42,
            "sibling lost state after peer retirement");
    }), "sibling did not dispatch after peer retirement");
    const auto retired = live;
    live = a.restart();
    check(live.generation == retired.generation + 1, "generation not advanced");
    denied<std::logic_error>([&] { a.send(retired, std::array<std::uint8_t,1>{1}); },
                             "stale or stopped actor destination");
    denied<std::logic_error>([&] { a.send(first, std::array<std::uint8_t,1>{1}); },
                             "stale or stopped actor destination");
  }
  a.stop();
  b.stop();
  check(budget.reserved() == 0 &&
        ActorMemoryAggregateBudgetV1::process().reserved() == baseline,
        "final owner budget leak");
  std::cout << "PASS OWNER_RESTART_CYCLES=100000 SIBLING_STATE=42 BUDGET_LEAK=0\n";
}
