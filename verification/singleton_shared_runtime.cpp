#include "runtime/shared_i64_table.hpp"
#include "runtime/singleton_cell.hpp"
#include <atomic>
#include <barrier>
#include <iostream>
#include <latch>
#include <limits>
#include <thread>
#include <vector>

using namespace oresforge::shared;
using oresforge::runtime::Mailbox;
namespace {
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template<class F> void error(TableErrorCode code, F operation) {
  try { operation(); }
  catch (const TableError& e) { check(e.code == code, "wrong table error"); return; }
  throw std::runtime_error("table operation unexpectedly succeeded");
}
template<class E, class F> void throws(F operation) {
  try { operation(); }
  catch (const E&) { return; }
  throw std::runtime_error("expected exception");
}
void table_authority() {
  Mailbox owner_box{1};
  Mailbox reader_box{2};
  Mailbox foreign_box{2, 1024, 4 * oresforge::runtime::kMaxMessageBytes, 2};
  const auto owner = owner_box.context();
  const auto reader = reader_box.context();
  const auto foreign = foreign_box.context();
  SharedI64Table<2> table(owner);
  const auto own = table.owner_capability(owner);
  const auto read = table.delegate(own, owner, reader);
  table.insert(own, owner, 7, 10);
  auto copy = table.lookup(read, reader, 7);
  copy = 99;
  check(table.lookup(read, reader, 7) == 10, "lookup aliases storage");
  check(!table.lookup(read, reader, 8), "missing key");
  error(TableErrorCode::Denied, [&] { table.insert(read, reader, 7, 11); });
  (void)reader_box.restart();
  const auto restarted = reader_box.context();
  check(!reader.valid() && restarted.valid(), "actor restart did not invalidate old context");
  error(TableErrorCode::Denied, [&] { table.lookup(read, reader, 7); });
  error(TableErrorCode::Denied, [&] { table.lookup(read, restarted, 7); });
  error(TableErrorCode::Denied, [&] { table.lookup(own, reader, 7); });
  error(TableErrorCode::Denied, [&] { table.lookup({}, reader, 7); });
  error(TableErrorCode::Denied, [&] { table.delegate(own, owner, reader, true); });
  error(TableErrorCode::Denied, [&] { table.delegate(own, owner, foreign); });
  error(TableErrorCode::Denied, [&] { table.delegate(read, reader, reader); });
  error(TableErrorCode::Denied, [&] { table.owner_capability(reader); });
  error(TableErrorCode::Denied, [&] { table.close(read, reader); });
  error(TableErrorCode::Denied, [&] { table.owner_stopped(restarted); });
  SharedI64Table<2> other(owner);
  error(TableErrorCode::Denied, [&] { other.lookup(own, owner, 7); });
  SharedI64Table<2> priv(owner, TableAccess::Private);
  const auto private_cap = priv.owner_capability(owner);
  error(TableErrorCode::Denied, [&] { priv.delegate(private_cap, owner, reader); });
  error(TableErrorCode::Denied, [&] { SharedI64Table<2> invalid({0, 1, 1}); });
  error(TableErrorCode::Denied, [&] { SharedI64Table<2> invalid(owner, static_cast<TableAccess>(9)); });
  table.owner_stopped(owner);
  error(TableErrorCode::Closed, [&] { table.lookup(read, reader, 7); });
  error(TableErrorCode::Closed, [&] { table.insert(own, owner, 7, 20); });
  error(TableErrorCode::Closed, [&] { table.delegate(own, owner, reader); });
  table.close(own, owner);
  table.close(own, owner);
  // New table identity never revives an old authority, even for the same owner.
  SharedI64Table<2> replacement(owner);
  error(TableErrorCode::Denied, [&] { replacement.lookup(read, reader, 7); });
  SharedI64Table<2>::Capability expired;
  {
    SharedI64Table<2> retired(owner);
    expired = retired.owner_capability(owner);
  }
  error(TableErrorCode::Denied, [&] { replacement.lookup(expired, owner, 7); });
}

void table_atomic_failures() {
  Mailbox owner_box{1};
  const auto owner = owner_box.context();
  SharedI64Table<2> table(owner);
  const auto cap = table.owner_capability(owner);
  check(table.insert_new(cap, owner, 1, 10), "new insert");
  check(!table.insert_new(cap, owner, 1, 20), "duplicate insert");
  table.insert(cap, owner, 2, 30);
  error(TableErrorCode::Capacity, [&] { table.insert(cap, owner, 3, 40); });
  error(TableErrorCode::Capacity, [&] { table.insert_new(cap, owner, 3, 40); });
  check(table.lookup(cap, owner, 1) == 10 && table.lookup(cap, owner, 2) == 30,
        "capacity failure changed contents");
  table.insert(cap, owner, 2, 31); // Replacement works at capacity.
  check(table.compare_exchange(cap, owner, 2, 31, 32), "CAS success");
  check(!table.compare_exchange(cap, owner, 2, 31, 99), "CAS mismatch");
  check(!table.compare_exchange(cap, owner, 9, 0, 99), "CAS missing");
  table.insert(cap, owner, 1, std::numeric_limits<std::int64_t>::max());
  error(TableErrorCode::Overflow, [&] { table.update_counter(cap, owner, 1, 1); });
  check(table.lookup(cap, owner, 1) == std::numeric_limits<std::int64_t>::max(), "overflow changed value");
  table.insert(cap, owner, 1, std::numeric_limits<std::int64_t>::min());
  error(TableErrorCode::Overflow, [&] { table.update_counter(cap, owner, 1, -1); });
  check(table.lookup(cap, owner, 1) == std::numeric_limits<std::int64_t>::min(), "underflow changed value");
  check(table.update_counter(cap, owner, 1, std::numeric_limits<std::int64_t>::max()) == -1,
        "wide positive delta");
  table.insert(cap, owner, 1, 0);
  check(table.update_counter(cap, owner, 1, std::numeric_limits<std::int64_t>::min()) ==
        std::numeric_limits<std::int64_t>::min(), "minimum delta");
  error(TableErrorCode::Missing, [&] { table.update_counter(cap, owner, 9, 1); });
  check(table.erase(cap, owner, 1) && !table.erase(cap, owner, 1), "erase");
  check(table.insert_new(cap, owner, -1, 42), "erased slot not reusable");
}

void table_concurrency() {
  constexpr int threads = 8, rounds = 10000;
  Mailbox owner_box{1};
  const auto owner = owner_box.context();
  SharedI64Table<4> table(owner, TableAccess::Public);
  const auto own = table.owner_capability(owner);
  table.insert(own, owner, 0, 0);
  std::barrier start(threads);
  std::atomic<int> insert_winners{0}, cas_winners{0};
  std::vector<std::unique_ptr<Mailbox>> actor_boxes;
  actor_boxes.reserve(threads);
  std::vector<std::thread> workers;
  for (int i = 0; i < threads; ++i) {
    actor_boxes.push_back(std::make_unique<Mailbox>(
        static_cast<std::uint64_t>(i + 2)));
    const auto actor = actor_boxes.back()->context();
    const auto cap = table.delegate(own, owner, actor, true);
    workers.emplace_back([&, actor, cap] {
      start.arrive_and_wait();
      for (int j = 0; j < rounds; ++j) table.update_counter(cap, actor, 0, 1);
      if (table.insert_new(cap, actor, 1, 0)) ++insert_winners;
      if (table.compare_exchange(cap, actor, 1, 0, 1)) ++cas_winners;
    });
  }
  for (auto& worker : workers) worker.join();
  check(table.lookup(own, owner, 0) == threads * rounds, "lost concurrent increments");
  check(insert_winners == 1 && cas_winners == 1, "nonlinearizable insert/CAS");
  // Retirement races with admitted operations under the same table lock.
  std::barrier retire_start(2);
  std::atomic<int> unexpected{0};
  Mailbox reader_box{100};
  const auto reader = reader_box.context();
  const auto read = table.delegate(own, owner, reader);
  std::thread reader_thread([&] {
    retire_start.arrive_and_wait();
    for (int i = 0; i < 10000; ++i) {
      try {
        if (table.lookup(read, reader, 0) != threads * rounds) ++unexpected;
      } catch (const TableError& e) {
        if (e.code != TableErrorCode::Closed) ++unexpected;
      }
    }
  });
  retire_start.arrive_and_wait();
  table.owner_stopped(owner);
  reader_thread.join();
  check(unexpected == 0, "retirement exposed partial state");
}

void singleton_once_and_failure() {
  SingletonCell<std::array<int, 2>> cell;
  std::atomic<int> initialized{0}, failures{0};
  std::barrier start(8);
  std::array<std::shared_ptr<const std::array<int, 2>>, 8> values{};
  std::vector<std::thread> workers;
  for (int i = 0; i < 8; ++i) workers.emplace_back([&, i] {
    start.arrive_and_wait();
    try { values[i] = cell.get([&] { ++initialized; return std::array<int, 2>{21, 42}; }); }
    catch (...) { ++failures; }
  });
  for (auto& worker : workers) worker.join();
  check(initialized == 1 && failures == 0, "singleton initialized more than once");
  for (const auto& value : values)
    check(value == values[0] && (*value)[0] == 21 && (*value)[1] == 42, "partial/duplicate publication");
  cell.close();
  check((*values[0])[1] == 42, "reader lease invalidated by close");
  throws<SingletonClosed>([&] { cell.get([] { return std::array<int, 2>{}; }); });
  SingletonCell<int> failed;
  int attempts = 0;
  for (int i = 0; i < 2; ++i)
    throws<std::logic_error>([&] { failed.get([&]() -> int { ++attempts; throw std::logic_error("init failure"); }); });
  check(attempts == 1, "failed initializer retried");
  SingletonCell<int> recursive;
  throws<SingletonInitializationDependency>([&] {
    recursive.get([&] { return *recursive.get([] { return 1; }); });
  });
  // Failure must restore per-thread initialization state for unrelated cells.
  SingletonCell<int> next;
  check(*next.get([] { return 42; }) == 42, "initializer depth leaked");
  SingletonCell<long> unpublished;
  SingletonCell<int> dependent;
  throws<SingletonInitializationDependency>([&] {
    dependent.get([&] { return static_cast<int>(*unpublished.get([] { return 3L; })); });
  });
  // A ready dependency is permitted, including across template types.
  check(*unpublished.get([] { return 9L; }) == 9, "nested rejection initialized dependency");
  SingletonCell<int> ready_dependency;
  check(*ready_dependency.get([&] { return static_cast<int>(*unpublished.get([] { return 0L; })); }) == 9,
        "ready dependency rejected");
  SingletonCell<int> closed_cold;
  closed_cold.close();
  throws<SingletonClosed>([&] { closed_cold.get([] { throw std::logic_error("must not run"); return 0; }); });
}

void singleton_dependencies_and_close() {
  SingletonCell<int> a, b;
  std::barrier both_initializing(2);
  std::atomic<int> rejected{0};
  auto run = [&](SingletonCell<int>& current, SingletonCell<int>& dependency) {
    try {
      current.get([&] {
        both_initializing.arrive_and_wait();
        return *dependency.get([] { return 7; });
      });
    } catch (const SingletonInitializationDependency&) { ++rejected; }
  };
  std::thread first([&] { run(a, b); }), second([&] { run(b, a); });
  first.join(); second.join();
  check(rejected == 2, "cross-initializer cycle did not fail");
  SingletonCell<int> closing;
  std::latch entered(1), release(1);
  std::atomic<bool> saw_closed{false};
  std::thread initializer([&] {
    try { closing.get([&] { entered.count_down(); release.wait(); return 42; }); }
    catch (const SingletonClosed&) { saw_closed = true; }
  });
  entered.wait();
  closing.close(); closing.close();
  release.count_down();
  initializer.join();
  check(saw_closed, "closed singleton published a value");
  throws<SingletonClosed>([&] { closing.get([] { return 9; }); });
  // The initializer and payload destructors may reenter close(): neither may
  // execute while the cell's internal mutex is held.
  SingletonCell<int> self_closing;
  throws<SingletonClosed>([&] { self_closing.get([&] { self_closing.close(); return 1; }); });
  struct Payload { std::shared_ptr<int> token; };
  SingletonCell<Payload> cleanup;
  int destroyed = 0;
  (void)cleanup.get([&] {
    return Payload{std::shared_ptr<int>(new int(42), [&](int* p) {
      ++destroyed;
      cleanup.close();
      delete p;
    })};
  });
  cleanup.close();
  check(destroyed == 1, "cleanup did not run exactly once");
}
}

int main() {
  try {
    table_authority();
    table_atomic_failures();
    table_concurrency();
    singleton_once_and_failure();
    singleton_dependencies_and_close();
    std::cout << "PASS: authority, capacity, overflow, 80000 atomic increments, insert/CAS winners, retirement, once publication, sticky failure, cycles and close\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
