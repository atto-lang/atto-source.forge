#pragma once

#include "runtime/actor.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace oresforge::runtime {

/**
 * Demand-gated host adapter over the REAL Forge mailbox. The mailbox owns a
 * COPY on successful admission; a failed offer retains the original vector.
 * Native message moves, guest Flow<T>, and async waiter scheduling are NOT
 * implemented here. Never treat this as zero-copy or source-level move proof.
 *
 * Lock order is port mutex -> mailbox mutex; no user callback runs under either.
 * Checked-out bytes are charged until explicit ACK, including after CANCEL.
 */
template <std::size_t MaxInFlight = 4>
class DemandMailboxPort final {
  static_assert(MaxInFlight > 0 && MaxInFlight <= 4096);
 public:
  enum class Offer { Accepted, NoSubscriber, NoDemand, Full, ByteBudget, Closed };
  enum class PollStatus { Item, NotReady, Complete, Cancelled };
  struct Poll {
    PollStatus status = PollStatus::NotReady;
    std::vector<std::uint8_t> value;
    std::uint64_t ticket = 0;
  };

  DemandMailboxPort(std::uint64_t id, std::size_t capacity,
                    std::size_t max_bytes, std::size_t max_demand)
    : mailbox_(id, capacity, max_bytes),
      owner_(mailbox_.ref()),
      max_bytes_(max_bytes),
      max_demand_(max_demand), capacity_(capacity) {
    if (!max_demand || max_demand > 1000000)
      throw std::invalid_argument("unbounded demand credits");
  }

  bool subscribe() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (subscribed_ || finished_ || cancelled_) return false;
    subscribed_ = true;
    return true;
  }

  void request(std::size_t n) {
    if (!n) throw std::invalid_argument("demand must be positive");
    std::lock_guard<std::mutex> lock(mutex_);
    if (!subscribed_ || finished_ || cancelled_)
      throw std::logic_error("no active subscriber");
    if (n > max_demand_ - credit_)
      throw std::invalid_argument("outstanding demand exhausted");
    credit_ += n;
  }

  Offer try_offer(std::vector<std::uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_ || cancelled_) return Offer::Closed;
    if (!subscribed_) return Offer::NoSubscriber;
    if (!credit_) return Offer::NoDemand;
    if (payload.size() > kMaxMessageBytes)
      return Offer::ByteBudget; // hard per-envelope bound; no exception/fake move
    if (payload.size() > max_bytes_ - retained_bytes_)
      return Offer::ByteBudget;
    if (mailbox_.queued() == capacity_) return Offer::Full;
    // Underlying mailbox validates authority, capacity and byte quota.
    // The copy may allocate; an allocation failure propagates, retaining
    // payload ownership at the caller. Do NOT swallow bad_alloc.
    mailbox_.send(owner_, std::span<const std::uint8_t>(payload));
    retained_bytes_ += payload.size();
    --credit_;
    payload.clear(); // sender relinquishes its logical payload on success
    return Offer::Accepted;
  }

  Poll try_next() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_) return {PollStatus::Cancelled, {}, 0};
    if (!subscribed_) return {};
    std::size_t free = MaxInFlight;
    for (std::size_t i = 0; i < MaxInFlight; ++i)
      if (!loans_[i].active) { free = i; break; }
    if (free == MaxInFlight) return {};
    if (!mailbox_.queued())
      return {finished_ ? PollStatus::Complete : PollStatus::NotReady, {}, 0};
    if (next_ticket_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("delivery ticket exhausted");
    auto value = mailbox_.receive();
    if (!value) throw std::logic_error("exclusive mailbox ownership violated");
    const auto ticket = next_ticket_++;
    loans_[free] = {true, ticket, value->size()};
    // std::vector move does not allocate; pending loans are explicit budget.
    return {PollStatus::Item, std::move(*value), ticket};
  }

  bool ack(std::uint64_t ticket) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& loan : loans_) {
      if (loan.active && loan.ticket == ticket) {
        retained_bytes_ -= loan.bytes;
        loan = {};
        return true;
      }
    }
    return false;
  }

  bool complete() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_ || cancelled_) return false;
    finished_ = true;
    credit_ = 0;
    // Mailbox::close() erases FIFO. Do not call it for graceful completion.
    return true;
  }

  bool cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (finished_ || cancelled_) return false;
    finished_ = true;
    cancelled_ = true;
    credit_ = 0;
    // The mailbox physically scrubs every undelivered payload.
    // Retire only bytes still queued; in-flight loans remain charged.
    const auto loan_bytes = in_flight_bytes();
    mailbox_.close();
    retained_bytes_ = loan_bytes;
    return true;
  }

  std::size_t retained_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return retained_bytes_;
  }
  std::size_t queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mailbox_.queued();
  }
  std::size_t outstanding_demand() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return credit_;
  }

 private:
  struct Loan { bool active = false; std::uint64_t ticket = 0; std::size_t bytes = 0; };
  std::size_t in_flight_bytes() const {
    std::size_t result = 0;
    for (const auto& loan : loans_) if (loan.active) result += loan.bytes;
    return result;
  }
  mutable std::mutex mutex_;
  Mailbox mailbox_;
  ActorRef owner_;
  const std::size_t max_bytes_;
  const std::size_t max_demand_;
  // Read once from the actor mailbox's construction argument.
  std::size_t capacity_ = 0;
  std::array<Loan, MaxInFlight> loans_{};
  std::size_t credit_ = 0;
  std::size_t retained_bytes_ = 0;
  std::uint64_t next_ticket_ = 1;
  bool subscribed_ = false;
  bool finished_ = false;
  bool cancelled_ = false;
};

} // namespace oresforge::runtime
