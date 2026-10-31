#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include "runtime/actor.hpp"

namespace oresforge::shared {
// Minted only by the actor runtime, NEVER decoded from guest arguments.
using ActorContext = oresforge::runtime::ActorContext;
enum class TableAccess { Private, Protected, Public };
enum class TableErrorCode { Denied, Closed, Capacity, Overflow, Missing };
class TableError : public std::exception {
 public:
  const TableErrorCode code;
  explicit TableError(TableErrorCode c) : code(c) {}
  const char* what() const noexcept override { return "shared i64 table operation failed"; }
};

// Fixed slot budget and exact i64 key equality. No allocation, guest callback
// or borrowed pointer in operations; one mutex gives single-operation atomicity.
// This is a host implementation slice, NOT a hard-RT lock/allocator guarantee.
template<std::size_t Capacity> class SharedI64Table final {
  static_assert(Capacity > 0);
  struct Slot { bool used = false; std::int64_t key = 0; std::int64_t value = 0; };
  struct OwnerObserver;
  struct State final {
    std::mutex mutex;
    ActorContext owner;
    std::uint64_t ownership_generation = 1;
    const TableAccess access;
    bool closed = false;
    std::array<Slot, Capacity> slots{};
    std::weak_ptr<OwnerObserver> active_observer;
    State(ActorContext o, TableAccess a) : owner(std::move(o)), access(a) {}
    void retire() noexcept {
      closed = true;
      for (auto& slot : slots) slot = {};
    }
  };
  // Each handoff registers a new lifetime observer. Obsolete observers are
  // inert even if their actor dies after the table changes ownership.
  struct OwnerObserver final : oresforge::runtime::ActorLifetimeResource {
    std::weak_ptr<State> state;
    bool stopped = false;  // Accessed only while holding State::mutex.
    explicit OwnerObserver(const std::shared_ptr<State>& s) : state(s) {}
    void actor_owner_stopped() noexcept override {
      if (auto s = state.lock()) {
        std::scoped_lock lock(s->mutex);
        stopped = true;
        if (s->active_observer.lock().get() == this) s->retire();
      }
    }
  };
 public:
  class Capability {
    friend class SharedI64Table;
    std::weak_ptr<State> identity_;
    ActorContext grantee_{};
    bool write_ = false;
    // Nonzero only for owner grants: transfers permanently fence old grants.
    std::uint64_t owner_generation_ = 0;
    Capability(const std::shared_ptr<State>& s, ActorContext g, bool w,
               std::uint64_t owner_generation = 0)
        : identity_(s), grantee_(g), write_(w),
          owner_generation_(owner_generation) {}
   public:
    Capability() = default; // Empty handles carry no authority.
  };
 private:
  const std::shared_ptr<State> state_;
  std::shared_ptr<OwnerObserver> owner_observer_; // Updated only under State::mutex.
  [[noreturn]] static void fail(TableErrorCode code) { throw TableError(code); }
  void validate(const State& s, const Capability& cap, ActorContext caller, bool write) const {
    if (!caller.valid() || cap.identity_.lock() != state_ || cap.grantee_ != caller ||
        caller.domain() != s.owner.domain() || (write && !cap.write_)) fail(TableErrorCode::Denied);
    if (cap.owner_generation_ != 0 &&
        cap.owner_generation_ != s.ownership_generation) fail(TableErrorCode::Denied);
    if (s.closed) fail(TableErrorCode::Closed);
    if (s.access == TableAccess::Private && caller != s.owner) fail(TableErrorCode::Denied);
    if (write && s.access == TableAccess::Protected && caller != s.owner) fail(TableErrorCode::Denied);
  }
  static Slot* find(State& s, std::int64_t key) {
    for (auto& slot : s.slots) if (slot.used && slot.key == key) return &slot;
    return nullptr;
  }
  static Slot& vacant(State& s) {
    for (auto& slot : s.slots) if (!slot.used) return slot;
    fail(TableErrorCode::Capacity);
  }
  static void retire(State& s) {
    s.retire();
    // Logical clearing only; no claim of optimizer-resistant secure zeroizing.
  }
 public:
  explicit SharedI64Table(ActorContext owner, TableAccess access = TableAccess::Protected)
      : state_(std::make_shared<State>(owner, access)),
        owner_observer_(std::make_shared<OwnerObserver>(state_)) {
    if (!owner.valid() || (access != TableAccess::Private && access != TableAccess::Protected &&
                          access != TableAccess::Public)) fail(TableErrorCode::Denied);
    // Registration is race-safe with actor stop/restart. If the incarnation
    // was fenced between validation and attachment, construction fails closed.
    state_->active_observer = owner_observer_;
    if (!owner.attach_owned_resource(owner_observer_))
      fail(owner.valid() ? TableErrorCode::Capacity : TableErrorCode::Denied);
  }
  SharedI64Table(const SharedI64Table&) = delete;
  SharedI64Table& operator=(const SharedI64Table&) = delete;

  // Host adapter alone may mint an initial owner capability. Guest code sees
  // only opaque capabilities plus operations using scheduler-derived caller.
  Capability owner_capability(ActorContext caller) const {
    std::lock_guard lock(state_->mutex);
    if (!caller.valid() || caller != state_->owner) fail(TableErrorCode::Denied);
    if (state_->closed) fail(TableErrorCode::Closed);
    return Capability(state_, caller, true, state_->ownership_generation);
  }
  Capability delegate(const Capability& owner_cap, ActorContext caller,
                      ActorContext grantee, bool write = false) const {
    std::lock_guard lock(state_->mutex);
    validate(*state_, owner_cap, caller, true);
    if (caller != state_->owner || !grantee.valid() || grantee.domain() != caller.domain())
      fail(TableErrorCode::Denied);
    if (state_->access == TableAccess::Private && grantee != caller) fail(TableErrorCode::Denied);
    if (write && state_->access == TableAccess::Protected && grantee != caller)
      fail(TableErrorCode::Denied);
    return Capability(state_, grantee, write);
  }
  // ETS-like owner handoff without granting cross-domain ownership. The
  // candidate's observer is registered OUTSIDE the table lock: actor teardown
  // holds the actor-resource lock while calling its observer (lock inversion).
  // The observer records a death even before activation; handoff fails closed
  // if the candidate stopped in that gap. Only the currently active observer
  // can retire a table, so aborted and previous handoffs cannot retire it.
  void give_away(const Capability& owner_cap, ActorContext caller,
                 ActorContext next_owner) {
    std::shared_ptr<OwnerObserver> previous;
    {
      std::lock_guard lock(state_->mutex);
      validate(*state_, owner_cap, caller, true);
      if (caller != state_->owner || !next_owner.valid() ||
          next_owner.domain() != caller.domain() || next_owner == caller)
        fail(TableErrorCode::Denied);
      previous = owner_observer_;
    }
    auto candidate = std::make_shared<OwnerObserver>(state_);
    if (!next_owner.attach_owned_resource(candidate))
      fail(next_owner.valid() ? TableErrorCode::Capacity : TableErrorCode::Denied);
    {
      std::lock_guard lock(state_->mutex);
      if (owner_observer_ != previous) fail(TableErrorCode::Denied);
      validate(*state_, owner_cap, caller, true);
      if (caller != state_->owner || !next_owner.valid() || candidate->stopped)
        fail(TableErrorCode::Denied);
      if (state_->ownership_generation == std::numeric_limits<std::uint64_t>::max())
        fail(TableErrorCode::Overflow);
      ++state_->ownership_generation;
      state_->owner = std::move(next_owner);
      state_->active_observer = candidate;
      owner_observer_ = std::move(candidate);
    }
  }
  std::optional<std::int64_t> lookup(const Capability& cap, ActorContext caller, std::int64_t key) const {
    std::lock_guard lock(state_->mutex);
    validate(*state_, cap, caller, false);
    const auto* slot = find(*state_, key);
    return slot ? std::optional<std::int64_t>(slot->value) : std::nullopt;
  }
  void insert(const Capability& cap, ActorContext caller, std::int64_t key, std::int64_t value) {
    std::lock_guard lock(state_->mutex);
    validate(*state_, cap, caller, true);
    auto* slot = find(*state_, key);
    if (!slot) slot = &vacant(*state_);
    *slot = {true, key, value};
  }
  bool insert_new(const Capability& cap, ActorContext caller, std::int64_t key, std::int64_t value) {
    std::lock_guard lock(state_->mutex);
    validate(*state_, cap, caller, true);
    if (find(*state_, key)) return false;
    vacant(*state_) = {true, key, value};
    return true;
  }
  bool compare_exchange(const Capability& cap, ActorContext caller, std::int64_t key,
                        std::int64_t expected, std::int64_t desired) {
    std::lock_guard lock(state_->mutex);
    validate(*state_, cap, caller, true);
    auto* slot = find(*state_, key);
    if (!slot || slot->value != expected) return false;
    slot->value = desired;
    return true;
  }
  std::int64_t update_counter(const Capability& cap, ActorContext caller,
                              std::int64_t key, std::int64_t delta) {
    std::lock_guard lock(state_->mutex);
    validate(*state_, cap, caller, true);
    auto* slot = find(*state_, key);
    if (!slot) fail(TableErrorCode::Missing);
    const auto value = slot->value;
    if ((delta > 0 && value > std::numeric_limits<std::int64_t>::max() - delta) ||
        (delta < 0 && value < std::numeric_limits<std::int64_t>::min() - delta))
      fail(TableErrorCode::Overflow);
    return slot->value = value + delta;
  }
  bool erase(const Capability& cap, ActorContext caller, std::int64_t key) {
    std::lock_guard lock(state_->mutex);
    validate(*state_, cap, caller, true);
    auto* slot = find(*state_, key);
    if (!slot) return false;
    *slot = {};
    return true;
  }
  void close(const Capability& cap, ActorContext caller) {
    std::lock_guard lock(state_->mutex);
    // Idempotent for the genuine owner, including after retirement.
    if (cap.identity_.lock() != state_ || cap.grantee_ != caller ||
        caller != state_->owner || !cap.write_ ||
        (cap.owner_generation_ != 0 &&
         cap.owner_generation_ != state_->ownership_generation))
      fail(TableErrorCode::Denied);
    retire(*state_);
  }
  // Trusted scheduler hook; no guest may directly supply a death notification.
  void owner_stopped(ActorContext stopped) {
    std::lock_guard lock(state_->mutex);
    if (stopped != state_->owner) fail(TableErrorCode::Denied);
    retire(*state_);
  }
};
}
