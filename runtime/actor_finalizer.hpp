#pragma once
#include "runtime/actor.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace oresforge::runtime {

// Trusted host supervisor adapter, not guest FFI or a production scheduler.
// Accepted requests keep their Mailbox alive until a supervisor drains them.
// A scheduler MUST drive run_one() on a thread that owns no lease on the
// target actor; it MUST not drop or destroy this queue while tickets remain.
// This bounded host adapter is NOT a hard real-time progress guarantee.
template<std::size_t Capacity = 64>
class ActorFinalizationQueue final {
  static_assert(Capacity > 0 && Capacity <= kMaxActorExecutionLeases);
 public:
  enum class Admission { Queued, AlreadyQueued, Full, Invalid };

  ActorFinalizationQueue() = default;
  ActorFinalizationQueue(const ActorFinalizationQueue&) = delete;
  ActorFinalizationQueue& operator=(const ActorFinalizationQueue&) = delete;

  ~ActorFinalizationQueue() noexcept {
    // Silently discarding pending cleanup permanently leaks owner observers.
    // The supervisor has an explicit obligation to drain before destruction.
    if (pending() != 0) std::terminate();
  }

  Admission try_request_close(const std::shared_ptr<Mailbox>& actor) {
    if (!actor) return Admission::Invalid;
    std::scoped_lock lock(mutex_);
    const auto expected = actor->ref().generation;
    for (const auto& ticket : tickets_) {
      if (ticket.actor == actor && ticket.generation == expected)
        return Admission::AlreadyQueued;
    }
    for (auto& ticket : tickets_) {
      if (ticket.actor) continue;
      // No guest execution can race between request_close and capturing the
      // generation: Mailbox::request_close fences under its admission mutex.
      // The queue lock prevents the consumer from seeing an incomplete ticket.
      const auto generation = actor->request_close();
      ticket.actor = actor;  // shared_ptr copy: no allocations or exceptions.
      ticket.generation = generation;
      ticket.processing = false;
      return Admission::Queued;
    }
    // Capacity refusal MUST NOT close an actor without a retained ticket.
    return Admission::Full;
  }

  // Exactly one queued ticket is claimed. On host exception the ticket stays
  // pending for the supervisor to retry on another safe thread.
  // Returns false when no *ready* ticket exists (another worker may be busy).
  bool run_one() {
    std::shared_ptr<Mailbox> actor;
    std::uint64_t generation = 0;
    std::size_t index = Capacity;
    {
      std::scoped_lock lock(mutex_);
      for (std::size_t i=0; i<Capacity; ++i) {
        if (!tickets_[i].actor || tickets_[i].processing) continue;
        tickets_[i].processing = true;
        actor = tickets_[i].actor;
        generation = tickets_[i].generation;
        index = i;
        break;
      }
    }
    if (!actor) return false;
    bool finalized = false;
    try {
      finalized = actor->finalize_stop(generation);
    } catch (...) {
      std::scoped_lock lock(mutex_);
      tickets_[index].processing = false;
      throw;
    }
    {
      std::scoped_lock lock(mutex_);
      if (tickets_[index].actor != actor ||
          tickets_[index].generation != generation ||
          !tickets_[index].processing)
        std::terminate(); // Internal supervisor ticket corruption.
      tickets_[index] = {};
      if (finalized) ++completed_;
      else ++stale_;
    }
    return true;
  }

  std::size_t drain_ready() {
    std::size_t count = 0;
    while (run_one()) ++count;
    return count;
  }

  std::size_t pending() const {
    std::scoped_lock lock(mutex_);
    std::size_t result = 0;
    for (const auto& ticket : tickets_) if (ticket.actor) ++result;
    return result;
  }
  std::uint64_t completed() const {
    std::scoped_lock lock(mutex_);
    return completed_;
  }
  std::uint64_t stale() const {
    std::scoped_lock lock(mutex_);
    return stale_;
  }

 private:
  struct Ticket {
    std::shared_ptr<Mailbox> actor;
    std::uint64_t generation = 0;
    bool processing = false;
  };
  mutable std::mutex mutex_;
  std::array<Ticket,Capacity> tickets_{};
  std::uint64_t completed_ = 0;
  std::uint64_t stale_ = 0;
};

} // namespace oresforge::runtime
