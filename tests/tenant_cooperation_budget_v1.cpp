#include "atto/tenant_cooperation_budget_v1.hpp"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

using Budget = atto::forge::runtime::TenantCooperationBudget;
using Status = Budget::ChargeStatus;

int main() {
  // Regression for legacy Forge #223: repeated 127-unit dispatches cannot
  // reset the poll phase. Four dispatches must cross three 128 boundaries.
  {
    Budget host;
    const auto tenant = host.admit_tenant(1000).value();
    const auto task = host.admit_task(tenant, 700).value();
    int polls = 0;
    for (int dispatch = 0; dispatch < 4; ++dispatch) {
      const auto turn = host.begin_dispatch(tenant, task).value();
      assert(!host.begin_dispatch(tenant, task));  // no reset of active turn
      for (int n = 0; n < 127; ++n) {
        const auto result = host.charge(tenant, task, turn);
        assert(result.committed());
        polls += result.poll_due() ? 1 : 0;
      }
      assert(host.end_dispatch(tenant, task, turn));
      assert(!host.charge(tenant, task, turn).committed());
      assert(!host.end_dispatch(tenant, task, turn));
    }
    assert(polls == 3);
    const auto snapshot = host.snapshot(tenant, task).value();
    assert(snapshot.remaining_fuel == 192 && snapshot.committed_work == 508);
    assert(snapshot.poll_phase == 124 && !snapshot.dispatch_active);
  }
  {
    Budget host;
    const auto tenant = host.admit_tenant(2000).value();
    const auto task = host.admit_task(tenant, 1024).value();
    const auto turn = host.begin_dispatch(tenant, task).value();
    int polls = 0;
    for (int i = 1; i <= 512; ++i) {
      auto result = host.charge(tenant, task, turn);
      assert(result.committed());
      polls += result.poll_due() ? 1 : 0;
      assert(result.yield_due() == (i == 512));
    }
    assert(polls == 4);
    assert(host.charge(tenant, task, turn).status == Status::DispatchLimitReached);
    assert(!host.begin_dispatch(tenant, task));
    assert(host.end_dispatch(tenant, task, turn));
    const auto next = host.begin_dispatch(tenant, task).value();
    assert(host.charge(tenant, task, turn).status == Status::InvalidTicket);
    assert(host.charge(tenant, task, next).committed());
    assert(host.snapshot(tenant, task)->remaining_fuel == 511);
    assert(host.end_dispatch(tenant, task, next));
  }
  // Weighted commits are bounded/atomic. Neither failed charge nor resumption
  // may refill fuel or modify phase or dispatch count.
  {
    Budget host;
    const auto tenant = host.admit_tenant(1000, 2).value();
    const auto task = host.admit_task(tenant, 513).value();
    assert(!host.admit_task(tenant, 488));  // reservation quota
    const auto leftover = host.admit_task(tenant, 487).value();
    assert(!host.admit_task(tenant, 1));
    auto turn = host.begin_dispatch(tenant, task).value();
    assert(host.charge(tenant, task, turn, 0).status == Status::InvalidWeight);
    assert(host.charge(tenant, task, turn, 129).status == Status::InvalidWeight);
    for (int i = 0; i < 4; ++i) {
      auto result = host.charge(tenant, task, turn, 128);
      assert(result.poll_due());
    }
    assert(host.charge(tenant, task, turn, 1).status == Status::DispatchLimitReached);
    assert(host.snapshot(tenant, task)->remaining_fuel == 1);
    assert(host.end_dispatch(tenant, task, turn));
    turn = host.begin_dispatch(tenant, task).value();
    assert(host.charge(tenant, task, turn, 2).status == Status::FuelExhausted);
    assert(host.snapshot(tenant, task)->committed_work == 512);
    assert(host.charge(tenant, task, turn).committed());
    assert(host.charge(tenant, task, turn).status == Status::FuelExhausted);
    assert(host.end_dispatch(tenant, task, turn));
    assert(!host.begin_dispatch(tenant, task));
    assert(host.retire_task(tenant, task));
    assert(!host.admit_task(tenant, 1));  // no refund after retirement
    assert(host.snapshot(tenant, leftover)->remaining_fuel == 487);
  }
  // Cross-tenant, cross-registry, retirement and stale-dispatch replay.
  {
    Budget a(2), b(1);
    const auto ta = a.admit_tenant(100).value();
    const auto tb = a.admit_tenant(100).value();
    assert(!a.admit_tenant(1));
    const auto foreign = b.admit_tenant(100).value();
    const auto actor = a.admit_task(ta, 90).value();
    const auto other = a.admit_task(tb, 80).value();
    const auto turn = a.begin_dispatch(ta, actor).value();
    assert(a.charge(tb, actor, turn).status == Status::InvalidTicket);
    assert(a.charge(ta, other, turn).status == Status::InvalidTicket);
    assert(b.charge(foreign, actor, turn).status == Status::InvalidTicket);
    assert(!b.snapshot(ta, actor));
    assert(a.retire_task(ta, actor));
    assert(a.charge(ta, actor, turn).status == Status::InvalidTicket);
    assert(!a.end_dispatch(ta, actor, turn));
    assert(a.retire_tenant(ta));
    assert(!a.admit_task(ta, 1));
    const auto fresh = a.admit_tenant(100).value();
    const auto fresh_task = a.admit_task(fresh, 50).value();
    assert(a.charge(fresh, fresh_task, turn).status == Status::InvalidTicket);
    assert(!a.retire_tenant(ta));
    assert(!a.begin_dispatch(Budget::TenantTicket{}, fresh_task));
    assert(!a.snapshot(fresh, Budget::TaskTicket{}));
  }
  // Same-turn concurrent charges serialize; cross-tenant work is independent.
  {
    Budget host;
    const auto ta = host.admit_tenant(10000).value();
    const auto tb = host.admit_tenant(10000).value();
    const auto a = host.admit_task(ta, 8192).value();
    const auto b = host.admit_task(tb, 8192).value();
    const auto da = host.begin_dispatch(ta, a).value();
    const auto db = host.begin_dispatch(tb, b).value();
    std::atomic<int> errors{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) {
      workers.emplace_back([&, i] {
        const auto& tenant = i < 4 ? ta : tb;
        const auto& task = i < 4 ? a : b;
        const auto& turn = i < 4 ? da : db;
        for (int j = 0; j < 128; ++j) {
          const auto result = host.charge(tenant, task, turn);
          if (j < 128 && !result.committed() &&
              result.status != Status::DispatchLimitReached) ++errors;
        }
      });
    }
    for (auto& worker : workers) worker.join();
    assert(errors == 0);
    assert(host.snapshot(ta, a)->committed_work == 512);
    assert(host.snapshot(tb, b)->committed_work == 512);
    assert(host.end_dispatch(ta, a, da));
    assert(host.end_dispatch(tb, b, db));
  }
  // Bounded admissions and invalid control inputs must fail closed.
  {
    Budget host(1);
    assert(!host.admit_tenant(0));
    assert(!host.admit_tenant(Budget::kMaxTenantFuel + 1));
    assert(!host.admit_tenant(10, 0));
    assert(!host.admit_tenant(10, Budget::kMaxTasks + 1));
    const auto t = host.admit_tenant(Budget::kMaxTenantFuel).value();
    assert(!host.admit_tenant(1));
    assert(!host.admit_task(t, 0));
    assert(!host.admit_task(t, Budget::kMaxTenantFuel + 1));
    const auto task = host.admit_task(t, 1).value();
    const auto turn = host.begin_dispatch(t, task).value();
    assert(host.charge(t, task, turn, std::numeric_limits<std::uint32_t>::max()).status == Status::InvalidWeight);
    assert(host.snapshot(t, task)->remaining_fuel == 1);
    assert(host.end_dispatch(t, task, turn));
  }
}
