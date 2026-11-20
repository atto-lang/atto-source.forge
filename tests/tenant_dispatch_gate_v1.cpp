#include "atto/tenant_dispatch_gate_v1.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>

using namespace atto::host;

int main() {
  using Status = DispatchStatus;
  using Kind = TenantExecution;
  using Lane = WorkLane;

  TenantDispatchGate::Limits limits;
  limits.max_tenants = 2;
  limits.max_pending_global = 12;
  limits.global_control_reserve = 2;
  limits.max_pending_per_tenant = 8;
  limits.tenant_control_reserve = 2;
  limits.max_control_burst = 2;
  TenantDispatchGate gate(limits);
  assert(gate.open(Kind::untrusted_in_process).status == Status::unsupported);
  assert(gate.open(Kind::untrusted_process_fallback).status == Status::unsupported);
  assert(gate.pending() == 0);

  auto first = gate.open(Kind::trusted_isolated);
  auto second = gate.open(Kind::trusted_isolated);
  assert(first.status == Status::ok && second.status == Status::ok);
  const auto a = *first.token;
  const auto b = *second.token;
  assert(gate.open(Kind::trusted_shared).status == Status::exhausted);
  assert(gate.enqueue(a, 5, 1, 100, Lane::data) == Status::ok);
  assert(!gate.dequeue(b).has_value());  // no cross-tenant stealing
  assert(gate.enqueue(b, 8, 2, 101, Lane::data) == Status::ok);
  assert(gate.enqueue(a, 5, 1, 102, Lane::control) == Status::ok);
  assert(gate.enqueue(a, 5, 1, 103, Lane::control) == Status::ok);
  assert(gate.enqueue(a, 5, 1, 104, Lane::control) == Status::ok);
  auto next = gate.dequeue(a);
  assert(next && next->lane == Lane::control && next->receipt_id == 102);
  next = gate.dequeue(a);
  assert(next && next->lane == Lane::control && next->receipt_id == 103);
  next = gate.dequeue(a);
  assert(next && next->lane == Lane::data && next->receipt_id == 100);
  next = gate.dequeue(a);
  assert(next && next->lane == Lane::control && next->receipt_id == 104);
  next = gate.dequeue(b);
  assert(next && next->tenant_id != 0 && next->receipt_id == 101);
  assert(gate.enqueue(a, 0, 1, 99, Lane::data) == Status::unsupported);
  assert(gate.enqueue(a, 5, 0, 99, Lane::data) == Status::unsupported);
  assert(gate.enqueue(a, 5, 1, 0, Lane::data) == Status::unsupported);
  assert(gate.close(a) == Status::ok);
  assert(gate.close(a) == Status::stale);
  assert(gate.enqueue(a, 5, 1, 105, Lane::data) == Status::stale);
  assert(!gate.dequeue(a));
  auto again = gate.open(Kind::trusted_isolated);
  assert(again.status == Status::ok);
  assert(gate.enqueue(a, 5, 1, 106, Lane::data) == Status::stale);
  assert(gate.enqueue(*again.token, 5, 2, 106, Lane::data) == Status::ok);

  TenantDispatchGate other(limits);
  auto foreign = other.open(Kind::trusted_isolated);
  assert(foreign.status == Status::ok);
  assert(gate.enqueue(*foreign.token, 5, 1, 99, Lane::data) == Status::stale);
  assert(!gate.dequeue(*foreign.token));

  // Limits preserve per-tenant CONTROL admission even when DATA is full.
  assert(gate.close(*again.token) == Status::ok);
  auto crowded = gate.open(Kind::trusted_isolated);
  assert(crowded.status == Status::ok);
  for (std::uint64_t i = 1; i <= 6; ++i)
    assert(gate.enqueue(*crowded.token, 1, 1, i, Lane::data) == Status::ok);
  assert(gate.enqueue(*crowded.token, 1, 1, 7, Lane::data) == Status::exhausted);
  assert(gate.enqueue(*crowded.token, 1, 1, 8, Lane::control) == Status::ok);
  assert(gate.enqueue(*crowded.token, 1, 1, 9, Lane::control) == Status::ok);
  assert(gate.enqueue(*crowded.token, 1, 1, 10, Lane::control) == Status::exhausted);
  assert(gate.close(*crowded.token) == Status::ok);
  assert(gate.pending() == 0);

  // Concurrent host admissions + retirement: no stale work is accepted.
  auto live = gate.open(Kind::trusted_shared);
  assert(live.status == Status::ok);
  std::atomic<bool> stop = false;
  std::atomic<std::uint64_t> accepted = 0;
  std::vector<std::thread> workers;
  for (int tid = 0; tid < 4; ++tid) {
    workers.emplace_back([&, tid] {
      for (std::uint64_t i = 1; i < 1000; ++i) {
        if (stop.load()) break;
        const auto s = gate.enqueue(*live.token, static_cast<std::uint64_t>(tid + 1),
                                    1, i, Lane::data);
        if (s == Status::ok) ++accepted;
        else assert(s == Status::exhausted || s == Status::stale);
      }
    });
  }
  assert(gate.close(*live.token) == Status::ok);
  stop = true;
  for (auto& worker : workers) worker.join();
  assert(gate.enqueue(*live.token, 1, 1, 99, Lane::control) == Status::stale);
  assert(gate.pending() == 0);
  (void)accepted;
  return 0;
}
