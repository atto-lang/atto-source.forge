#include "runtime/BatchedCheckpointV1.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

using oresforge::runtime::BatchedCheckpointV1;
static_assert(!std::is_copy_constructible_v<BatchedCheckpointV1>);
static_assert(!std::is_move_constructible_v<BatchedCheckpointV1>);

template <typename Fn> static bool throws(Fn fn) {
  try { fn(); } catch (const std::exception&) { return true; }
  return false;
}
int main() {
  for (auto interval : {64u, 128u, 256u, 512u}) {
    std::vector<std::uint64_t> charges;
    BatchedCheckpointV1 q(interval * 3, [&](auto charged, auto) { charges.push_back(charged); }, interval);
    for (std::uint32_t i = 1; i < interval; ++i) {
      assert(!q.completedWorkUnit());
      assert(charges.empty());
      assert(q.remainingFuel() == interval * 3 - i);
    }
    assert(q.completedWorkUnit());
    assert((charges == std::vector<std::uint64_t>{interval}));
  }
  std::vector<std::uint64_t> charges;
  BatchedCheckpointV1 q(129, [&](auto charged, auto) { charges.push_back(charged); });
  for (int i = 0; i < 128; ++i) q.completedWorkUnit();
  assert(q.remainingFuel() == 1);
  assert(q.completedWorkUnit());
  assert(q.remainingFuel() == 0);
  assert((charges == std::vector<std::uint64_t>{128, 1}));
  assert(throws([&] { q.completedWorkUnit(); }));
  assert(throws([&] { q.completedWorkUnit(); }));

  BatchedCheckpointV1 zero(0, [](auto, auto) {});
  assert(throws([&] { zero.completedWorkUnit(); }));
  BatchedCheckpointV1 huge(std::numeric_limits<std::uint64_t>::max(), [](auto, auto) {}, 512);
  for (int i = 0; i < 512; ++i) huge.completedWorkUnit();
  assert(huge.remainingFuel() == std::numeric_limits<std::uint64_t>::max() - 512);
  assert(throws([] { BatchedCheckpointV1 bad(4, [](auto, auto) {}, 100); }));
  assert(throws([] { BatchedCheckpointV1 bad(4, {}); }));
  BatchedCheckpointV1 error(1, [](auto, auto) { throw std::runtime_error("cancelled"); });
  assert(throws([&] { error.completedWorkUnit(); }));
  assert(throws([&] { error.completedWorkUnit(); }));
  BatchedCheckpointV1* ref = nullptr;
  BatchedCheckpointV1 reentrant(64, [&](auto, auto) { ref->completedWorkUnit(); }, 64);
  ref = &reentrant;
  for (int i = 0; i < 63; ++i) reentrant.completedWorkUnit();
  assert(throws([&] { reentrant.completedWorkUnit(); }));
  assert(throws([&] { reentrant.completedWorkUnit(); }));
}
