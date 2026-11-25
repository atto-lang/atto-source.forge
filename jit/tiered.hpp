#pragma once
#include "compiler/ir.hpp"
#include <cstdint>
namespace oresforge::jit {
enum class Tier { Interpreter, BaselineRequested, OptimizedRequested };
struct Policy {
  std::uint64_t baseline_after=10;
  std::uint64_t optimize_after=100;
  Tier request(std::uint64_t calls) const;
};
class Executor {
 public:
  explicit Executor(Policy policy={}): policy_(policy) {}
  std::int64_t run(const Program&);
  Tier requested_tier() const { return policy_.request(calls_); }
  Tier effective_tier() const { return Tier::Interpreter; }
  std::uint64_t call_count() const { return calls_; }
 private:
  Policy policy_;
  std::uint64_t calls_=0;
};
}
