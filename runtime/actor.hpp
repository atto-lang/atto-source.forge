#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <condition_variable>
#include <vector>
#include <utility>
namespace oresforge::runtime {
inline constexpr std::size_t kMaxMessageBytes=1024*1024;
inline constexpr std::size_t kMaxMailboxMessages=4096;
inline constexpr std::size_t kMaxMailboxBytes=16*kMaxMessageBytes;
inline constexpr std::size_t kMaxActorOwnedResources=256;
inline constexpr std::size_t kMaxActorExecutionLeases=4096;
// Actor-owned host resources register a weak lifecycle observer. The actor
// authority never owns the resource, so actor/resource teardown cannot cycle.
class ActorLifetimeResource {
 public:
  virtual ~ActorLifetimeResource() = default;
  virtual void actor_owner_stopped() noexcept = 0;
};

// Opaque, local-only authority; never serialize or expose it through guest FFI.
struct CapabilityToken final {
  // Internal stop completion can be triggered by a const lease authority.
  mutable std::atomic<bool> active{true};
  // Admission stops immediately, while active leased callers may drain.
  std::atomic<bool> accepting{true};
  bool attach_owned_resource(const std::shared_ptr<ActorLifetimeResource>& resource) const noexcept;
  // Admission-only transition: never runs user resource callbacks or waits
  // for active leases. Supervisor must later call invalidate() to retire.
  void request_stop() noexcept;
  // Blocking external/supervisor completion: wait for quiescence, then retire
  // resources exactly once. Must not be called from a current lease/callback.
  // The unique cleanup winner returns true. Concurrent supervisor callers
  // wait until retirement completes but do not claim a second completion.
  bool invalidate() noexcept;
  // True only after the supervisor has finished resource observer callbacks.
  bool retirement_complete() const noexcept;
  bool has_current_thread_lease() const noexcept;
  bool is_retiring_on_current_thread() const noexcept;
 private:
  bool acquire_lease() const noexcept;
  void release_lease() const noexcept;
  void retire_resources() const noexcept;
  mutable std::mutex execution_mutex_;
  mutable std::condition_variable execution_cv_;
  mutable std::size_t executing_ = 0;
  mutable bool stopping_ = false;
  mutable bool cleanup_started_ = false;
  mutable bool cleanup_complete_ = false;
  friend class ActorExecutionLease;
  friend class Mailbox;
  mutable std::mutex resource_mutex_;
  mutable std::array<std::weak_ptr<ActorLifetimeResource>,
                     kMaxActorOwnedResources> resources_{};
};
class Mailbox;
class ActorContext {
 public:
  ActorContext() = default; // Empty contexts are invalid and carry no authority.
  std::uint64_t domain() const noexcept { return domain_; }
  std::uint64_t actor() const noexcept { return actor_; }
  std::uint64_t incarnation() const noexcept { return incarnation_; }
  // Admission state is independent of same-thread outstanding leases:
  // delegated readers must stop once the table owner's stop is requested.
  bool accepting() const noexcept {
    return authority_ && authority_->active.load(std::memory_order_acquire) &&
           authority_->accepting.load(std::memory_order_acquire);
  }
  bool valid() const noexcept {
    return domain_ != 0 && actor_ != 0 && incarnation_ != 0 && authority_ &&
           authority_->active.load(std::memory_order_acquire) &&
           (authority_->accepting.load(std::memory_order_acquire) ||
            authority_->has_current_thread_lease());
  }
  bool operator==(const ActorContext& other) const noexcept {
    return domain_ == other.domain_ && actor_ == other.actor_ &&
           incarnation_ == other.incarnation_ && authority_ == other.authority_;
  }
  // Trusted host-runtime hook. This is not a guest FFI surface.
  bool attach_owned_resource(
      const std::shared_ptr<ActorLifetimeResource>& resource) const noexcept;
 private:
  std::uint64_t domain_ = 0;
  std::uint64_t actor_ = 0;
  std::uint64_t incarnation_ = 0;
  std::shared_ptr<const CapabilityToken> authority_;
  ActorContext(std::uint64_t domain, std::uint64_t actor,
               std::uint64_t incarnation,
               std::shared_ptr<const CapabilityToken> authority)
      : domain_(domain), actor_(actor), incarnation_(incarnation),
        authority_(std::move(authority)) {}
  friend class Mailbox;
};
// Trusted mailbox-issued stage-0 admission guard (not yet the production
// scheduler execution lease). Guest source cannot construct it or pass it
// through wire/FFI. Thread-affine; never move a lease to another OS thread.
// Never hold leases across await/select or callbacks that require a join.
class ActorExecutionLease final {
 public:
  ActorExecutionLease() = delete;
  ActorExecutionLease(const ActorExecutionLease&) = delete;
  ActorExecutionLease& operator=(const ActorExecutionLease&) = delete;
  ActorExecutionLease(ActorExecutionLease&&) noexcept = default;
  ActorExecutionLease& operator=(ActorExecutionLease&&) = delete;
  ~ActorExecutionLease() noexcept;
  const ActorContext& context() const {
    if (!authority_ || owner_thread_ != std::this_thread::get_id() ||
        !context_.valid())
      throw std::runtime_error("actor execution lease is inactive or on wrong thread");
    return context_;
  }
 private:
  const ActorContext context_;
  std::shared_ptr<const CapabilityToken> authority_;
  std::thread::id owner_thread_;
  ActorExecutionLease(ActorContext context,
                      std::shared_ptr<const CapabilityToken> authority);
  friend class Mailbox;
};
struct ActorRef {
  std::uint64_t id;
  std::uint64_t generation;
 private:
  std::shared_ptr<const CapabilityToken> authority_;
  ActorRef(std::uint64_t id, std::uint64_t generation,
           std::shared_ptr<const CapabilityToken> authority)
      : id(id), generation(generation), authority_(std::move(authority)) {}
  friend class Mailbox;
};
class Mailbox {
 public:
  Mailbox(std::uint64_t id,std::size_t capacity=1024,
          std::size_t byte_capacity=4*kMaxMessageBytes,
          std::uint64_t domain=1);
  // No concurrent calls may outlive mailbox destruction.
  ~Mailbox() noexcept;
  ActorRef ref() const;
  // Trusted scheduler/actor-runtime adapter. Guest code must never be allowed
  // to manufacture ActorContext values or pass them through FFI.
  ActorContext context() const;
  // Trusted scheduler admission. An acquired lease pins actor validity until
  // it is released; close/restart then fence the actor and retire its tables.
  ActorExecutionLease execution_lease() const;
  void send(ActorRef,std::span<const std::uint8_t>);
  std::optional<std::vector<std::uint8_t>> receive();
  // Stop admission without executing resource callbacks; bounded mailbox
  // erasure still happens here. Supervisor must call close() or restart()
  // after active leases drain to finalize owned-resource retirement.
  // Returns the incarnation fenced by the stop request. Trusted host-only
  // return value enables generation-pinned supervisor finalization tickets.
  std::uint64_t request_close();
  // Generation-checked supervisor admission. A stale snapshot MUST NOT stop
  // a replacement incarnation; return false without any lifecycle effects.
  bool request_close_if_generation(std::uint64_t expected_generation);
  // Complete a queued stop only if its original generation is still current.
  // Returns false for a stale ticket; does not close a restarted actor.
  bool finalize_stop(std::uint64_t incarnation);
  // Generation-scoped deduplication against tickets whose cleanup already
  // finished. Host-only query, never exposed as guest actor authority.
  bool stop_finalized(std::uint64_t incarnation) const noexcept;
  // Synchronous external/supervisor shutdown, forbidden while this thread
  // holds an execution lease for the same actor.
  void close();
  // Restart is supervisor-only after quiescence; inside a lease it fails fast.
  ActorRef restart();
  std::size_t queued() const;
 private:
  const std::uint64_t id_;
  const std::uint64_t domain_;
  const std::size_t capacity_,byte_capacity_;
  mutable std::mutex mutex_;
  std::uint64_t generation_=1;
  std::shared_ptr<CapabilityToken> authority_;
  std::size_t queued_bytes_=0;
  bool closed_=false;
  // A restart waits outside the mailbox mutex. Stop requests arriving during
  // that window abort the restart instead of re-enabling a stopped actor.
  bool restarting_=false;
  bool stop_during_restart_=false;
  std::deque<std::vector<std::uint8_t>> queue_;
};
}
