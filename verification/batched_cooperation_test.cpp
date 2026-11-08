#include "runtime/BatchedCooperation.hpp"
#include "compiler/ir.hpp"
#include <cassert>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>
using oresforge::runtime::BatchedCooperation;

int main() {
  for (auto interval : {128u, 512u}) {
    BatchedCooperation meter(interval);
    unsigned polls = 0;
    for (unsigned i = 1; i <= 4096; ++i) {
      const bool due = meter.commit_batch(1, [&](std::uint64_t n, bool overdue) {
        assert(n == i);
        assert(overdue == (i >= 512));
        ++polls;
      });
      assert(due == (i % interval == 0));
    }
    assert(polls == 4096 / interval);
    assert(meter.completed() == 4096);
    meter.begin_dispatch();
    assert(meter.completed() == 0 && meter.phase() == 0);
  }
  {
    BatchedCooperation meter;
    unsigned polls = 0;
    meter.commit_batch(127, [&](auto,auto) { ++polls; });
    meter.commit_batch(128, [&](auto,auto) { ++polls; });
    assert(polls == 1 && meter.phase() == 127);
    meter.commit_batch(1, [&](auto,auto) { ++polls; });
    assert(polls == 2 && meter.phase() == 0);
    bool denied = false;
    try {meter.commit_batch(129, [&](auto,auto) {});}
    catch (const std::invalid_argument&) {denied = true;}
    assert(denied && meter.completed() == 256);
  }
  {
    oresforge::Program p;
    for (unsigned i = 0; i < 640; ++i)
      p.instructions.push_back({oresforge::Op::Constant, 42, -1, -1});
    p.instructions.push_back({oresforge::Op::Return, 0, 639, -1});
    std::vector<std::uint64_t> observed;
    const auto value = oresforge::interpret_metered(p, {}, [&](std::uint64_t n,bool overdue) {
      assert(overdue == (n >= 512));
      observed.push_back(n);
    });
    assert(value == 42);
    assert((observed == std::vector<std::uint64_t>{128,256,384,512,640}));
    assert(oresforge::interpret(p) == 42);
  }
}
