#include "jit/tiered.hpp"
#include <limits>
#include <stdexcept>
namespace oresforge::jit {
Tier Policy::request(std::uint64_t calls) const {
  if (calls>=optimize_after) return Tier::OptimizedRequested;
  if (calls>=baseline_after) return Tier::BaselineRequested;
  return Tier::Interpreter;
}
std::int64_t Executor::run(const Program& p) {
  if (calls_==std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("counter exhausted");
  std::int64_t result=interpret(p);
  ++calls_;
  return result; // No executable JIT exists yet.
}
}
