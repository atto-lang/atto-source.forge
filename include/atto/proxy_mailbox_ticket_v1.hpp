#pragma once
// Experimental Forge trusted-HOST mailbox ticket/turn model (C++20).
// NOT production TenantVM isolation, IPC, authentication, compiler .atto guest
// ABI, a durable-provider commit or a competing app reply registry.
// A gateway authenticates tenant, capability, actor and peer BEFORE admission.
// Route identity below is correlation metadata, not an authorization token.
#include "proxy_receipt_gate_v1.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace atto::ipc {
enum class host_ticket_state { admitted, started, processed, failed, abandoned };
enum class host_ticket_admission { admitted, invalid, stale, closed, full };
struct host_ticket_snapshot {
    receipt_identity route;
    host_ticket_state state;
    bool has_result;
    std::int64_t result;
};
class host_ticket final {
    friend class host_proxy_mailbox_tickets;
    const std::shared_ptr<void> owner_;
    const receipt_identity route_;
    host_ticket_state state_ = host_ticket_state::admitted;
    bool has_result_ = false;
    std::int64_t result_ = 0;
    host_ticket(std::shared_ptr<void> owner, receipt_identity route)
        : owner_(std::move(owner)), route_(route) {}
};
using host_ticket_ptr = std::shared_ptr<host_ticket>;
struct host_ticket_admission_result {
    host_ticket_admission status;
    host_ticket_ptr ticket;
};
class host_turn final {
    friend class host_proxy_mailbox_tickets;
    const host_ticket_ptr ticket_;
    const std::int64_t value_;
    host_turn(host_ticket_ptr ticket, std::int64_t value)
        : ticket_(std::move(ticket)), value_(value) {}
public:
    std::int64_t value() const noexcept { return value_; }
};
class host_proxy_mailbox_tickets final {
    const std::shared_ptr<void> owner_ = std::make_shared<int>(0);
    const std::uint64_t tenant_, actor_, incarnation_;
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<std::pair<std::int64_t, host_ticket_ptr>> queue_;
    host_ticket_ptr current_;
    bool closed_ = false, fenced_ = false;
    std::size_t admitted_ = 0, processed_ = 0, failed_ = 0, abandoned_ = 0;
    static bool valid(const receipt_identity& r) noexcept {
        return r.protocol_major == 1 && r.tenant && r.caller_incarnation &&
               r.target_actor && r.target_incarnation &&
               r.association_generation && r.placement_epoch &&
               r.message_id && r.attempt;
    }
public:
    host_proxy_mailbox_tickets(std::uint64_t tenant, std::uint64_t actor, std::uint64_t incarnation,
                               std::size_t capacity)
        : tenant_(tenant), actor_(actor), incarnation_(incarnation), capacity_(capacity) {
        if (!tenant || !actor || !incarnation || capacity < 1 || capacity > 4096)
            throw std::invalid_argument("invalid host actor mailbox");
    }
    // Issue exactly one private ticket under the same mailbox admission mutex.
    host_ticket_admission_result admit(const receipt_identity& route, std::int64_t value) {
        if (!valid(route)) return {host_ticket_admission::invalid, {}};
        if (route.tenant != tenant_ || route.target_actor != actor_ || route.target_incarnation != incarnation_)
            return {host_ticket_admission::stale, {}};
        std::lock_guard lock(mutex_);
        if (closed_ || fenced_) return {host_ticket_admission::closed, {}};
        if (queue_.size() >= capacity_) return {host_ticket_admission::full, {}};
        auto ticket = host_ticket_ptr(new host_ticket(owner_, route));
        queue_.emplace_back(value, ticket);
        ++admitted_;
        return {host_ticket_admission::admitted, std::move(ticket)};
    }
    std::optional<host_ticket_snapshot> snapshot(const host_ticket_ptr& ticket) const {
        std::lock_guard lock(mutex_);
        if (!ticket || ticket->owner_ != owner_) return std::nullopt;
        return host_ticket_snapshot{ticket->route_, ticket->state_,
                                    ticket->has_result_, ticket->result_};
    }
    // Must be called by the actual trusted actor carrier on mailbox dequeue.
    std::optional<host_turn> dispatch() {
        std::lock_guard lock(mutex_);
        if (fenced_ || current_ || queue_.empty()) return std::nullopt;
        auto item = std::move(queue_.front());
        queue_.pop_front();
        if (item.second->state_ != host_ticket_state::admitted)
            throw std::logic_error("ticket conservation violated");
        item.second->state_ = host_ticket_state::started;
        current_ = item.second;
        return host_turn(std::move(item.second), item.first);
    }
    bool commit(const host_turn& turn, std::int64_t result) {
        std::lock_guard lock(mutex_);
        if (fenced_ || !current_ || turn.ticket_ != current_ ||
            current_->state_ != host_ticket_state::started) return false;
        current_->state_ = host_ticket_state::processed;
        current_->has_result_ = true;
        current_->result_ = result;
        current_.reset();
        ++processed_;
        return true;
    }
    // PANIC is fenced here; priority supervisor CONTROL is distinct.
    bool panic(const host_turn& turn) {
        std::lock_guard lock(mutex_);
        if (fenced_ || !current_ || turn.ticket_ != current_ ||
            current_->state_ != host_ticket_state::started) return false;
        current_->state_ = host_ticket_state::failed;
        current_.reset();
        ++failed_;
        fenced_ = closed_ = true;
        for (const auto& item : queue_) {
            if (item.second->state_ != host_ticket_state::admitted)
                throw std::logic_error("ticket conservation violated");
            item.second->state_ = host_ticket_state::abandoned;
            ++abandoned_;
        }
        queue_.clear();
        return true;
    }
    void close() { std::lock_guard lock(mutex_); closed_ = true; }
    bool conservation() const {
        std::lock_guard lock(mutex_);
        return admitted_ == processed_ + failed_ + abandoned_ +
            queue_.size() + (current_ ? 1U : 0U);
    }
};
} // namespace atto::ipc
