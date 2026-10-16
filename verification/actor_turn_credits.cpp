#include "runtime/actor_turn_credits.hpp"
#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>

using oresforge::runtime::ActorPhysicalTurnCredits;
static void require(bool result) { if (!result) std::abort(); }

int main() {
  {
    ActorPhysicalTurnCredits gate;
    require(gate.maximum() == 1);
    auto first = gate.try_acquire();
    require(first.has_value());
    require(!gate.try_acquire().has_value());
    require(gate.active() == 1);
    require(gate.release(*first));
    require(!gate.release(*first));
    require(gate.active() == 0);
  }
  {
    ActorPhysicalTurnCredits gate(3);
    auto a = gate.try_acquire(), b = gate.try_acquire(), c = gate.try_acquire();
    require(a && b && c && gate.active() == 3 && !gate.try_acquire());
    require(gate.release(*b) && !gate.release(*b));
    auto d = gate.try_acquire();
    require(d && gate.active() == 3);
    require(!gate.release(*b)); // stale cannot release reused slot
    gate.retire();
    require(gate.retired() && !gate.try_acquire());
    require(gate.release(*a) && gate.release(*c) && gate.release(*d));
    require(gate.active() == 0 && gate.retired());
  }
  for (auto invalid : {std::size_t{0}, std::size_t{4}, std::size_t{64}}) {
    bool rejected = false;
    try { ActorPhysicalTurnCredits gate(invalid); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected);
  }
  for (int round = 0; round < 100; ++round) {
    ActorPhysicalTurnCredits gate(3);
    std::atomic<int> ready{0}, attempted{0}, peak{0};
    std::atomic<bool> launch{false}, release{false};
    std::vector<std::thread> workers;
    for (int i = 0; i < 12; ++i) {
      workers.emplace_back([&]() {
        ready.fetch_add(1, std::memory_order_release);
        while (!launch.load(std::memory_order_acquire)) std::this_thread::yield();
        const auto credit = gate.try_acquire();
        attempted.fetch_add(1, std::memory_order_release);
        if (!credit) return;
        const int observed = static_cast<int>(gate.active());
        int before = peak.load(std::memory_order_relaxed);
        while (before < observed && !peak.compare_exchange_weak(
               before, observed, std::memory_order_relaxed)) {}
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        require(gate.release(*credit));
      });
    }
    while (ready.load(std::memory_order_acquire) != 12) std::this_thread::yield();
    launch.store(true, std::memory_order_release);
    while (attempted.load(std::memory_order_acquire) != 12) std::this_thread::yield();
    require(gate.active() <= 3 && peak.load(std::memory_order_relaxed) <= 3);
    release.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    require(gate.active() == 0);
  }
  return 0;
}
