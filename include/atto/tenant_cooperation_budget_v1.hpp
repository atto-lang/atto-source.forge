#pragma once

// Trusted-host cooperation accounting for an Atto Forge TenantVM.
// Not a security boundary against hostile in-process native code. The compiler
// must instrument every bounded basic block/recursion/backedge, and the
// scheduler must honor Poll/Yield before this can govern untrusted guests.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace atto::forge::runtime {

class TenantCooperationBudget final {
 public:
  static constexpr std::uint32_t kPollInterval = 128;
  static constexpr std::uint32_t kDispatchLimit = 512;
  static constexpr std::uint32_t kMaxBlockWeight = 128;
  static constexpr std::uint64_t kMaxTenantFuel = 1'000'000'000;
  static constexpr std::size_t kMaxTenants = 1024;
  static constexpr std::size_t kMaxTasks = 1024;

  class TenantTicket final {
    friend class TenantCooperationBudget;
    std::uint64_t issuer_ = 0, tenant_ = 0;
    TenantTicket(std::uint64_t issuer, std::uint64_t tenant) noexcept
        : issuer_(issuer), tenant_(tenant) {}
   public:
    TenantTicket() = default;  // Invalid, never a capability.
  };
  class TaskTicket final {
    friend class TenantCooperationBudget;
    std::uint64_t issuer_ = 0, tenant_ = 0, task_ = 0;
    TaskTicket(std::uint64_t issuer, std::uint64_t tenant,
               std::uint64_t task) noexcept
        : issuer_(issuer), tenant_(tenant), task_(task) {}
   public:
    TaskTicket() = default;
  };
  class DispatchTicket final {
    friend class TenantCooperationBudget;
    std::uint64_t issuer_ = 0, tenant_ = 0, task_ = 0, serial_ = 0;
    DispatchTicket(std::uint64_t issuer, std::uint64_t tenant,
                   std::uint64_t task, std::uint64_t serial) noexcept
        : issuer_(issuer), tenant_(tenant), task_(task), serial_(serial) {}
   public:
    DispatchTicket() = default;
  };

  enum class ChargeStatus {
    Continue, Poll, Yield, FuelExhausted, DispatchLimitReached,
    InvalidWeight, InvalidTicket
  };
  struct ChargeResult {
    ChargeStatus status;
    [[nodiscard]] bool committed() const noexcept {
      return status == ChargeStatus::Continue ||
             status == ChargeStatus::Poll || status == ChargeStatus::Yield;
    }
    [[nodiscard]] bool poll_due() const noexcept {
      return status == ChargeStatus::Poll || status == ChargeStatus::Yield;
    }
    [[nodiscard]] bool yield_due() const noexcept {
      return status == ChargeStatus::Yield;
    }
  };
  struct Snapshot {
    std::uint64_t remaining_fuel;
    std::uint64_t committed_work;
    std::uint32_t poll_phase;
    std::uint32_t dispatch_work;
    bool dispatch_active;
  };

  explicit TenantCooperationBudget(std::size_t tenant_limit = 32)
      : tenant_limit_(tenant_limit), issuer_(issue_global_id()) {
    if (tenant_limit == 0 || tenant_limit > kMaxTenants)
      throw std::invalid_argument("invalid tenant admission limit");
  }
  TenantCooperationBudget(const TenantCooperationBudget&) = delete;
  TenantCooperationBudget& operator=(const TenantCooperationBudget&) = delete;
  TenantCooperationBudget(TenantCooperationBudget&&) = delete;
  TenantCooperationBudget& operator=(TenantCooperationBudget&&) = delete;

  [[nodiscard]] std::optional<TenantTicket> admit_tenant(
      std::uint64_t lifetime_fuel, std::size_t task_limit = 64) {
    if (lifetime_fuel == 0 || lifetime_fuel > kMaxTenantFuel ||
        task_limit == 0 || task_limit > kMaxTasks) return std::nullopt;
    std::lock_guard lock(mu_);
    if (tenants_.size() >= tenant_limit_ || next_id_ == max_id())
      return std::nullopt;
    const auto id = next_id_++;
    tenants_.emplace(id, TenantState{lifetime_fuel, task_limit, {}});
    return TenantTicket{issuer_, id};
  }

  [[nodiscard]] std::optional<TaskTicket> admit_task(
      const TenantTicket& tenant, std::uint64_t task_fuel) {
    if (task_fuel == 0 || task_fuel > kMaxTenantFuel) return std::nullopt;
    std::lock_guard lock(mu_);
    auto* t = checked_tenant(tenant);
    if (!t || t->tasks.size() >= t->task_limit ||
        task_fuel > t->unallocated_fuel || next_id_ == max_id())
      return std::nullopt;
    const auto id = next_id_++;
    t->tasks.emplace(id, TaskState{task_fuel, 0, 0, 0, 0, false});
    // Lifetime reservation is intentionally never returned on retirement.
    // Recreating tasks must not replenish exhausted tenant fuel.
    t->unallocated_fuel -= task_fuel;
    return TaskTicket{issuer_, tenant.tenant_, id};
  }

  [[nodiscard]] std::optional<DispatchTicket> begin_dispatch(
      const TenantTicket& tenant, const TaskTicket& task) {
    std::lock_guard lock(mu_);
    auto* t = checked_task(tenant, task);
    if (!t || t->active || t->remaining_fuel == 0 ||
        t->serial == max_id()) return std::nullopt;
    // Only the dispatch-local work budget resets. Phase, cumulative work and
    // lifetime fuel survive arbitrary short dispatch/resume cycles.
    t->dispatch_work = 0;
    t->active = true;
    ++t->serial;
    return DispatchTicket{issuer_, tenant.tenant_, task.task_, t->serial};
  }

  [[nodiscard]] ChargeResult charge(const TenantTicket& tenant,
                                    const TaskTicket& task,
                                    const DispatchTicket& dispatch,
                                    std::uint32_t weight = 1) {
    std::lock_guard lock(mu_);
    auto* t = checked_task(tenant, task);
    if (!t || !t->active || dispatch.issuer_ != issuer_ ||
        dispatch.tenant_ != tenant.tenant_ ||
        dispatch.task_ != task.task_ || dispatch.serial_ != t->serial)
      return {ChargeStatus::InvalidTicket};
    if (weight == 0 || weight > kMaxBlockWeight)
      return {ChargeStatus::InvalidWeight};
    if (weight > t->remaining_fuel)
      return {ChargeStatus::FuelExhausted};
    if (weight > kDispatchLimit - t->dispatch_work)
      return {ChargeStatus::DispatchLimitReached};

    // Checked bounds above: addition is <=255; at most one poll boundary.
    const auto new_phase = t->poll_phase + weight;
    const bool crossed_poll = new_phase >= kPollInterval;
    t->poll_phase = new_phase % kPollInterval;
    t->remaining_fuel -= weight;
    t->committed_work += weight;  // <= admitted fuel <= 1e9
    t->dispatch_work += weight;
    const bool yield = t->dispatch_work == kDispatchLimit;
    // Fairness and checkpoint are independent: weighted charges may hit the
    // 512-unit limit even without exactly landing on a 128-phase boundary.
    return {yield ? ChargeStatus::Yield :
            crossed_poll ? ChargeStatus::Poll : ChargeStatus::Continue};
  }

  [[nodiscard]] bool end_dispatch(const TenantTicket& tenant,
                                  const TaskTicket& task,
                                  const DispatchTicket& dispatch) {
    std::lock_guard lock(mu_);
    auto* t = checked_task(tenant, task);
    if (!t || !t->active || dispatch.issuer_ != issuer_ ||
        dispatch.tenant_ != tenant.tenant_ ||
        dispatch.task_ != task.task_ || dispatch.serial_ != t->serial)
      return false;
    t->active = false;
    return true;
  }

  [[nodiscard]] std::optional<Snapshot> snapshot(
      const TenantTicket& tenant, const TaskTicket& task) const {
    std::lock_guard lock(mu_);
    const auto* t = checked_task(tenant, task);
    if (!t) return std::nullopt;
    return Snapshot{t->remaining_fuel, t->committed_work, t->poll_phase,
                    t->dispatch_work, t->active};
  }

  [[nodiscard]] bool retire_task(const TenantTicket& tenant,
                                 const TaskTicket& task) {
    std::lock_guard lock(mu_);
    auto* t = checked_tenant(tenant);
    if (!t || task.issuer_ != issuer_ || task.tenant_ != tenant.tenant_)
      return false;
    return t->tasks.erase(task.task_) == 1;
  }

  [[nodiscard]] bool retire_tenant(const TenantTicket& tenant) {
    std::lock_guard lock(mu_);
    if (!checked_tenant(tenant)) return false;
    return tenants_.erase(tenant.tenant_) == 1;
  }

 private:
  struct TaskState {
    std::uint64_t remaining_fuel;
    std::uint64_t committed_work;
    std::uint32_t poll_phase;
    std::uint32_t dispatch_work;
    std::uint64_t serial;
    bool active;
  };
  struct TenantState {
    std::uint64_t unallocated_fuel;
    std::size_t task_limit;
    std::unordered_map<std::uint64_t, TaskState> tasks;
  };
  static constexpr std::uint64_t max_id() noexcept {
    return std::numeric_limits<std::uint64_t>::max();
  }
  // Unique across service destruction/recreation: avoid pointer-address ABA.
  static std::uint64_t issue_global_id() {
    std::uint64_t current = next_issuer_.load(std::memory_order_relaxed);
    for (;;) {
      if (current == max_id())
        throw std::overflow_error("cooperation issuer exhausted");
      if (next_issuer_.compare_exchange_weak(current, current + 1,
            std::memory_order_relaxed, std::memory_order_relaxed))
        return current;
    }
  }
  TenantState* checked_tenant(const TenantTicket& key) {
    if (key.issuer_ != issuer_ || key.tenant_ == 0) return nullptr;
    auto it = tenants_.find(key.tenant_);
    return it == tenants_.end() ? nullptr : &it->second;
  }
  const TenantState* checked_tenant(const TenantTicket& key) const {
    if (key.issuer_ != issuer_ || key.tenant_ == 0) return nullptr;
    auto it = tenants_.find(key.tenant_);
    return it == tenants_.end() ? nullptr : &it->second;
  }
  TaskState* checked_task(const TenantTicket& tenant, const TaskTicket& task) {
    auto* owner = checked_tenant(tenant);
    if (!owner || task.issuer_ != issuer_ ||
        task.tenant_ != tenant.tenant_ || task.task_ == 0) return nullptr;
    auto it = owner->tasks.find(task.task_);
    return it == owner->tasks.end() ? nullptr : &it->second;
  }
  const TaskState* checked_task(const TenantTicket& tenant,
                                const TaskTicket& task) const {
    const auto* owner = checked_tenant(tenant);
    if (!owner || task.issuer_ != issuer_ ||
        task.tenant_ != tenant.tenant_ || task.task_ == 0) return nullptr;
    auto it = owner->tasks.find(task.task_);
    return it == owner->tasks.end() ? nullptr : &it->second;
  }

  static inline std::atomic<std::uint64_t> next_issuer_{1};
  mutable std::mutex mu_;
  std::unordered_map<std::uint64_t, TenantState> tenants_;
  const std::size_t tenant_limit_;
  const std::uint64_t issuer_;
  std::uint64_t next_id_ = 1;
};

}  // namespace atto::forge::runtime
