#include "runtime/actor.hpp"
#include <memory>
#include <limits>
#include <stdexcept>
#include <utility>
namespace oresforge::runtime {
Mailbox::Mailbox(std::uint64_t id,std::size_t capacity,std::size_t bytes):
    id_(id),capacity_(capacity),byte_capacity_(bytes) {
  if (id==0 || capacity==0 || bytes==0) throw std::invalid_argument("invalid mailbox");
}
ActorRef Mailbox::ref() const {
  std::scoped_lock lock(mutex_);
  return ActorRef(id_,generation_,authority_);
}
void Mailbox::send(ActorRef destination,std::span<const std::uint8_t> payload) {
  if (payload.size()>kMaxMessageBytes) throw std::length_error("message size limit");
  std::scoped_lock lock(mutex_);
  if (closed_ || destination.id!=id_ || destination.generation!=generation_ ||
      destination.authority_!=authority_)
    throw std::runtime_error("closed or stale actor");
  if (queue_.size()>=capacity_ || payload.size()>byte_capacity_-queued_bytes_)
    throw std::length_error("mailbox capacity");
  queue_.emplace_back(payload.begin(),payload.end());
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
// Erase undelivered owned message bytes, never sender or delivered buffers.
void Mailbox::discard_pending() {
  for (auto& pending : queue_) {
    volatile std::uint8_t* bytes=pending.data();
    for (std::size_t i=0; i<pending.size(); ++i) bytes[i]=0;
  }
  queue_.clear();
}
void Mailbox::close() {
  std::scoped_lock lock(mutex_);
  closed_=true;
  discard_pending();
  queued_bytes_=0;
}
ActorRef Mailbox::restart() {
  std::scoped_lock lock(mutex_);
  if (generation_==std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("generation exhausted");
  // Allocation failure cannot partially revoke or restart the mailbox.
  auto fresh_authority=std::make_shared<const std::uint8_t>(0);
  ++generation_;
  authority_=std::move(fresh_authority);
  closed_=false;
  discard_pending();
  queued_bytes_=0;
  return ActorRef(id_,generation_,authority_);
}
std::size_t Mailbox::queued() const {
  std::scoped_lock lock(mutex_);
  return queue_.size();
}
}
