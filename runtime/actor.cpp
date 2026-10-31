#include "runtime/actor.hpp"
#include <limits>
#include <stdexcept>
#include <utility>
#include <memory>
#include <deque>
#include <vector>
namespace oresforge::runtime {
bool CapabilityToken::attach_owned_resource(
    const std::shared_ptr<ActorLifetimeResource>& resource) const noexcept {
  if (!resource) return false;
  try {
    std::scoped_lock lock(resource_mutex_);
    if (!active.load(std::memory_order_acquire)) return false;
    for (auto& slot : resources_) {
      if (slot.expired()) {
        slot = resource;
        return true;
      }
    }
    return false;
  } catch (...) {
    return false;
  }
}

void CapabilityToken::invalidate() noexcept {
  try {
    // A unique execution lock waits for admitted guest table calls to finish.
    // Once acquired, no new execution lease can be admitted on this token.
    std::unique_lock execution_guard(execution_mutex_);
    active.store(false, std::memory_order_release);
    std::scoped_lock resources_guard(resource_mutex_);
    for (auto& weak : resources_) {
      if (auto resource = weak.lock()) resource->actor_owner_stopped();
      weak.reset();
    }
  } catch (...) {
    // Platform mutex failure is fatal for lifecycle guarantees. Keep the
    // authority fenced even if retirement callbacks could not complete.
    active.store(false, std::memory_order_release);
  }
}

bool ActorContext::attach_owned_resource(
    const std::shared_ptr<ActorLifetimeResource>& resource) const noexcept {
  return domain_ != 0 && actor_ != 0 && incarnation_ != 0 && authority_ &&
         authority_->attach_owned_resource(resource);
}

namespace {
// Only pending mailbox-owned copies may be wiped. Already-delivered values
// belong to the receiver. Volatile byte stores prevent dead-store elimination
// when queue entries are released immediately afterward.
void discard_queued(std::deque<std::vector<std::uint8_t>>& queue) noexcept {
  for (auto& payload : queue) {
    volatile std::uint8_t* bytes = payload.data();
    for (std::size_t i = 0; i < payload.size(); ++i) bytes[i] = 0;
  }
  queue.clear();
}
} // namespace
// Clean pending mailbox-owned data on destruction, not just explicit close.
// As with any C++ object, destruction requires all users to have stopped.
Mailbox::~Mailbox() noexcept {
  if (authority_) authority_->invalidate();
  discard_queued(queue_);
}
Mailbox::Mailbox(std::uint64_t id,std::size_t capacity,std::size_t bytes,
                 std::uint64_t domain):
    id_(id),domain_(domain),capacity_(capacity),byte_capacity_(bytes) {
  // Reject invalid admission budgets before allocating a capability.
  if (id==0 || domain==0 || capacity==0 || capacity>kMaxMailboxMessages ||
      bytes==0 || bytes>kMaxMailboxBytes)
    throw std::invalid_argument("invalid or unbounded mailbox capacity");
  authority_=std::make_shared<CapabilityToken>();
}
ActorRef Mailbox::ref() const {
  std::scoped_lock lock(mutex_);
  return ActorRef{id_,generation_,authority_};
}
ActorContext Mailbox::context() const {
  std::scoped_lock lock(mutex_);
  if (closed_ || !authority_ ||
      !authority_->active.load(std::memory_order_acquire))
    throw std::runtime_error("closed actor has no execution context");
  return ActorContext{domain_,id_,generation_,authority_};
}
ActorExecutionLease Mailbox::execution_lease() const {
  // The mailbox mutex orders acquisition against close/restart, which also
  // hold this mutex while fencing the old actor authority.
  std::scoped_lock lock(mutex_);
  if (closed_ || !authority_ ||
      !authority_->active.load(std::memory_order_acquire))
    throw std::runtime_error("closed actor has no execution lease");
  ActorExecutionLease lease(ActorContext{domain_,id_,generation_,authority_},
                            authority_);
  if (!lease.context().valid())
    throw std::runtime_error("stale actor execution lease");
  return lease;
}
void Mailbox::send(ActorRef destination,std::span<const std::uint8_t> payload) {
  std::scoped_lock lock(mutex_);
  if (closed_ || destination.id!=id_ || destination.generation!=generation_ ||
      destination.authority_!=authority_)
    throw std::runtime_error("closed or stale actor");
  // Check capability before exposing receiver message limits.
  if (payload.size()>kMaxMessageBytes) throw std::length_error("message size limit");
  if (queue_.size()>=capacity_ || queued_bytes_>byte_capacity_ ||
      payload.size()>byte_capacity_-queued_bytes_)
    throw std::length_error("mailbox capacity");
  // A default empty span may have null iterators. It still uses one slot.
  if (payload.empty()) queue_.emplace_back();
  else queue_.emplace_back(payload.begin(),payload.end());
  queued_bytes_+=payload.size();
}
std::optional<std::vector<std::uint8_t>> Mailbox::receive() {
  std::scoped_lock lock(mutex_);
  if (queue_.empty()) return std::nullopt;
  auto value=std::move(queue_.front());
  queued_bytes_-=value.size();
  queue_.pop_front();
  return value;
}
void Mailbox::close() {
  std::scoped_lock lock(mutex_);
  closed_=true;
  authority_->invalidate();
  discard_queued(queue_);
  queued_bytes_=0;
}
ActorRef Mailbox::restart() {
  std::scoped_lock lock(mutex_);
  if (generation_==std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("generation exhausted");
  // Allocate first: allocation failure leaves the old incarnation untouched.
  auto fresh=std::make_shared<CapabilityToken>();
  authority_->invalidate();
  ++generation_;
  authority_=std::move(fresh);
  closed_=false;
  discard_queued(queue_);
  queued_bytes_=0;
  return ActorRef{id_,generation_,authority_};
}
std::size_t Mailbox::queued() const {
  std::scoped_lock lock(mutex_);
  return queue_.size();
}
}
