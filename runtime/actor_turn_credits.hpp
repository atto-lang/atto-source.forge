#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace oresforge::runtime {
/** Host-only, preallocated physical-turn credit proof.
 * A credit does not authorize a concurrently mutable source actor or create
 * a scheduler/reactor. Reactor and guest I/O support remain unsupported.
 * No GC, heap allocation, mutex, or wait on admission/release paths.
 */
class ActorPhysicalTurnCredits final {
 public:
  static constexpr std::size_t default_maximum = 1;
  static constexpr std::size_t supported_maximum = 3;
  struct Claim { std::uint8_t slot; std::uint64_t ticket; };

  explicit ActorPhysicalTurnCredits(std::size_t maximum = default_maximum)
      : maximum_(maximum) {
    if (maximum < 1 || maximum > supported_maximum)
      throw std::invalid_argument("actor physical turns must be 1..3");
    for (auto& ticket : tickets_)
      ticket.store(0, std::memory_order_relaxed);
  }
  ActorPhysicalTurnCredits(const ActorPhysicalTurnCredits&) = delete;
  ActorPhysicalTurnCredits& operator=(const ActorPhysicalTurnCredits&) = delete;

  std::size_t maximum() const noexcept { return maximum_; }
  bool retired() const noexcept { return retired_.load(std::memory_order_acquire); }
  std::size_t active() const noexcept {
    std::size_t count = 0;
    for (std::size_t i = 0; i < maximum_; ++i)
      count += (tickets_[i].load(std::memory_order_acquire) & 1u) != 0;
    return count;
  }
  std::optional<Claim> try_acquire() noexcept {
    if (retired()) return std::nullopt;
    for (std::size_t i = 0; i < maximum_; ++i) {
      auto prior = tickets_[i].load(std::memory_order_acquire);
      if ((prior & 1u) || prior >= std::numeric_limits<std::uint64_t>::max() - 1)
        continue;
      if (!tickets_[i].compare_exchange_strong(
              prior, prior + 1, std::memory_order_acq_rel,
              std::memory_order_relaxed)) continue;
      Claim claim{static_cast<std::uint8_t>(i), prior + 1};
      if (retired()) {
        (void)release(claim);
        return std::nullopt;
      }
      return claim;
    }
    return std::nullopt;
  }
  bool release(Claim claim) noexcept {
    if (claim.slot >= maximum_ || !(claim.ticket & 1u) ||
        claim.ticket == std::numeric_limits<std::uint64_t>::max()) return false;
    auto expected = claim.ticket;
    return tickets_[claim.slot].compare_exchange_strong(
        expected, claim.ticket + 1, std::memory_order_acq_rel,
        std::memory_order_relaxed);
  }
  void retire() noexcept { retired_.store(true, std::memory_order_release); }

 private:
  const std::size_t maximum_;
  std::array<std::atomic<std::uint64_t>, supported_maximum> tickets_{};
  std::atomic<bool> retired_{false};
};
} // namespace oresforge::runtime
