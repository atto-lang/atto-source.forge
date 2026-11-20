#include "atto/tenant_dispatch_gate_v1.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <new>
#include <type_traits>
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
  limits.global_control_reserve = 6;
  limits.max_pending_per_tenant = 8;
  limits.tenant_control_reserve = 3;
  limits.max_control_burst = 2;
  static_assert(!std::is_default_constructible_v<TenantDispatchGate::TenantToken>);
  static_assert(!std::is_default_constructible_v<TenantDispatchGate::SupervisorToken>);
  static_assert(!std::is_constructible_v<TenantDispatchGate::SupervisorToken,
                                          TenantDispatchGate::TenantToken>);
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
  assert(gate.enqueue_data(a, 5, 1, 100) == Status::ok);
  assert(!gate.dequeue(b).has_value());  // no cross-tenant stealing
  assert(gate.enqueue_data(b, 8, 2, 101) == Status::ok);
  assert(gate.enqueue_control(*first.supervisor, 5, 1, 102) == Status::ok);
  assert(gate.enqueue_control(*first.supervisor, 5, 1, 103) == Status::ok);
  assert(gate.enqueue_control(*first.supervisor, 5, 1, 104) == Status::ok);
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
  assert(gate.enqueue_data(a, 0, 1, 99) == Status::unsupported);
  assert(gate.enqueue_data(a, 5, 0, 99) == Status::unsupported);
  assert(gate.enqueue_data(a, 5, 1, 0) == Status::unsupported);
  assert(gate.close(a) == Status::ok);
  assert(gate.close(a) == Status::stale);
  assert(gate.enqueue_control(*first.supervisor, 5, 1, 77) == Status::stale);
  assert(gate.enqueue_data(a, 5, 1, 105) == Status::stale);
  assert(!gate.dequeue(a));
  auto again = gate.open(Kind::trusted_isolated);
  assert(again.status == Status::ok);
  assert(gate.enqueue_data(a, 5, 1, 106) == Status::stale);
  assert(gate.enqueue_data(*again.token, 5, 2, 106) == Status::ok);

  TenantDispatchGate other(limits);
  auto foreign = other.open(Kind::trusted_isolated);
  assert(foreign.status == Status::ok);
  assert(gate.enqueue_data(*foreign.token, 5, 1, 99) == Status::stale);
  assert(!gate.dequeue(*foreign.token));
  assert(gate.enqueue_control(*foreign.supervisor, 5, 1, 99) == Status::stale);

  // DATA cannot spend global CONTROL reservations; all tenant supervisors
  // retain their reserved capacity in a fully admitted host configuration.
  TenantDispatchGate crowded_global(limits);
  auto ga = crowded_global.open(Kind::trusted_isolated);
  auto gb = crowded_global.open(Kind::trusted_isolated);
  assert(ga.status == Status::ok && gb.status == Status::ok);
  for (std::uint64_t i = 1; i <= 5; ++i)
    assert(crowded_global.enqueue_data(*ga.token, 1, 1, i) == Status::ok);
  assert(crowded_global.enqueue_data(*gb.token, 2, 1, 1) == Status::ok);
  assert(crowded_global.enqueue_data(*gb.token, 2, 1, 2) == Status::exhausted);
  for (std::uint64_t i = 1; i <= 3; ++i) {
    assert(crowded_global.enqueue_control(*ga.supervisor, 1, 1, i) == Status::ok);
    assert(crowded_global.enqueue_control(*gb.supervisor, 2, 1, i) == Status::ok);
  }
  assert(crowded_global.pending() == 12);
  assert(crowded_global.close(*ga.token) == Status::ok);
  assert(crowded_global.close(*gb.token) == Status::ok);
  assert(crowded_global.pending() == 0);

  // Recycled C++ object addresses cannot authenticate stale issuer tickets.
  alignas(TenantDispatchGate) unsigned char storage[sizeof(TenantDispatchGate)];
  auto* old_gate = new (storage) TenantDispatchGate(limits);
  auto old_open = old_gate->open(Kind::trusted_shared);
  assert(old_open.status == Status::ok);
  old_gate->~TenantDispatchGate();
  auto* new_gate = new (storage) TenantDispatchGate(limits);
  assert(new_gate->enqueue_data(*old_open.token, 1, 1, 42) == Status::stale);
  new_gate->~TenantDispatchGate();

  // Limits preserve per-tenant CONTROL admission even when DATA is full.
  assert(gate.close(*again.token) == Status::ok);
  auto crowded = gate.open(Kind::trusted_isolated);
  assert(crowded.status == Status::ok);
  for (std::uint64_t i = 1; i <= 5; ++i)
    assert(gate.enqueue_data(*crowded.token, 1, 1, i) == Status::ok);
  assert(gate.enqueue_data(*crowded.token, 1, 1, 7) == Status::exhausted);
  assert(gate.enqueue_control(*crowded.supervisor, 1, 1, 8) == Status::ok);
  assert(gate.enqueue_control(*crowded.supervisor, 1, 1, 9) == Status::ok);
  assert(gate.enqueue_control(*crowded.supervisor, 1, 1, 10) == Status::ok);
  assert(gate.enqueue_control(*crowded.supervisor, 1, 1, 11) == Status::exhausted);
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
        const auto s = gate.enqueue_data(*live.token, static_cast<std::uint64_t>(tid + 1),
                                    1, i);
        if (s == Status::ok) ++accepted;
        else assert(s == Status::exhausted || s == Status::stale);
      }
    });
  }
  assert(gate.close(*live.token) == Status::ok);
  stop = true;
  for (auto& worker : workers) worker.join();
  assert(gate.enqueue_control(*live.supervisor, 1, 1, 99) == Status::stale);
  assert(gate.pending() == 0);
  (void)accepted;
  return 0;
}
