#include "runtime/BatchedCooperation.hpp"
#include "compiler/ir.hpp"
#include <stdexcept>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>
using oresforge::runtime::BatchedCooperation;
// Keep every invariant live under release optimization and -DNDEBUG.
#define REQUIRE(condition) do { \
  if (!(condition)) throw std::runtime_error("batched cooperation invariant failed: " #condition); \
} while (false)

int main() {
  for (auto interval : {128u, 512u}) {
    BatchedCooperation meter(interval);
    unsigned polls = 0;
    for (unsigned i = 1; i <= 4096; ++i) {
      const bool due = meter.commit_batch(1, [&](std::uint64_t n, bool overdue) {
        REQUIRE(n == i);
        REQUIRE(overdue == (i >= 512));
        ++polls;
      });
      REQUIRE(due == (i % interval == 0));
    }
    REQUIRE(polls == 4096 / interval);
    REQUIRE(meter.completed() == 4096);
    meter.begin_dispatch();
    REQUIRE(meter.completed() == 0 && meter.phase() == 0);
  }
  {
    BatchedCooperation meter;
    unsigned polls = 0;
    meter.commit_batch(127, [&](auto,auto) { ++polls; });
    meter.commit_batch(128, [&](auto,auto) { ++polls; });
    REQUIRE(polls == 1 && meter.phase() == 127);
    meter.commit_batch(1, [&](auto,auto) { ++polls; });
    REQUIRE(polls == 2 && meter.phase() == 0);
    bool denied = false;
    try {meter.commit_batch(129, [&](auto,auto) {});}
    catch (const std::invalid_argument&) {denied = true;}
    REQUIRE(denied && meter.completed() == 256);
  }
  {
    oresforge::Program p;
    for (unsigned i = 0; i < 640; ++i)
      p.instructions.push_back({oresforge::Op::Constant, 42, -1, -1});
    p.instructions.push_back({oresforge::Op::Return, 0, 639, -1});
    std::vector<std::uint64_t> observed;
    const auto value = oresforge::interpret_metered(p, {}, [&](std::uint64_t n,bool overdue) {
      REQUIRE(overdue == (n >= 512));
      observed.push_back(n);
    });
    REQUIRE(value == 42);
    REQUIRE((observed == std::vector<std::uint64_t>{128,256,384,512,640}));
    REQUIRE(oresforge::interpret(p) == 42);
  }
}
