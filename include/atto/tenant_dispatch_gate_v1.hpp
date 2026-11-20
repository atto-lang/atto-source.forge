#pragma once

// Trusted-host scheduling admission only. This is NOT a hostile guest sandbox.
// No untrusted in-process guest may be admitted until enforced confinement,
// validated host gates, and independent compiled-guest evidence are available.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace atto::host {

enum class TenantExecution : std::uint8_t {
  trusted_shared,
  trusted_isolated,
  untrusted_in_process,
  untrusted_process_fallback
};

enum class DispatchStatus : std::uint8_t {
  ok,
  unsupported,
  exhausted,
  stale,
  closed
};

enum class WorkLane : std::uint8_t { data, control };

struct DispatchWork final {
  std::uint64_t tenant_id;
  std::uint64_t tenant_generation;
  std::uint64_t actor_id;
  std::uint64_t actor_incarnation;
  std::uint64_t receipt_id;
  WorkLane lane;
};

class TenantDispatchGate final {
 public:
  struct Limits final {
    std::size_t max_tenants = 64;
    std::size_t max_pending_global = 4096;
    std::size_t global_control_reserve = 64;
    std::size_t max_pending_per_tenant = 128;
    std::size_t tenant_control_reserve = 4;
    std::size_t max_control_burst = 4;
  };

  // Host-owned credential. NEVER serialize, hand to guest code, or treat it
  // as confinement against malicious native instructions in this process.
  class TenantToken final {
    friend class TenantDispatchGate;
    const TenantDispatchGate* issuer_;
    std::uint64_t id_;
    std::uint64_t generation_;
    TenantToken(const TenantDispatchGate* issuer, std::uint64_t id,
                std::uint64_t generation) noexcept
        : issuer_(issuer), id_(id), generation_(generation) {}
   public:
    TenantToken(const TenantToken&) = default;
    TenantToken& operator=(const TenantToken&) = default;
  };

  struct OpenResult final {
    DispatchStatus status;
    std::optional<TenantToken> token;
  };

  explicit TenantDispatchGate(Limits limits) : limits_(limits) {
    if (limits.max_tenants == 0 || limits.max_pending_global == 0 ||
        limits.max_pending_per_tenant == 0 || limits.max_control_burst == 0 ||
        limits.global_control_reserve >= limits.max_pending_global ||
        limits.tenant_control_reserve >= limits.max_pending_per_tenant ||
        limits.max_pending_global < limits.max_pending_per_tenant) {
      throw std::invalid_argument("invalid tenant dispatch limits");
    }
  }
  TenantDispatchGate(const TenantDispatchGate&) = delete;
  TenantDispatchGate& operator=(const TenantDispatchGate&) = delete;

  [[nodiscard]] OpenResult open(TenantExecution execution) {
    // Hostile native guests cannot be made safe with thread isolation or a
    // bounded queue. There is no independently verified sandbox here.
    if (execution == TenantExecution::untrusted_in_process ||
        execution == TenantExecution::untrusted_process_fallback) {
      return {DispatchStatus::unsupported, std::nullopt};
    }
    std::lock_guard lock(mu_);
    if (tenants_.size() >= limits_.max_tenants || next_id_ == 0 ||
        next_id_ == std::numeric_limits<std::uint64_t>::max()) {
      return {DispatchStatus::exhausted, std::nullopt};
    }
    const auto id = next_id_++;
    constexpr std::uint64_t generation = 1;
    tenants_.emplace(id, State{generation, execution, {}, {}, 0});
    return {DispatchStatus::ok, TenantToken(this, id, generation)};
  }

  // Called exclusively by a trusted gateway after authenticating actor id,
  // incarnation and tenant provenance. This API accepts no guest callback,
  // pointer, FD, or other executable/ambient authority.
  [[nodiscard]] DispatchStatus enqueue(const TenantToken& token,
                                       std::uint64_t actor_id,
                                       std::uint64_t incarnation,
                                       std::uint64_t receipt_id,
                                       WorkLane lane) {
    std::lock_guard lock(mu_);
    auto* state = find_live(token);
    if (!state) return DispatchStatus::stale;
    if (receipt_id == 0 || actor_id == 0 || incarnation == 0)
      return DispatchStatus::unsupported;
    const std::size_t pending = state->control.size() + state->data.size();
    if (pending >= limits_.max_pending_per_tenant ||
        pending_global_ >= limits_.max_pending_global)
      return DispatchStatus::exhausted;
    if (lane == WorkLane::data &&
        (state->data.size() >= limits_.max_pending_per_tenant -
                                   limits_.tenant_control_reserve ||
         pending_data_global_ >= limits_.max_pending_global -
                                     limits_.global_control_reserve)) {
      return DispatchStatus::exhausted;
    }
    DispatchWork job{token.id_, token.generation_, actor_id, incarnation,
                     receipt_id, lane};
    if (lane == WorkLane::control) {
      state->control.push_back(job);
    } else {
      state->data.push_back(job);
      ++pending_data_global_;
    }
    ++pending_global_;
    return DispatchStatus::ok;
  }

  // A worker is explicitly bound by its trusted host to this tenant token.
  // Dequeue never steals from another tenant. This is not guest execution.
  [[nodiscard]] std::optional<DispatchWork> dequeue(const TenantToken& token) {
    std::lock_guard lock(mu_);
    auto* state = find_live(token);
    if (!state) return std::nullopt;
    bool take_control = !state->control.empty() &&
        (state->data.empty() ||
         state->control_burst < limits_.max_control_burst);
    if (!take_control && state->data.empty()) return std::nullopt;
    auto& lane = take_control ? state->control : state->data;
    DispatchWork job = lane.front();
    lane.pop_front();
    --pending_global_;
    if (take_control) {
      if (state->control_burst < limits_.max_control_burst)
        ++state->control_burst;
    } else {
      --pending_data_global_;
      state->control_burst = 0;
    }
    return job;
  }

  // Retirement invalidates all outstanding tickets and drops *undispatched*
  // work. Actual worker continuation/IO quiescence must be handled by the
  // runtime before reclaiming any tenant memory.
  [[nodiscard]] DispatchStatus close(const TenantToken& token) {
    std::lock_guard lock(mu_);
    auto* state = find_live(token);
    if (!state) return DispatchStatus::stale;
    pending_global_ -= state->control.size() + state->data.size();
    pending_data_global_ -= state->data.size();
    tenants_.erase(token.id_);
    return DispatchStatus::ok;
  }

  [[nodiscard]] std::size_t pending() const {
    std::lock_guard lock(mu_);
    return pending_global_;
  }

 private:
  struct State final {
    std::uint64_t generation;
    TenantExecution execution;
    std::deque<DispatchWork> control;
    std::deque<DispatchWork> data;
    std::size_t control_burst = 0;
  };

  [[nodiscard]] State* find_live(const TenantToken& token) {
    if (token.issuer_ != this) return nullptr;
    auto it = tenants_.find(token.id_);
    if (it == tenants_.end() || it->second.generation != token.generation_)
      return nullptr;
    return &it->second;
  }

  const Limits limits_;
  mutable std::mutex mu_;
  std::unordered_map<std::uint64_t, State> tenants_;
  std::uint64_t next_id_ = 1;  // never recycle tenant IDs in this gate lifetime
  std::size_t pending_global_ = 0;
  std::size_t pending_data_global_ = 0;
};

}  // namespace atto::host
