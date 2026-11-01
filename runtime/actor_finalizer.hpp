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
enum class ActorStopCause : std::uint8_t {
  Requested = 0, Cancelled = 1, UnhandledThrow = 2,
  Raised = 3, Panicked = 4
};

template<std::size_t Capacity = 64>
class ActorFinalizationQueue final {
  static_assert(Capacity > 0 && Capacity <= kMaxActorExecutionLeases);
 public:
  enum class Admission { Queued, AlreadyQueued, AlreadyFinalized, Full, Invalid };
  struct Completion {
    std::uint64_t actor_id = 0;
    std::uint64_t incarnation = 0;
    ActorStopCause cause = ActorStopCause::Requested;
    bool finalized = false; // This runner performed resource retirement.
    bool deduplicated = false; // Another supervisor already retired this generation.
  };

  ActorFinalizationQueue() = default;
  ActorFinalizationQueue(const ActorFinalizationQueue&) = delete;
  ActorFinalizationQueue& operator=(const ActorFinalizationQueue&) = delete;

  ~ActorFinalizationQueue() noexcept {
    // Silently discarding pending cleanup permanently leaks owner observers.
    // The supervisor has an explicit obligation to drain before destruction.
    if (pending() != 0) std::terminate();
  }

  Admission try_request_close(const std::shared_ptr<Mailbox>& actor,
                              ActorStopCause cause = ActorStopCause::Requested) {
    // Only canonical stop causes can be reported to supervisors. Reject
    // arbitrary casted enum values before reserving capacity or fencing
    // the actor, otherwise they could outrank panic in priority receipts.
    if (!actor || static_cast<unsigned>(cause) >
                      static_cast<unsigned>(ActorStopCause::Panicked))
      return Admission::Invalid;
    std::scoped_lock lock(mutex_);
    // Check duplicates first, including a ticket actively being finalized.
    // It retains the actor and preserves the highest-priority stop cause.
    const auto before = actor->ref();
    for (auto& ticket : tickets_) {
      if (ticket.actor != actor || ticket.generation != before.generation) continue;
      if (static_cast<unsigned>(cause) > static_cast<unsigned>(ticket.cause))
        ticket.cause = cause;
      return Admission::AlreadyQueued;
    }
    // A completed ticket has already been removed from this bounded queue.
    // Ask the generation-owned mailbox rather than retaining unbounded
    // tombstones: re-request cannot produce a duplicate completion receipt.
    if (actor->stop_finalized(before.generation))
      return Admission::AlreadyFinalized;
    Ticket* free_slot = nullptr;
    for (auto& ticket : tickets_)
      if (!ticket.actor && !free_slot) free_slot = &ticket;
    if (!free_slot) return Admission::Full;

    // A slot is reserved BEFORE the stop request. Capture/fence the current
    // generation under the mailbox mutex, then re-check generation races.
    const auto generation = actor->request_close();
    for (auto& ticket : tickets_) {
      if (ticket.actor != actor || ticket.generation != generation) continue;
      if (static_cast<unsigned>(cause) > static_cast<unsigned>(ticket.cause))
        ticket.cause = cause;
      return Admission::AlreadyQueued;
    }
    // Another trusted supervisor can finalize while our stop request runs.
    if (actor->stop_finalized(generation))
      return Admission::AlreadyFinalized;
    free_slot->actor = actor;
    free_slot->actor_id = before.id;
    free_slot->generation = generation;
    free_slot->cause = cause;
    free_slot->processing = false;
    return Admission::Queued;
  }

  // Exactly one queued ticket is claimed. On host exception the ticket stays
  // pending for the supervisor to retry on another safe thread.
  // Returns false when no *ready* ticket exists (another worker may be busy).
  bool run_one(Completion* completion = nullptr) {
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
    bool deduplicated = false;
    try {
      finalized = actor->finalize_stop(generation);
      // A different queue or supervisor can finish retirement first.
      // Distinguish that harmless duplicate from a stale incarnation.
      if (!finalized) deduplicated = actor->stop_finalized(generation);
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
      if (completion) {
        completion->actor_id = tickets_[index].actor_id;
        completion->incarnation = generation;
        completion->cause = tickets_[index].cause;
        completion->finalized = finalized;
        completion->deduplicated = deduplicated;
      }
      tickets_[index] = {};
      if (finalized) ++completed_;
      else if (deduplicated) ++duplicates_;
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
  std::uint64_t duplicates() const {
    std::scoped_lock lock(mutex_);
    return duplicates_;
  }

 private:
  struct Ticket {
    std::shared_ptr<Mailbox> actor;
    std::uint64_t actor_id = 0;
    std::uint64_t generation = 0;
    ActorStopCause cause = ActorStopCause::Requested;
    bool processing = false;
  };
  mutable std::mutex mutex_;
  std::array<Ticket,Capacity> tickets_{};
  std::uint64_t completed_ = 0;
  std::uint64_t stale_ = 0;
  std::uint64_t duplicates_ = 0;
};

} // namespace oresforge::runtime
