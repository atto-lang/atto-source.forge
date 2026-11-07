#include "runtime/actor.hpp"
#include <limits>
#include <stdexcept>
#include <utility>
#include <memory>
#include <deque>
#include <vector>
namespace oresforge::runtime {
struct CapabilityToken final {}; // unique per mailbox incarnation
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
Mailbox::~Mailbox() noexcept { discard_queued(queue_); }
Mailbox::Mailbox(std::uint64_t id,std::size_t capacity,std::size_t bytes,
                 TenantIsolationMode isolation,bool untrusted):
    id_(id),capacity_(capacity),byte_capacity_(bytes) {
  // Check requested physical/security boundary before creating any actor
  // authority. The stage-0 Forge mailbox is trusted-in-process only.
  require_tenant_isolation(isolation, untrusted);
  // Reject invalid admission budgets before allocating a capability.
  if (id==0 || capacity==0 || capacity>kMaxMailboxMessages ||
      bytes==0 || bytes>kMaxMailboxBytes)
    throw std::invalid_argument("invalid or unbounded mailbox capacity");
  authority_=std::make_shared<CapabilityToken>();
}
ActorRef Mailbox::ref() const {
  std::scoped_lock lock(mutex_);
  return ActorRef{id_,generation_,authority_};
}
bool Mailbox::authorizes(const ActorRef& owner) const {
  std::scoped_lock lock(mutex_);
  return !closed_ && owner.id==id_ && owner.generation==generation_ &&
         owner.authority_==authority_;
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
  discard_queued(queue_);
  queued_bytes_=0;
}
ActorRef Mailbox::restart() {
  std::scoped_lock lock(mutex_);
  if (generation_==std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("generation exhausted");
  // Allocate first: allocation failure leaves the old incarnation untouched.
  auto fresh=std::make_shared<CapabilityToken>();
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
