#pragma once
#include "compiler/ir.hpp"
#include "runtime/shared_i64_table.hpp"
#include <cstddef>
#include <cstdint>
#include <utility>

namespace oresforge::shared {

// Trusted host/scheduler bridge for the internal scalar stage0 profile.
// The compiled program supplies only key/value/delta i64 operands.
// Caller identity is always derived afresh from a live mailbox incarnation;
// no guest i64, handle, or forged ActorContext is accepted as caller.
// Stage0 only; every call requires the matching scheduler dispatch lease.
// NOT YET a production guest ABI or a complete scheduler/fiber hook.
template<std::size_t Capacity>
class MailboxBoundI64GuestDispatch final : public oresforge::GuestI64TableDispatch {
  const oresforge::runtime::Mailbox& mailbox_;
  SharedI64Table<Capacity>& table_;
  const typename SharedI64Table<Capacity>::Capability capability_;

  oresforge::runtime::ActorContext current_actor() const {
    // A retained Mailbox reference is not an execution lease: operations must
    // originate from the corresponding scheduler-bound actor handler.
    const auto bound = oresforge::runtime::active_dispatch_actor();
    if (!bound.valid()) throw TableError(TableErrorCode::Denied);
    try {
      if (bound != mailbox_.context()) throw TableError(TableErrorCode::Denied);
    } catch (const std::runtime_error&) {
      throw TableError(TableErrorCode::Denied);
    }
    return bound;
  }

 public:
  MailboxBoundI64GuestDispatch(
      const oresforge::runtime::Mailbox& mailbox,
      SharedI64Table<Capacity>& table,
      typename SharedI64Table<Capacity>::Capability capability)
      : mailbox_(mailbox), table_(table), capability_(std::move(capability)) {}
  std::int64_t lookup(std::int64_t key) override {
    const auto value = table_.lookup(capability_, current_actor(), key);
    if (!value) throw TableError(TableErrorCode::Missing);
    return *value;
  }
  std::int64_t insert(std::int64_t key, std::int64_t value) override {
    table_.insert(capability_, current_actor(), key, value);
    return value;
  }
  std::int64_t insert_new(std::int64_t key, std::int64_t value) override {
    return table_.insert_new(capability_, current_actor(), key, value) ? 1 : 0;
  }
  std::int64_t update_counter(std::int64_t key, std::int64_t delta) override {
    return table_.update_counter(capability_, current_actor(), key, delta);
  }
};

} // namespace oresforge::shared
