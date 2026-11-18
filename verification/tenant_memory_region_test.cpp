#include "runtime/tenant_memory_region.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>
using oresforge::runtime::TenantMemoryRegion;
template<class F> bool rejected(F&& f) { try { f(); } catch (const std::exception&) { return true; } return false; }
int main() {
  static_assert(!std::is_default_constructible_v<TenantMemoryRegion::Lease>);
  static_assert(!std::is_constructible_v<TenantMemoryRegion::Lease, std::uint32_t, std::uint32_t, bool>);
  assert(rejected([] { TenantMemoryRegion r{0}; }));
  assert(rejected([] { TenantMemoryRegion r{16u*1024u*1024u + 1}; }));
  TenantMemoryRegion a{64}, b{64};
  auto aw = a.grant(8, 16, true), ar = a.grant(8, 16, false), bw = b.grant(8, 16, true);
  const std::array<std::uint8_t, 3> payload{42, 0, 7};
  a.write(aw, 1, payload);
  assert((a.read(ar, 1, 3) == std::vector<std::uint8_t>{42, 0, 7}));
  assert((b.read(bw, 1, 3) == std::vector<std::uint8_t>{0, 0, 0}));
  assert(rejected([&] { b.read(aw, 1, 3); }));
  assert(rejected([&] { b.write(aw, 0, payload); }));
  assert(rejected([&] { a.write(ar, 0, payload); }));
  assert(rejected([&] { a.grant(63, 2, true); }));
  assert(rejected([&] { a.grant(UINT32_MAX, 1, true); }));
  assert(rejected([&] { a.grant(1, UINT32_MAX, true); }));
  assert(rejected([&] { a.read(ar, UINT32_MAX, 1); }));
  assert(rejected([&] { a.read(ar, 15, 2); }));
  assert(rejected([&] { a.write(aw, 16, payload); }));
  // Distinct lanes and active leases can be used from multiple host carriers.
  auto root = a.grant(0, 64, true);
  std::array<std::thread, 4> threads;
  for (int i = 0; i != 4; ++i) threads[i] = std::thread([&, i] {
    std::array<std::uint8_t, 8> chunk{};
    chunk.fill(static_cast<std::uint8_t>(10+i));
    for (int n = 0; n != 1000; ++n) a.write(root, static_cast<std::uint32_t>(i*8), chunk);
  });
  for (auto& t : threads) t.join();
  for (int i = 0; i != 4; ++i) {
    auto chunk = a.read(root, static_cast<std::uint32_t>(i*8), 8);
    assert(std::all_of(chunk.begin(), chunk.end(), [i](auto c) { return c == 10+i; }));
  }
  a.retire();
  assert(rejected([&] { a.read(aw, 0, 1); }));
  assert(rejected([&] { a.grant(0, 1, true); }));
  assert(rejected([&] { a.write(aw, 0, payload); }));
  assert(rejected([&] { a.retire(); }));
  TenantMemoryRegion a2{64};
  assert(rejected([&] { a2.read(aw, 0, 1); }));
  assert((a2.read(a2.grant(8, 16, false), 0, 3) == std::vector<std::uint8_t>{0, 0, 0}));
  // Retiring A cannot corrupt independent B, including the original lease.
  assert((b.read(bw, 1, 3) == std::vector<std::uint8_t>{0, 0, 0}));
}
