#include "runtime/AmortizedCooperationGate.hpp"
#include <stdexcept>

int main() {
  oresforge::runtime::AmortizedCooperationGate gate;
  int polls = 0;
  for (int i = 1; i <= 1024; ++i) {
    const bool due = gate.completed_backedge_needs_slow_poll();
    if (due != (i % 128 == 0)) throw std::runtime_error("unexpected slow poll");
    if (due) ++polls;
    if (gate.fairness_expired() != (i >= 512))
      throw std::runtime_error("incorrect fairness cap");
  }
  if (polls != 8 || gate.completed_backedges() != 1024)
    throw std::runtime_error("checkpoint cadence mismatch");
  gate.begin_dispatch();
  for (int i = 1; i <= 127; ++i)
    if (gate.completed_backedge_needs_slow_poll())
      throw std::runtime_error("poll before first new quantum");
  if (!gate.completed_backedge_needs_slow_poll() || gate.completed_backedges() != 128)
    throw std::runtime_error("missed quantum after reset");
}
