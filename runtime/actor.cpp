#include "runtime/actor.hpp"
#include <limits>
#include <stdexcept>
#include <utility>
namespace oresforge::runtime {
namespace {
// The identity need not be a secret: it prevents accidental or constructed
// actor-ID/generation aliasing inside this native process. This is NOT
// cryptographic remote authorization; no local ActorRef is wire-serializable.
std::uint64_t next_mailbox_identity() {
  static std::mutex gate;
  static std::uint64_t next = 0;
  std::scoped_lock lock(gate);
  if (next == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("mailbox identity exhausted");
  return ++next;
}
}
Mailbox::Mailbox(std::uint64_t id,std::size_t capacity,std::size_t bytes):
    id_(id),capacity_(capacity),byte_capacity_(bytes),
    mailbox_identity_(next_mailbox_identity()) {
  if (id==0 || capacity==0 || bytes==0) throw std::invalid_argument("invalid mailbox");
}
ActorRef Mailbox::ref() const {
  std::scoped_lock lock(mutex_);
  return ActorRef(id_,generation_,mailbox_identity_);
}
void Mailbox::send(ActorRef destination,std::span<const std::uint8_t> payload) {
  if (payload.size()>kMaxMessageBytes) throw std::length_error("message size limit");
  std::scoped_lock lock(mutex_);
  if (closed_ || destination.id!=id_ || destination.generation!=generation_ ||
      destination.mailbox_identity_!=mailbox_identity_)
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
void Mailbox::close() {
  std::scoped_lock lock(mutex_);
  closed_=true;
  discardPending();
  queued_bytes_=0;
}
ActorRef Mailbox::restart() {
  std::scoped_lock lock(mutex_);
  if (generation_==std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("generation exhausted");
  ++generation_;
  closed_=false;
  discardPending();
  queued_bytes_=0;
  return ActorRef(id_,generation_,mailbox_identity_);
}
// A best-effort volatile scrub of undelivered owned mailbox buffers. Delivered
// messages are owned by receivers; sender inputs were snapshotted on send.
void Mailbox::discardPending() noexcept {
  for (auto& message : queue_) {
    volatile std::uint8_t* p = message.data();
    for (std::size_t i = 0; i < message.size(); ++i) p[i] = 0;
  }
  queue_.clear();
}
std::size_t Mailbox::queued() const {
  std::scoped_lock lock(mutex_);
  return queue_.size();
}
}
