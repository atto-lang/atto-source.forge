#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
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
  struct State {
    std::mutex mutex;
    const ActorContext owner;
    const TableAccess access;
    bool closed = false;
    std::array<Slot, Capacity> slots{};
    State(ActorContext o, TableAccess a) : owner(o), access(a) {}
  };
 public:
  class Capability {
    friend class SharedI64Table;
    std::weak_ptr<State> identity_;
    ActorContext grantee_{};
    bool write_ = false;
    Capability(const std::shared_ptr<State>& s, ActorContext g, bool w)
        : identity_(s), grantee_(g), write_(w) {}
   public:
    Capability() = default; // Empty handles carry no authority.
  };
 private:
  const std::shared_ptr<State> state_;
  [[noreturn]] static void fail(TableErrorCode code) { throw TableError(code); }
  void validate(const State& s, const Capability& cap, ActorContext caller, bool write) const {
    if (!caller.valid() || cap.identity_.lock() != state_ || cap.grantee_ != caller ||
        caller.domain() != s.owner.domain() || (write && !cap.write_)) fail(TableErrorCode::Denied);
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
    s.closed = true;
    for (auto& slot : s.slots) slot = {};
    // Logical clearing only; no claim of optimizer-resistant secure zeroizing.
  }
 public:
  explicit SharedI64Table(ActorContext owner, TableAccess access = TableAccess::Protected)
      : state_(std::make_shared<State>(owner, access)) {
    if (!owner.valid() || (access != TableAccess::Private && access != TableAccess::Protected &&
                          access != TableAccess::Public)) fail(TableErrorCode::Denied);
  }
  SharedI64Table(const SharedI64Table&) = delete;
  SharedI64Table& operator=(const SharedI64Table&) = delete;

  // Host adapter alone may mint an initial owner capability. Guest code sees
  // only opaque capabilities plus operations using scheduler-derived caller.
  Capability owner_capability(ActorContext caller) const {
    std::lock_guard lock(state_->mutex);
    if (caller != state_->owner) fail(TableErrorCode::Denied);
    if (state_->closed) fail(TableErrorCode::Closed);
    return Capability(state_, caller, true);
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
        caller != state_->owner || !cap.write_) fail(TableErrorCode::Denied);
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
