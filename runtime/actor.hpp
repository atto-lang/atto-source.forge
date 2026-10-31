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
#include <shared_mutex>
#include <vector>
#include <utility>
namespace oresforge::runtime {
inline constexpr std::size_t kMaxMessageBytes=1024*1024;
inline constexpr std::size_t kMaxMailboxMessages=4096;
inline constexpr std::size_t kMaxMailboxBytes=16*kMaxMessageBytes;
inline constexpr std::size_t kMaxActorOwnedResources=256;
// Actor-owned host resources register a weak lifecycle observer. The actor
// authority never owns the resource, so actor/resource teardown cannot cycle.
class ActorLifetimeResource {
 public:
  virtual ~ActorLifetimeResource() = default;
  virtual void actor_owner_stopped() noexcept = 0;
};

// Opaque, local-only authority; never serialize or expose it through guest FFI.
struct CapabilityToken final {
  std::atomic<bool> active{true};
  bool attach_owned_resource(const std::shared_ptr<ActorLifetimeResource>& resource) const noexcept;
  void invalidate() noexcept;
 private:
  // An execution lease pins this incarnation until its current operation ends.
  // Invalidate takes the unique lock before fencing the authority and before
  // notifying actor-owned resources; stage0 dispatch takes the shared lock.
  mutable std::shared_mutex execution_mutex_;
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
  bool valid() const noexcept {
    return domain_ != 0 && actor_ != 0 && incarnation_ != 0 && authority_ &&
           authority_->active.load(std::memory_order_acquire);
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
// Trusted scheduler-issued admission guard. It cannot be created or
// reconstructed from guest operands and is never a wire/FFI capability.
// Keep leases short: do not hold them across await, select or guest callbacks.
class ActorExecutionLease final {
 public:
  ActorExecutionLease() = delete;
  ActorExecutionLease(const ActorExecutionLease&) = delete;
  ActorExecutionLease& operator=(const ActorExecutionLease&) = delete;
  ActorExecutionLease(ActorExecutionLease&&) noexcept = default;
  ActorExecutionLease& operator=(ActorExecutionLease&&) = delete;
  const ActorContext& context() const {
    // A moved-from lease must not expose a caller context without the pin.
    // Only the live, lock-owning lease can authorize a stage0 guest call.
    if (!admission_.owns_lock() || !context_.valid())
      throw std::runtime_error("actor execution lease is no longer active");
    return context_;
  }
 private:
  const ActorContext context_;
  std::shared_ptr<const CapabilityToken> authority_;
  std::shared_lock<std::shared_mutex> admission_;
  ActorExecutionLease(ActorContext context, std::shared_ptr<const CapabilityToken> authority)
      : context_(std::move(context)), authority_(std::move(authority)),
        admission_(authority_->execution_mutex_) {}
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
  void close();
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
  std::deque<std::vector<std::uint8_t>> queue_;
};
}
