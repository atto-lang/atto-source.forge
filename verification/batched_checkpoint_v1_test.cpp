#include "runtime/BatchedCheckpointV1.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

using oresforge::runtime::BatchedCheckpointV1;
static void require(bool ok) { if (!ok) throw std::runtime_error("batched checkpoint invariant failed"); }
static_require(!std::is_copy_constructible_v<BatchedCheckpointV1>);
static_require(!std::is_move_constructible_v<BatchedCheckpointV1>);

template <typename Fn> static bool throws(Fn fn) {
  try { fn(); } catch (const std::exception&) { return true; }
  return false;
}
int main() {
  for (auto interval : {64u, 128u, 256u, 512u}) {
    std::vector<std::uint64_t> charges;
    BatchedCheckpointV1 q(interval * 3, [&](auto charged, auto) { charges.push_back(charged); }, interval);
    for (std::uint32_t i = 1; i < interval; ++i) {
      require(!q.completedWorkUnit());
      require(charges.empty());
      require(q.remainingFuel() == interval * 3 - i);
    }
    require(q.completedWorkUnit());
    require((charges == std::vector<std::uint64_t>{interval}));
  }
  std::vector<std::uint64_t> charges;
  BatchedCheckpointV1 q(129, [&](auto charged, auto) { charges.push_back(charged); });
  for (int i = 0; i < 128; ++i) q.completedWorkUnit();
  require(q.remainingFuel() == 1);
  require(q.completedWorkUnit());
  require(q.remainingFuel() == 0);
  require((charges == std::vector<std::uint64_t>{128, 1}));
  require(throws([&] { q.completedWorkUnit(); }));
  require(throws([&] { q.completedWorkUnit(); }));

  BatchedCheckpointV1 zero(0, [](auto, auto) {});
  require(throws([&] { zero.completedWorkUnit(); }));
  BatchedCheckpointV1 huge(std::numeric_limits<std::uint64_t>::max(), [](auto, auto) {}, 512);
  for (int i = 0; i < 512; ++i) huge.completedWorkUnit();
  require(huge.remainingFuel() == std::numeric_limits<std::uint64_t>::max() - 512);
  require(throws([] { BatchedCheckpointV1 bad(4, [](auto, auto) {}, 100); }));
  require(throws([] { BatchedCheckpointV1 bad(4, {}); }));
  BatchedCheckpointV1 error(1, [](auto, auto) { throw std::runtime_error("cancelled"); });
  require(throws([&] { error.completedWorkUnit(); }));
  require(throws([&] { error.completedWorkUnit(); }));
  BatchedCheckpointV1* ref = nullptr;
  BatchedCheckpointV1 reentrant(64, [&](auto, auto) { ref->completedWorkUnit(); }, 64);
  ref = &reentrant;
  for (int i = 0; i < 63; ++i) reentrant.completedWorkUnit();
  require(throws([&] { reentrant.completedWorkUnit(); }));
  require(throws([&] { reentrant.completedWorkUnit(); }));
}
