#include <atto/ownership_v1.hpp>
#include <cassert>
#include <atomic>
#include <thread>
#include <utility>
using namespace atto::ownership_v1;
static Graph cycle() {
  return {1, {{1, EdgeKind::plain, {1}, {2, 2}}, {2, EdgeKind::plain, {2}, {1}}}};
}
int main() {
  assert(checked_graph(cycle()));
  auto bad = cycle(); bad.nodes[1].kind = EdgeKind::foreign_pointer;
  assert(!checked_graph(bad));
  auto absent = cycle(); absent.nodes[0].edges.push_back(999);
  assert(!checked_graph(absent));
  assert(!checked_graph(cycle(), Limits{10,1,10}));
  auto original = UniqueGraph(7, cycle());
  auto copy = original.deep_copy(); assert(copy && copy->read());
  assert(copy->read()->nodes[0].edges == original.read()->nodes[0].edges);
  assert(copy->read()->nodes[0].bytes.data() != original.read()->nodes[0].bytes.data());
  copy->mutate()->nodes[1].bytes[0] = 99;
  assert(original.read()->nodes[1].bytes[0] == 2);
  auto frozen = std::move(original).freeze(); assert(frozen);
  assert(!original.read());
  auto permit = frozen->permit();
  auto shared = frozen->share();
  auto weak = frozen->weak();
  auto upgraded = weak.upgrade(permit); assert(upgraded);
  assert(upgraded->read(permit, [](const Graph& g) { return g.nodes.size(); }) == 2);
  auto second = std::move(*copy).freeze(); assert(second);
  assert(!weak.upgrade(second->permit())); // same domain != same generation
  frozen->retire();
  assert(!weak.upgrade(permit));
  assert(!shared.read(permit, [](const Graph& g) { return g.nodes.size(); }));
  auto pin = [] {
    Pinned<std::vector<int>> handle(std::vector<int>{42,7});
    auto address = handle.get();
    auto moved = std::move(handle);
    assert(address == moved.get());
    return moved;
  }();
  const auto address = pin.get();
  auto worker = std::thread([p = std::move(pin), address]() mutable {
    assert(p.get() == address); assert(p.get()->at(0) == 42);
  });
  worker.join();
  struct Tracked {
    std::atomic<int>* drops;
    Tracked(std::atomic<int>* p): drops(p) {}
    Tracked(Tracked&& other) noexcept : drops(std::exchange(other.drops, nullptr)) {}
    ~Tracked() { if (drops) drops->fetch_add(1); }
  };
  std::atomic<int> drops{0};
  {
    OwnedResource<Tracked> r{Tracked(&drops)};
    assert(r.alive()); r.drop_now(); assert(!r.alive());
    r.drop_now(); assert(drops.load() == 1);
  }
  assert(drops.load() == 1);
}
