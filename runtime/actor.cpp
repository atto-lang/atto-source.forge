#include "runtime/actor.hpp"
#include <limits>
#include <stdexcept>
#include <utility>
#include <memory>
#include <deque>
#include <algorithm>
#include <exception>
#include <iterator>
#include <vector>
namespace oresforge::runtime {
namespace {
// A lease must be released on its admitting thread. Nested leases (including
// different actors) are tracked individually; a move within the same thread
// transfers the live lease without adding another entry.
thread_local std::vector<const CapabilityToken*> active_execution_leases;
// Retirement callbacks run on the final releaser's thread. Guard synchronous
// lifecycle reentry without allocation or a potentially blocking lock.
struct RetirementFrame {
  const CapabilityToken* token;
  RetirementFrame* previous;
};
thread_local RetirementFrame* active_retirement = nullptr;
struct RetirementGuard {
  RetirementFrame frame;
  explicit RetirementGuard(const CapabilityToken* token) noexcept
      : frame{token,active_retirement} { active_retirement = &frame; }
  ~RetirementGuard() noexcept { active_retirement = frame.previous; }
};
}

bool CapabilityToken::attach_owned_resource(
    const std::shared_ptr<ActorLifetimeResource>& resource) const noexcept {
  if (!resource) return false;
  try {
    std::scoped_lock execution_guard(execution_mutex_);
    // No resource may be newly attached after shutdown was requested,
    // even while older leased operations are still draining.
    if (stopping_ || !active.load(std::memory_order_acquire)) return false;
    std::scoped_lock resource_guard(resource_mutex_);
    for (auto& slot : resources_) {
      if (slot.expired()) {
        slot = resource;
        return true;
      }
    }
  } catch (...) { return false; }
  return false;
}

bool CapabilityToken::acquire_lease() const noexcept {
  try {
    std::scoped_lock guard(execution_mutex_);
    if (stopping_ || !active.load(std::memory_order_acquire) ||
        executing_ >= kMaxActorExecutionLeases) return false;
    ++executing_;
    return true;
  } catch (...) { return false; }
}

void CapabilityToken::retire_resources() const noexcept {
  RetirementGuard guard(this);
  try {
    std::scoped_lock resources_guard(resource_mutex_);
    for (auto& weak : resources_) {
      if (auto resource = weak.lock()) resource->actor_owner_stopped();
      weak.reset();
    }
  } catch (...) {
    // Failure to deliver retirement must never be reported as completion.
    std::terminate();
  }
}

void CapabilityToken::release_lease() const noexcept {
  try {
    {
      std::scoped_lock guard(execution_mutex_);
      if (executing_ == 0) std::terminate();
      --executing_;
      if (stopping_ && executing_ == 0)
        active.store(false, std::memory_order_release);
    }
    // Only the supervisor's invalidate() may invoke resource callbacks.
    execution_cv_.notify_all();
  } catch (...) { std::terminate(); }
}

void CapabilityToken::request_stop() noexcept {
  try {
    std::scoped_lock guard(execution_mutex_);
    stopping_ = true;
    accepting.store(false, std::memory_order_release);
    if (executing_ == 0) active.store(false, std::memory_order_release);
    // No callback, join, allocation or resource mutex is touched here.
  } catch (...) { std::terminate(); }
}

void CapabilityToken::invalidate() noexcept {
  if (has_current_thread_lease() || is_retiring_on_current_thread())
    std::terminate(); // Self-join and retirement callback reentry forbidden.
  request_stop();
  bool retire = false;
  try {
    std::unique_lock lock(execution_mutex_);
    execution_cv_.wait(lock, [&] { return executing_ == 0; });
    if (!cleanup_started_) {
      cleanup_started_ = true;
      active.store(false, std::memory_order_release);
      retire = true;
    } else {
      execution_cv_.wait(lock, [&] { return cleanup_complete_; });
    }
  } catch (...) { std::terminate(); }
  if (retire) {
    retire_resources();
    try {
      {
        std::scoped_lock lock(execution_mutex_);
        cleanup_complete_ = true;
      }
      execution_cv_.notify_all();
    } catch (...) { std::terminate(); }
  }
}

bool CapabilityToken::retirement_complete() const noexcept {
  try {
    std::scoped_lock lock(execution_mutex_);
    return cleanup_complete_;
  } catch (...) { std::terminate(); }
}

bool CapabilityToken::has_current_thread_lease() const noexcept {
  return std::find(active_execution_leases.begin(), active_execution_leases.end(),
                   this) != active_execution_leases.end();
}
bool CapabilityToken::is_retiring_on_current_thread() const noexcept {
  for (auto* frame = active_retirement; frame; frame = frame->previous)
    if (frame->token == this) return true;
  return false;
}

ActorExecutionLease::ActorExecutionLease(
    ActorContext context, std::shared_ptr<const CapabilityToken> authority)
    : context_(std::move(context)), authority_(std::move(authority)),
      owner_thread_(std::this_thread::get_id()) {
  if (!authority_ || !authority_->acquire_lease())
    throw std::runtime_error("closed actor has no execution lease");
  try {
    active_execution_leases.push_back(authority_.get());
  } catch (...) {
    authority_->release_lease();
    throw;
  }
}
ActorExecutionLease::~ActorExecutionLease() noexcept {
  if (!authority_) return; // Moved-from object owns no lease.
  if (owner_thread_ != std::this_thread::get_id()) std::terminate();
  const auto found = std::find(active_execution_leases.rbegin(),
                               active_execution_leases.rend(), authority_.get());
  if (found == active_execution_leases.rend()) std::terminate();
  active_execution_leases.erase(std::next(found).base());
  authority_->release_lease();
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
std::uint64_t Mailbox::request_close() {
  std::scoped_lock lock(mutex_);
  closed_ = true;
  if (restarting_) stop_during_restart_ = true;
  // Fence the actor while holding mailbox admission, not after unlocking:
  // old contexts must not acquire authority between request and fence.
  authority_->request_stop();
  discard_queued(queue_);
  queued_bytes_ = 0;
  return generation_;
}

bool Mailbox::finalize_stop(std::uint64_t incarnation) {
  std::shared_ptr<CapabilityToken> old;
  {
    std::scoped_lock lock(mutex_);
    if (incarnation == 0 || generation_ != incarnation) return false;
    if (authority_->has_current_thread_lease() ||
        authority_->is_retiring_on_current_thread())
      throw std::runtime_error("supervisor finalization cannot self-join");
    closed_ = true;
    if (restarting_) stop_during_restart_ = true;
    old = authority_;
    old->request_stop();
    discard_queued(queue_);
    queued_bytes_ = 0;
  }
  // Quiescence and callbacks happen outside the mailbox lock.
  old->invalidate();
  return true;
}
bool Mailbox::stop_finalized(std::uint64_t incarnation) const noexcept {
  try {
    std::scoped_lock lock(mutex_);
    return incarnation != 0 && generation_ == incarnation &&
           authority_ && authority_->retirement_complete();
  } catch (...) { std::terminate(); }
}

void Mailbox::close() {
  std::shared_ptr<CapabilityToken> old;
  {
    std::scoped_lock lock(mutex_);
    // Synchronous shutdown cannot join the current actor's own lease.
    if (authority_->has_current_thread_lease() ||
        authority_->is_retiring_on_current_thread())
      throw std::runtime_error("actor self-close requires request_close");
    closed_ = true;
    if (restarting_) stop_during_restart_ = true;
    old = authority_;
    discard_queued(queue_);
    queued_bytes_ = 0;
  }
  // Other threads can still request_close() while this thread waits for
  // lease retirement. No mailbox mutex is held across this wait.
  old->invalidate();
}
ActorRef Mailbox::restart() {
  // Supervisor-only synchronous incarnation replacement. The transition
  // rejects new admissions before any potentially blocking wait.
  auto fresh = std::make_shared<CapabilityToken>();
  std::shared_ptr<CapabilityToken> old;
  {
    std::scoped_lock lock(mutex_);
    if (authority_->has_current_thread_lease() ||
        authority_->is_retiring_on_current_thread())
      throw std::runtime_error("actor restart requires quiescence");
    if (restarting_)
      throw std::runtime_error("actor restart already in progress");
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("generation exhausted");
    restarting_ = true;
    stop_during_restart_ = false;
    closed_ = true;
    old = authority_;
  }
  // Let leased operations reach the mailbox while the supervisor waits.
  old->invalidate();
  std::scoped_lock lock(mutex_);
  if (stop_during_restart_) {
    restarting_ = false;
    stop_during_restart_ = false;
    throw std::runtime_error("actor restart cancelled by shutdown request");
  }
  ++generation_;
  authority_ = std::move(fresh);
  closed_ = false;
  restarting_ = false;
  discard_queued(queue_);
  queued_bytes_ = 0;
  return ActorRef{id_,generation_,authority_};
}
std::size_t Mailbox::queued() const {
  std::scoped_lock lock(mutex_);
  return queue_.size();
}
}
