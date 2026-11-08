#include "runtime/AmortizedCooperationGate.hpp"
#include <cstdint>
#include <limits>
#include <stdexcept>

using Gate = oresforge::runtime::AmortizedCooperationGate;
static_assert(Gate::saturating_add(std::numeric_limits<std::uint64_t>::max()-127,128)
              == std::numeric_limits<std::uint64_t>::max());
static_assert(Gate::saturating_add(std::numeric_limits<std::uint64_t>::max(),128)
              == std::numeric_limits<std::uint64_t>::max());
static_assert(Gate::saturating_add(512,127)==639);

int main() {
  Gate gate;
  std::uint64_t polls = 0;
  for (int i = 1; i <= 1'000'000; ++i) {
    const bool due = gate.completed_backedge_needs_slow_poll();
    if (due != (i % 128 == 0)) throw std::runtime_error("unexpected slow poll");
    if (gate.completed_backedges()!=static_cast<std::uint64_t>(i))
      throw std::runtime_error("incomplete window was not accounted for");
    if (due) ++polls;
    if (gate.fairness_expired() != (i >= 512))
      throw std::runtime_error("incorrect fairness cap");
  }
  if (polls != 7'812 || gate.completed_backedges() != 1'000'000)
    throw std::runtime_error("checkpoint cadence mismatch");
  Gate other;
  for (int i = 1; i <= 128; ++i) {
    if (other.completed_backedge_needs_slow_poll() != (i == 128))
      throw std::runtime_error("independent actor quantum crossed lanes");
  }
  if (other.completed_backedges()!=128 || gate.completed_backedges()!=1'000'000)
    throw std::runtime_error("independent actor quantum shared state");
  gate.begin_dispatch();
  if (gate.completed_backedges()!=0 || gate.fairness_expired())
    throw std::runtime_error("dispatch reset kept expired fairness");
  for (int i = 1; i <= 127; ++i) {
    if (gate.completed_backedge_needs_slow_poll())
      throw std::runtime_error("poll before first new quantum");
    if (gate.completed_backedges()!=static_cast<std::uint64_t>(i))
      throw std::runtime_error("partial count after reset");
  }
  if (!gate.completed_backedge_needs_slow_poll() || gate.completed_backedges()!=128)
    throw std::runtime_error("missed quantum after reset");
}
