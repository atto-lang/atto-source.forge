// Host runtime test of actual ActorIncarnationOwnerV1 and Mailbox gate.
// Not compiled AttoLang guest, hierarchical supervisor policy or AOT evidence.
#include "runtime/ActorIncarnationOwnerV1.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace oresforge::runtime;
static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
template<typename Fn> static void require_denied(Fn&& fn) {
  bool denied = false;
  try { fn(); } catch (const std::logic_error&) { denied = true; }
  require(denied, "stale/fenced actor destination accepted DATA");
}

int main() {
  unsigned total_attempts = 0;
  unsigned total_commits = 0;
  for (unsigned trial = 0; trial < 96; ++trial) {
    ActorMemoryAggregateBudgetV1 budget(1024);
    ActorIncarnationOwnerV1 actor(991000 + trial * 2, budget, 256, 64, 4, 2);
    ActorIncarnationOwnerV1 sibling(991001 + trial * 2, budget, 256, 64, 4, 2);
    const auto old = actor.ref();
    const auto healthy = sibling.ref();
    std::atomic<unsigned> barrier{0};
    std::atomic<bool> stop_returned{false};
    unsigned committed = 0, denied = 0;
    std::exception_ptr error;
    std::thread producer([&] {
      try {
        barrier.fetch_add(1, std::memory_order_acq_rel);
        while (barrier.load(std::memory_order_acquire) != 2)
          std::this_thread::yield();
        for (unsigned i = 0; i < 64; ++i) {
          const bool already_stopped = stop_returned.load(std::memory_order_acquire);
          try {
            actor.send(old, std::array<std::uint8_t,1>{static_cast<std::uint8_t>(i)});
            if (already_stopped)
              throw std::runtime_error("DATA admitted after STOP returned");
            ++committed;
          } catch (const std::logic_error&) { ++denied; }
        }
      } catch (...) { error = std::current_exception(); }
    });
    std::thread stopper([&] {
      barrier.fetch_add(1, std::memory_order_acq_rel);
      while (barrier.load(std::memory_order_acquire) != 2)
        std::this_thread::yield();
      actor.stop();
      stop_returned.store(true, std::memory_order_release);
    });
    producer.join();
    stopper.join();
    if (error) std::rethrow_exception(error);
    require(committed + denied == 64, "unsettled DATA send attempt");
    require(actor.stopped(), "STOP did not seal actor incarnation");
    require(actor.queued() == 0, "retired mailbox retained queued DATA");
    require_denied([&] {
      actor.send(old, std::array<std::uint8_t,1>{1});
    });
    require(budget.reserved() == 320,
            "native actor heap/frame not physically released after STOP");
    sibling.send(healthy, std::array<std::uint8_t,1>{42});
    require(sibling.dispatch_one(healthy, [](auto&, auto payload) {
      require(payload.size() == 1 && payload[0] == 42,
              "unrelated sibling DATA corrupted by STOP");
    }), "healthy sibling was not scheduled");
    const auto next = actor.restart();
    require(next.generation > old.generation, "restart generation reused");
    require_denied([&] {
      actor.send(old, std::array<std::uint8_t,1>{2});
    });
    actor.send(next, std::array<std::uint8_t,1>{7});
    require(actor.dispatch_one(next, [](auto&, auto payload) {
      require(payload.size() == 1 && payload[0] == 7,
              "new generation did not receive DATA");
    }), "restarted actor could not process message");
    actor.stop();
    sibling.stop();
    require(budget.reserved() == 0, "physical native quota leaked after final STOP");
    total_attempts += 64;
    total_commits += committed;
  }
  require(total_attempts == 6144, "race coverage not completed");
  std::cout << "FORGE_SUPERVISOR_DATA_FENCE_HOST_PASS"
            << "|attempts=" << total_attempts
            << "|admitted=" << total_commits
            << "|sibling=1|native_guest=0|supervision_tree=0\n";
}
