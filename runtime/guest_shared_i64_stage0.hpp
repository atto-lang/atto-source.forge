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
// NOT YET a production guest ABI, execution lease, or scheduler hook.
template<std::size_t Capacity>
class MailboxBoundI64GuestDispatch final : public oresforge::GuestI64TableDispatch {
  const oresforge::runtime::Mailbox& mailbox_;
  SharedI64Table<Capacity>& table_;
  const typename SharedI64Table<Capacity>::Capability capability_;

 public:
  MailboxBoundI64GuestDispatch(
      const oresforge::runtime::Mailbox& mailbox,
      SharedI64Table<Capacity>& table,
      typename SharedI64Table<Capacity>::Capability capability)
      : mailbox_(mailbox), table_(table), capability_(std::move(capability)) {}
  std::int64_t lookup(std::int64_t key) override {
    const auto value = table_.lookup(capability_, mailbox_.context(), key);
    if (!value) throw TableError(TableErrorCode::Missing);
    return *value;
  }
  std::int64_t insert(std::int64_t key, std::int64_t value) override {
    table_.insert(capability_, mailbox_.context(), key, value);
    return value;
  }
  std::int64_t insert_new(std::int64_t key, std::int64_t value) override {
    return table_.insert_new(capability_, mailbox_.context(), key, value) ? 1 : 0;
  }
  std::int64_t update_counter(std::int64_t key, std::int64_t delta) override {
    return table_.update_counter(capability_, mailbox_.context(), key, delta);
  }
};

} // namespace oresforge::shared
