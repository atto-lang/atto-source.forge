#include "runtime/actor_arena.hpp"
#include <array>
#include <algorithm>
#include <vector>
#include <limits>
#include <cstddef>
#include <stdexcept>
#include <exception>
#include <cstdlib>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <thread>
#include <atomic>
#include <thread>
using oresforge::runtime::ActorArena;
static void ensure(bool ok) { if (!ok) throw std::runtime_error("actor arena invariant failed"); }
template <typename F> static void rejects(F&& f) {
  bool caught = false;
  try { f(); } catch (const std::exception&) { caught = true; }
  ensure(caught);
}
#if defined(__unix__) || defined(__APPLE__)
// Test the *actual* native destructor in a child: a correctly failed-closed
// destructor terminates the process and cannot be caught in this test runner.
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
#if defined(__unix__) || defined(__APPLE__)
  ensure(destruction_outcome(0) == 77); // an unretired arena is rejected
  ensure(destruction_outcome(1) == 77); // an active turn blocks destruction
  ensure(destruction_outcome(2) == 0);  // retired, idempotent cleanup succeeds
#endif
  // Saturating domain IDs may never be recycled under concurrent admission.
  // Test the compiled native arena allocation path directly.
  {
    constexpr std::size_t Threads = 8, PerThread = 256;
    std::array<std::array<std::uint64_t, PerThread>, Threads> issued{};
    std::array<std::thread, Threads> workers;
    std::atomic<bool> start{false};
    for (std::size_t t = 0; t < Threads; ++t) {
      workers[t] = std::thread([&, t] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (std::size_t i = 0; i < PerThread; ++i) {
          ActorArena domain(1000 + t, 1);
          auto turn = domain.begin_turn(1000 + t, 1);
          issued[t][i] = turn.domain;
          domain.end_turn(turn);
          domain.retire();
        }
      });
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    std::vector<std::uint64_t> values;
    values.reserve(Threads * PerThread);
    for (const auto& row : issued) for (auto identity : row) {
      ensure(identity != 0 && identity != std::numeric_limits<std::uint64_t>::max());
      values.push_back(identity);
    }
    std::sort(values.begin(), values.end());
    ensure(std::adjacent_find(values.begin(), values.end()) == values.end());
  }
  ActorArena a(10, 1), b(11, 1);
  rejects([&] { (void)a.begin_turn(10, 2); }); // refuse wrong incarnation before admission
  auto ta = a.begin_turn(10, 1);
  rejects([&] { (void)a.begin_turn(10, 1); }); // no overlapping actor turns
  auto tb = b.begin_turn(11, 1);
  auto h = a.allocate(ta, 2);
  std::array<std::byte, 2> value{std::byte{42}, std::byte{9}}, output{};
  a.write(ta, h, value);
  rejects([&] { b.read(tb, h, output); });
  ActorArena twin(10, 1);
  auto tt = twin.begin_turn(10, 1);
  rejects([&] { twin.read(tt, h, output); }); // same actor/incarnation, distinct domain
  twin.end_turn(tt);
  twin.retire();
  rejects([&] { a.read(tb, h, output); });
  std::thread foreign([&] { rejects([&] { a.read(ta, h, output); }); });
  foreign.join();
  a.read(ta, h, output);
  ensure(output == value);
  auto scratch = a.scratch_alloc(ta, 2);
  a.scratch_write(ta, scratch, value);
  // A scratch handle must identify an issued allocation, not an arbitrary
  // in-bounds region of scratch memory.
  auto subrange = scratch;
  ++subrange.offset;
  subrange.length = 1;
  std::array<std::byte, 1> one{};
  rejects([&] { a.scratch_read(ta, subrange, one); });
  auto shortened = scratch;
  shortened.length = 1;
  rejects([&] { a.scratch_read(ta, shortened, one); });
  a.scratch_read(ta, scratch, output);
  ensure(output == value);
  // A stolen turn token cannot be used to end the owner's turn or mutate
  // scratch from a different physical carrier.
  std::atomic<bool> foreignEndDenied{false}, foreignScratchDenied{false};
  std::thread intruder([&] {
    try { a.end_turn(ta); }
    catch (const std::logic_error&) { foreignEndDenied.store(true); }
    try { a.scratch_write(ta, scratch, value); }
    catch (const std::logic_error&) { foreignScratchDenied.store(true); }
  });
  intruder.join();
  ensure(foreignEndDenied.load() && foreignScratchDenied.load());
  a.scratch_read(ta, scratch, output);
  ensure(output == value);
  a.end_turn(ta);
  b.end_turn(tb);
  // Every arena must explicitly pass CONTROL retirement before destruction.
  b.retire();
  b.retire(); // retired teardown is idempotent
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
}
