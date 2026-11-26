#include "runtime/MonotonicDomainV1.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using oresforge::runtime::MonotonicDomainV1;
static void check(bool ok) {
  if (!ok) throw std::runtime_error("domain issuance invariant");
}

int main() {
  constexpr std::size_t threads = 8, per_thread = 12500;
  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  MonotonicDomainV1 normal;
  std::vector<std::uint64_t> issued(threads * per_thread);
  std::array<std::thread, threads> workers;
  for (std::size_t i = 0; i < threads; ++i) {
    workers[i] = std::thread([&, i] {
      for (std::size_t j = 0; j < per_thread; ++j)
        issued[i * per_thread + j] = normal.try_issue();
    });
  }
  for (auto& w : workers) w.join();
  std::sort(issued.begin(), issued.end());
  for (std::size_t i = 0; i < issued.size(); ++i) check(issued[i] == i + 1);

  MonotonicDomainV1 near_end(max - 65);
  std::array<std::uint64_t, threads * 128> boundary{};
  for (std::size_t i = 0; i < threads; ++i) {
    workers[i] = std::thread([&, i] {
      for (std::size_t j = 0; j < 128; ++j)
        boundary[i * 128 + j] = near_end.try_issue();
    });
  }
  for (auto& w : workers) w.join();
  std::sort(boundary.begin(), boundary.end());
  constexpr auto denied = boundary.size() - 64;
  for (std::size_t i = 0; i < denied; ++i) check(boundary[i] == 0);
  for (std::size_t i = 0; i < 64; ++i) check(boundary[denied + i] == max - 64 + i);
  for (std::size_t i = 0; i < 100000; ++i) check(near_end.try_issue() == 0);
  MonotonicDomainV1 exhausted(max);
  check(exhausted.try_issue() == 0);
  std::cout << "PASS DOMAIN_ISSUED=100000 BOUNDARY_ISSUED=64 BOUNDARY_DENIED=960 PERMANENT_DENIALS=100000\n";
}
