#include "runtime/actor_arena.hpp"
#include <array>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <type_traits>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>
#include <cstdlib>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif
using oresforge::runtime::ActorArena;
// No host caller can manufacture a valid-looking arena authority from the
// observable domain/slot/generation values. Handles remain copyable once
// issued, so mailbox continuation code can carry them across turns.
static_assert(!std::is_aggregate_v<ActorArena::Turn>);
static_assert(!std::is_aggregate_v<ActorArena::Handle>);
static_assert(!std::is_aggregate_v<ActorArena::Scratch>);
static_assert(!std::is_default_constructible_v<ActorArena::Turn>);
static_assert(!std::is_default_constructible_v<ActorArena::Handle>);
static_assert(!std::is_default_constructible_v<ActorArena::Scratch>);
static_assert(!std::is_constructible_v<ActorArena::Turn, std::uint64_t, std::uint64_t>);
static_assert(!std::is_constructible_v<ActorArena::Handle, std::uint64_t, std::uint64_t, std::size_t>);
static_assert(!std::is_constructible_v<ActorArena::Scratch, std::uint64_t, std::uint64_t, std::size_t, std::size_t>);
static_assert(std::is_copy_constructible_v<ActorArena::Turn>);
static_assert(std::is_copy_constructible_v<ActorArena::Handle>);
static_assert(std::is_copy_constructible_v<ActorArena::Scratch>);
static_assert(!std::is_copy_constructible_v<ActorArena::ExternalLease>);
static_assert(std::is_move_constructible_v<ActorArena::ExternalLease>);
static void ensure(bool ok) { if (!ok) throw std::runtime_error("actor arena invariant failed"); }
template <typename F> static void rejects(F&& f) {
  bool caught = false;
  try { f(); } catch (const std::exception&) { caught = true; }
  ensure(caught);
}
#if defined(__unix__) || defined(__APPLE__)
static int destruction_outcome(int scenario) {
  const pid_t pid = fork();
  ensure(pid >= 0);
  if (pid == 0) {
    std::set_terminate([] { std::_Exit(77); });
    {
      ActorArena arena(1001, 1);
      if (scenario == 1) {
        (void)arena.begin_turn(1001, 1);
      } else if (scenario == 2) {
        arena.retire();
        arena.retire();
      }
    }
    std::_Exit(0);
  }
  int status = 0;
  ensure(waitpid(pid, &status, 0) == pid && WIFEXITED(status));
  return WEXITSTATUS(status);
}
#endif
int main() {
  std::atomic<std::uint64_t> invalid_owner_counter{7};
  rejects([&] { (void)claim_actor_domain(invalid_owner_counter, 0, 1); });
  rejects([&] { (void)claim_actor_domain(invalid_owner_counter, 1, 0); });
  ensure(invalid_owner_counter.load() == 7);
#if defined(__unix__) || defined(__APPLE__)
  ensure(destruction_outcome(0) == 77);
  ensure(destruction_outcome(1) == 77);
  ensure(destruction_outcome(2) == 0);
#endif
  ActorArena a(10, 1), b(11, 1);
  auto ta = a.begin_turn(10, 1);
  auto tb = b.begin_turn(11, 1);
  auto h = a.allocate(ta, 2);
  std::array<std::byte, 2> value{std::byte{42}, std::byte{9}}, output{};
  a.write(ta, h, value);
  rejects([&] { b.read(tb, h, output); });
  rejects([&] { a.read(tb, h, output); });
  std::thread foreign([&] { rejects([&] { a.read(ta, h, output); }); });
  foreign.join();
  a.read(ta, h, output);
  ensure(output == value);
  auto scratch = a.scratch_alloc(ta, 2);
  a.scratch_write(ta, scratch, value);
  // Issued scratch handles are opaque capabilities. Even with a valid handle,
  // wrong-size access must fail without touching its underlying contents.
  std::array<std::byte, 1> one{};
  rejects([&] { a.scratch_read(ta, scratch, one); });
  rejects([&] { a.scratch_write(ta, scratch, one); });
  a.scratch_read(ta, scratch, output);
  ensure(output == value);
  a.end_turn(ta);
  b.end_turn(tb);
  b.retire(); b.retire();
  auto t2 = a.begin_turn(10, 1);
  rejects([&] { a.scratch_read(t2, scratch, output); });
  auto fresh = a.scratch_alloc(t2, 2);
  a.scratch_read(t2, fresh, output);
  ensure(output == (std::array<std::byte, 2>{}));
  a.read(t2, h, output);
  ensure(output == value);
  a.drop(t2, h);
  rejects([&] { a.read(t2, h, output); });
  auto replacement = a.allocate(t2, 2);
  rejects([&] { a.read(t2, h, output); });
  a.read(t2, replacement, output);
  ensure(output == (std::array<std::byte, 2>{}));
  rejects([&] { a.retire(); });
  a.end_turn(t2);
  a.retire(); a.retire();
  rejects([&] { a.begin_turn(10, 1); });
  ActorArena restarted(10, 2);
  auto tr = restarted.begin_turn(10, 2);
  rejects([&] { restarted.read(tr, replacement, output); });
  restarted.end_turn(tr); restarted.retire();
  // Turn authority is carrier-specific and may migrate only after end_turn.
  ActorArena migrating(200, 7);
  auto m0 = migrating.begin_turn(200, 7);
  auto mh = migrating.allocate(m0, 8);
  std::array<std::byte, 8> payload{};
  payload[0] = std::byte{41};
  migrating.write(m0, mh, payload);
  std::atomic<bool> wrongCarrierDenied{false};
  std::thread blocked([&] {
    try { std::array<std::byte, 8> buffer{}; migrating.read(m0, mh, buffer); }
    catch (const std::logic_error&) { wrongCarrierDenied = true; }
  });
  blocked.join();
  ensure(wrongCarrierDenied.load());
  migrating.end_turn(m0);
  std::atomic<bool> migrated{false};
  std::thread nextCarrier([&] {
    auto t = migrating.begin_turn(200, 7);
    std::array<std::byte, 8> copy{};
    migrating.read(t, mh, copy);
    migrated = (copy == payload);
    migrating.end_turn(t);
  });
  nextCarrier.join();
  ensure(migrated.load());
  rejects([&] { migrating.begin_turn(200, 8); });
  rejects([&] { migrating.begin_turn(201, 7); });
  auto m1 = migrating.begin_turn(200, 7);
  // 8192 deterministic generational reallocations ensure stale handles never
  // regain authority and fresh slots have their bytes zeroed.
  for (int i = 0; i < 8192; ++i) {
    auto temp = migrating.allocate(m1, 8);
    migrating.write(m1, temp, payload);
    migrating.drop(m1, temp);
    rejects([&] { std::array<std::byte, 8> result{}; migrating.read(m1, temp, result); });
    auto fresh = migrating.allocate(m1, 8);
    std::array<std::byte, 8> result{};
    migrating.read(m1, fresh, result);
    ensure(result == (std::array<std::byte,8>{}));
    migrating.drop(m1, fresh);
  }
  migrating.end_turn(m1);
  migrating.retire();

  // External lifetime pins model continuations, FFI and I/O callbacks.
  // They grant no arena access, but must delay physical reclamation after
  // logical retirement until the final external owner quiesces.
  ActorArena quiescing(700, 3);
  auto qturn = quiescing.begin_turn(700, 3);
  auto qhandle = quiescing.allocate(qturn, 8);
  quiescing.write(qturn, qhandle, payload);
  auto external = quiescing.acquire_external(qturn);
  ensure(quiescing.outstanding_external_leases() == 1);
  quiescing.end_turn(qturn);
  quiescing.retire();
  ensure(quiescing.retired());
  ensure(!quiescing.physically_reclaimed());
  rejects([&] { (void)quiescing.begin_turn(700, 3); });
  std::thread callbackCarrier([lease = std::move(external)]() mutable {
    lease.close();
    lease.close();
  });
  callbackCarrier.join();
  ensure(quiescing.outstanding_external_leases() == 0);
  ensure(quiescing.physically_reclaimed());
  quiescing.retire();
  // Register a real callback while the actor turn is admitted. The wrapper
  // itself carries only the lifetime lease and releases it after invocation,
  // including when the callback runs on another carrier.
  ActorArena callbackPinned(405, 1);
  auto callbackTurn = callbackPinned.begin_turn(405, 1);
  auto trackedCallback = callbackPinned.track_external_callback(
      callbackTurn, [] { return 23; });
  ensure(callbackPinned.outstanding_external_leases() == 1);
  callbackPinned.end_turn(callbackTurn);
  callbackPinned.retire();
  ensure(callbackPinned.retired());
  ensure(!callbackPinned.physically_reclaimed());

  std::atomic<int> callbackResult{0};
  std::thread completionCarrier([&] { callbackResult.store(trackedCallback()); });
  completionCarrier.join();
  ensure(callbackResult.load() == 23);
  ensure(callbackPinned.outstanding_external_leases() == 0);
  ensure(callbackPinned.physically_reclaimed());
  rejects([&] { (void)trackedCallback(); });


  // The actual runtime's saturating allocator must not wrap or reuse IDs.
  // Drive it with independent counters so global capability domains remain
  // untampered and production actor IDs are not exhausted by this test.
  using oresforge::runtime::detail::claim_actor_domain;
  constexpr auto max_id = std::numeric_limits<std::uint64_t>::max();
  std::atomic<std::uint64_t> near_end{max_id - 1};
  ensure(claim_actor_domain(near_end, 1, 1) == max_id - 1);
  ensure(near_end.load() == max_id);
  rejects([&] { (void)claim_actor_domain(near_end, 1, 1); });
  rejects([&] { (void)claim_actor_domain(near_end, 1, 1); });
  ensure(near_end.load() == max_id);  // must never wrap to zero or one
  std::atomic<std::uint64_t> invalid_zero{0};
  rejects([&] { (void)claim_actor_domain(invalid_zero, 1, 1); });
  ensure(invalid_zero.load() == 0);

  constexpr int Workers = 8, PerWorker = 1250;
  std::atomic<std::uint64_t> concurrent_counter{1};
  std::array<std::vector<std::uint64_t>, Workers> issued{};
  std::array<std::thread, Workers> workers;
  for (int w = 0; w < Workers; ++w) {
    workers[w] = std::thread([&, w] {
      auto& ids = issued[w];
      ids.reserve(PerWorker);
      for (int j = 0; j < PerWorker; ++j)
        ids.push_back(claim_actor_domain(concurrent_counter, 1000 + w, 1));
    });
  }
  for (auto& worker : workers) worker.join();
  std::vector<std::uint64_t> all;
  for (const auto& batch : issued)
    all.insert(all.end(), batch.begin(), batch.end());
  std::sort(all.begin(), all.end());
  ensure(all.size() == static_cast<std::size_t>(Workers * PerWorker));
  for (std::size_t i = 0; i < all.size(); ++i)
    ensure(all[i] == i + 1);
  ensure(concurrent_counter.load() == all.size() + 1);
}
