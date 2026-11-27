#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace atto::forge::scalar {
struct ReusableSlot {
  enum class Kind { bind, read, detach };
  struct Step { Kind kind; std::size_t index = 0; };
  struct Term { bool literal; std::int64_t value; bool subtract = false; };
  std::vector<std::int64_t> owners;
  std::vector<Step> steps;
  std::vector<Term> result;
};

// Independent replay of closed scalar slot authority; indices are never addresses.
// A read publishes a Copy value into the scalar table. Detach never destroys an owner.
[[nodiscard]] inline std::optional<std::int64_t> verify_reusable_slot(const ReusableSlot& p) {
  if (p.owners.size() > 16 || p.steps.size() > 32 ||
      p.result.empty() || p.result.size() > 32) return std::nullopt;
  std::vector<std::int64_t> values = p.owners;
  for (auto v : values) if (v < 0) return std::nullopt;
  std::optional<std::size_t> bound;
  for (const auto& step : p.steps) {
    switch (step.kind) {
      case ReusableSlot::Kind::bind:
        if (bound || step.index >= p.owners.size()) return std::nullopt;
        bound = step.index;
        break;
      case ReusableSlot::Kind::read:
        if (!bound || step.index != *bound) return std::nullopt;
        values.push_back(p.owners[*bound]);
        break;
      case ReusableSlot::Kind::detach:
        if (!bound || step.index != 0) return std::nullopt;
        bound.reset();
        break;
      default: return std::nullopt;
    }
  }
  if (bound || p.result.front().subtract) return std::nullopt;
  std::int64_t answer = 0;
  for (const auto& t : p.result) {
    if (t.value < 0) return std::nullopt;
    if (!t.literal && static_cast<std::uint64_t>(t.value) >= values.size()) return std::nullopt;
    const auto value = t.literal ? t.value : values[static_cast<std::size_t>(t.value)];
    std::int64_t next = 0;
    if (t.subtract ? __builtin_sub_overflow(answer, value, &next)
                   : __builtin_add_overflow(answer, value, &next)) return std::nullopt;
    answer = next;
  }
  return answer;
}
}  // namespace atto::forge::scalar
