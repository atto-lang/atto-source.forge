#include "runtime/shared_i64_table.hpp"
#include "runtime/singleton_cell.hpp"
#include "runtime/singleton_registry.hpp"
#include <atomic>
#include <barrier>
#include <iostream>
#include <latch>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>

using namespace oresforge::shared;
using oresforge::runtime::Mailbox;
static_assert(!std::is_constructible_v<ActorContext, std::uint64_t, std::uint64_t,
                                       std::uint64_t,
                                       std::shared_ptr<const oresforge::runtime::CapabilityToken>>,
              "guest/FFI code must not be able to forge ActorContext");
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
template<class F> void registry_error(
    SingletonRegistryErrorCode code, F operation) {
  try { operation(); }
  catch (const SingletonRegistryError& e) {
    check(e.code == code, "wrong singleton registry error");
    return;
  }
  throw std::runtime_error("singleton registry operation unexpectedly succeeded");
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
  error(TableErrorCode::Denied, [&] { SharedI64Table<2> invalid(ActorContext{}); });
  error(TableErrorCode::Denied, [&] { SharedI64Table<2> invalid(owner, static_cast<TableAccess>(9)); });
  // Explicit close is idempotent while the owning actor incarnation is live.
  table.close(own, owner);
  error(TableErrorCode::Denied, [&] { table.lookup(read, restarted, 7); });
  error(TableErrorCode::Closed, [&] { table.lookup(own, owner, 7); });
  error(TableErrorCode::Closed, [&] { table.insert(own, owner, 7, 20); });
  error(TableErrorCode::Closed, [&] { table.delegate(own, owner, restarted); });
  table.close(own, owner);
  table.close(own, owner);

  // New table identity never revives an old authority, even for the same owner.
  SharedI64Table<2> replacement(owner, TableAccess::Public);
  const auto replacement_owner = replacement.owner_capability(owner);
  const auto replacement_read = replacement.delegate(
      replacement_owner, owner, restarted);
  replacement.insert(replacement_owner, owner, 9, 90);
  error(TableErrorCode::Denied, [&] { replacement.lookup(read, restarted, 9); });
  SharedI64Table<2>::Capability expired;
  {
    SharedI64Table<2> retired(owner);
    expired = retired.owner_capability(owner);
  }
  error(TableErrorCode::Denied, [&] { replacement.lookup(expired, owner, 9); });

  // Owner lifecycle is now wired automatically; no manual owner_stopped call.
  owner_box.close();
  check(!owner.valid(), "owner close did not fence actor context");
  error(TableErrorCode::Closed, [&] {
    replacement.lookup(replacement_read, restarted, 9);
  });
}

void actor_context_lifecycle() {
  Mailbox mailbox{50};
  Mailbox reader_box{51};
  const auto first = mailbox.context();
  const auto reader = reader_box.context();
  check(first.valid(), "fresh actor context invalid");

  SharedI64Table<1> first_table(first, TableAccess::Public);
  const auto first_owner = first_table.owner_capability(first);
  const auto first_read = first_table.delegate(first_owner, first, reader);
  first_table.insert(first_owner, first, 1, 41);

  (void)mailbox.restart();
  const auto second = mailbox.context();
  check(!first.valid() && second.valid() && first != second,
        "restart did not fence the old actor incarnation");
  // Restart automatically retires resources owned by the old incarnation.
  error(TableErrorCode::Closed, [&] { first_table.lookup(first_read, reader, 1); });
  error(TableErrorCode::Denied, [&] { (void)first_table.owner_capability(first); });
  error(TableErrorCode::Denied, [&] { first_table.lookup(first_owner, second, 1); });

  SharedI64Table<1> second_table(second, TableAccess::Public);
  const auto second_owner = second_table.owner_capability(second);
  const auto second_read = second_table.delegate(second_owner, second, reader);
  second_table.insert(second_owner, second, 1, 42);
  mailbox.close();
  check(!second.valid(), "close did not fence actor context");
  throws<std::runtime_error>([&] { (void)mailbox.context(); });
  error(TableErrorCode::Closed, [&] { second_table.lookup(second_read, reader, 1); });

  // Destruction is also a lifecycle boundary: an owned table may outlive its
  // mailbox, but it must be retired before the mailbox destructor returns.
  std::unique_ptr<SharedI64Table<1>> survivor;
  SharedI64Table<1>::Capability survivor_read;
  {
    auto short_lived = std::make_unique<Mailbox>(60);
    const auto short_owner = short_lived->context();
    survivor = std::make_unique<SharedI64Table<1>>(
        short_owner, TableAccess::Public);
    const auto short_cap = survivor->owner_capability(short_owner);
    survivor_read = survivor->delegate(short_cap, short_owner, reader);
    survivor->insert(short_cap, short_owner, 5, 55);
  }
  error(TableErrorCode::Closed, [&] {
    survivor->lookup(survivor_read, reader, 5);
  });
}

void table_owner_handoff() {
  Mailbox original_box{80}, successor_box{81}, reader_box{82};
  Mailbox foreign_box{83, 1024, 4 * oresforge::runtime::kMaxMessageBytes, 2};
  const auto original = original_box.context();
  const auto successor = successor_box.context();
  const auto reader = reader_box.context();
  const auto foreign = foreign_box.context();
  SharedI64Table<2> table(original, TableAccess::Protected);
  const auto original_cap = table.owner_capability(original);
  const auto read = table.delegate(original_cap, original, reader);
  table.insert(original_cap, original, 1, 41);
  // A public-table write grant is not an owner/administration grant, even
  // when the grantee happens to be the owner actor.
  SharedI64Table<1> public_table(original, TableAccess::Public);
  const auto public_owner = public_table.owner_capability(original);
  const auto self_write = public_table.delegate(public_owner, original, original, true);
  public_table.insert(self_write, original, 7, 77); // Data access is allowed.
  error(TableErrorCode::Denied, [&] {
    public_table.give_away(self_write, original, successor);
  });
  error(TableErrorCode::Denied, [&] {
    (void)public_table.delegate(self_write, original, reader);
  });
  error(TableErrorCode::Denied, [&] {
    public_table.close(self_write, original);
  });
  const auto successor_write = public_table.delegate(public_owner, original, successor, true);
  public_table.insert(successor_write, successor, 7, 78);
  error(TableErrorCode::Denied, [&] {
    public_table.close(successor_write, successor);
  });
  check(public_table.lookup(public_owner, original, 7) == 78,
        "delegated public write grant lost normal data permission");
  error(TableErrorCode::Denied, [&] {
    table.give_away(read, reader, successor);
  });
  error(TableErrorCode::Denied, [&] {
    table.give_away(original_cap, original, foreign);
  });
  Mailbox stopped_box{84};
  const auto stopped = stopped_box.context();
  stopped_box.close();
  error(TableErrorCode::Denied, [&] {
    table.give_away(original_cap, original, stopped);
  });

  table.give_away(original_cap, original, successor);
  error(TableErrorCode::Denied, [&] {
    table.owner_capability(original);
  });
  error(TableErrorCode::Denied, [&] {
    table.insert(original_cap, original, 1, 100);
  });
  error(TableErrorCode::Denied, [&] {
    table.give_away(original_cap, original, reader);
  });
  const auto successor_cap = table.owner_capability(successor);
  table.insert(successor_cap, successor, 1, 42);
  // Transferring ownership back must NOT resurrect an old owner capability.
  table.give_away(successor_cap, successor, original);
  const auto new_original_cap = table.owner_capability(original);
  error(TableErrorCode::Denied, [&] {
    table.lookup(original_cap, original, 1);
  });
  error(TableErrorCode::Denied, [&] {
    table.close(original_cap, original);
  });
  table.give_away(new_original_cap, original, successor);
  error(TableErrorCode::Denied, [&] {
    table.lookup(successor_cap, successor, 1);
  });
  const auto current_successor_cap = table.owner_capability(successor);
  original_box.close(); // Old owner stop must not retire a transferred table.
  check(table.lookup(read, reader, 1) == 42,
        "old owner retirement destroyed the new owner's table");
  const auto successor_reader = table.delegate(current_successor_cap, successor, reader);
  check(table.lookup(successor_reader, reader, 1) == 42,
        "new owner cannot delegate after handoff");
  (void)successor_box.restart();
  error(TableErrorCode::Closed, [&] { table.lookup(read, reader, 1); });
  error(TableErrorCode::Closed, [&] { table.lookup(successor_reader, reader, 1); });

  // Registration failure is transactional; the original owner retains all
  // rights, and failed candidates must never be able to retire this table.
  Mailbox prior_box{90}, full_box{91};
  const auto prior = prior_box.context(), full = full_box.context();
  SharedI64Table<1> limited(prior, TableAccess::Protected);
  const auto limited_cap = limited.owner_capability(prior);
  limited.insert(limited_cap, prior, 9, 90);
  std::vector<std::unique_ptr<SharedI64Table<1>>> registrations;
  registrations.reserve(oresforge::runtime::kMaxActorOwnedResources);
  for (std::size_t i = 0; i < oresforge::runtime::kMaxActorOwnedResources; ++i)
    registrations.push_back(std::make_unique<SharedI64Table<1>>(full));
  error(TableErrorCode::Capacity, [&] {
    limited.give_away(limited_cap, prior, full);
  });
  check(limited.lookup(limited_cap, prior, 9) == 90,
        "failed handoff changed ownership or table state");
  full_box.close();
  check(limited.lookup(limited_cap, prior, 9) == 90,
        "failed recipient stop retired unrelated table");
  prior_box.close();
  error(TableErrorCode::Denied, [&] { limited.owner_capability(prior); });
}

void table_handoff_race() {
  // Force transfer to race with original-actor teardown. The handoff either
  // linearizes first and survives or the table is retired by the old owner.
  for (int attempt = 0; attempt < 128; ++attempt) {
    Mailbox original_box{static_cast<std::uint64_t>(1000 + attempt * 2)};
    Mailbox successor_box{static_cast<std::uint64_t>(1001 + attempt * 2)};
    const auto original = original_box.context();
    const auto successor = successor_box.context();
    SharedI64Table<1> table(original);
    const auto original_cap = table.owner_capability(original);
    table.insert(original_cap, original, 1, 1);
    std::barrier start(2);
    std::atomic<bool> transferred{false};
    std::atomic<bool> bad_failure{false};
    std::thread worker([&] {
      start.arrive_and_wait();
      try {
        table.give_away(original_cap, original, successor);
        transferred = true;
      } catch (const TableError& e) {
        if (e.code != TableErrorCode::Closed && e.code != TableErrorCode::Denied)
          bad_failure = true;
      } catch (...) {
        bad_failure = true;
      }
    });
    start.arrive_and_wait();
    original_box.close();
    worker.join();
    check(!bad_failure, "unexpected handoff race failure");
    if (transferred) {
      const auto new_cap = table.owner_capability(successor);
      check(table.lookup(new_cap, successor, 1) == 1,
            "original death retired a successfully transferred table");
      successor_box.close();
      error(TableErrorCode::Denied, [&] { table.owner_capability(successor); });
    } else {
      error(TableErrorCode::Denied, [&] { table.owner_capability(original); });
    }
  }
}

void actor_owned_resource_budget() {
  Mailbox owner_box{70};
  Mailbox reader_box{71};
  const auto owner = owner_box.context();
  const auto reader = reader_box.context();

  std::vector<std::unique_ptr<SharedI64Table<1>>> tables;
  tables.reserve(oresforge::runtime::kMaxActorOwnedResources);
  for (std::size_t i = 0; i < oresforge::runtime::kMaxActorOwnedResources; ++i)
    tables.push_back(std::make_unique<SharedI64Table<1>>(
        owner, TableAccess::Public));

  auto first_owner = tables.front()->owner_capability(owner);
  auto first_read = tables.front()->delegate(first_owner, owner, reader);
  tables.front()->insert(first_owner, owner, 1, 70);

  error(TableErrorCode::Capacity, [&] {
    SharedI64Table<1> over_budget(owner);
  });

  // Destroying an owned resource frees its deterministic registration slot.
  tables.pop_back();
  tables.push_back(std::make_unique<SharedI64Table<1>>(
      owner, TableAccess::Public));

  // One restart must retire the full bounded set before returning.
  (void)owner_box.restart();
  error(TableErrorCode::Closed, [&] {
    tables.front()->lookup(first_read, reader, 1);
  });
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
  owner_box.close();
  reader_thread.join();
  check(unexpected == 0, "retirement exposed partial state");
}

void singleton_domain_registry() {
  using Value = std::array<int, 2>;
  Mailbox first_box{201, 1024, 4 * oresforge::runtime::kMaxMessageBytes, 7};
  Mailbox second_box{202, 1024, 4 * oresforge::runtime::kMaxMessageBytes, 7};
  Mailbox foreign_box{203, 1024, 4 * oresforge::runtime::kMaxMessageBytes, 8};
  const auto first_actor = first_box.context();
  const auto second_actor = second_box.context();
  const auto foreign_actor = foreign_box.context();

  SingletonRegistry<Value, 4> registry{7};
  const SingletonDeclarationId settings{11, 1};
  const SingletonDeclarationId imported_alias{11, 1};
  const SingletonDeclarationId other{11, 2};
  const SingletonDeclarationId failing{11, 3};
  const SingletonDeclarationId fourth{11, 4};
  const SingletonDeclarationId over_budget{11, 5};

  std::atomic<int> initialized{0};
  std::barrier start(8);
  std::array<std::shared_ptr<const Value>, 8> values{};
  std::vector<std::thread> workers;
  for (int i = 0; i < 8; ++i) {
    const auto actor = (i % 2 == 0) ? first_actor : second_actor;
    workers.emplace_back([&, i, actor] {
      start.arrive_and_wait();
      values[i] = registry.get(actor, settings, [&] {
        ++initialized;
        return Value{21, 42};
      });
    });
  }
  for (auto& worker : workers) worker.join();
  check(initialized == 1, "domain singleton initialized more than once");
  for (const auto& value : values)
    check(value == values[0] && (*value)[1] == 42,
          "same-domain actors received different singleton identity");

  // Import aliases are resolved by the compiler to the same declaration ID.
  bool alias_initializer_ran = false;
  auto alias = registry.get(second_actor, imported_alias, [&] {
    alias_initializer_ran = true;
    return Value{0, 0};
  });
  check(alias == values[0] && !alias_initializer_ran,
        "import alias did not resolve to the original singleton cell");

  registry_error(SingletonRegistryErrorCode::Denied, [&] {
    registry.get(foreign_actor, settings, [] { return Value{1, 1}; });
  });
  registry_error(SingletonRegistryErrorCode::InvalidDeclaration, [&] {
    registry.get(first_actor, SingletonDeclarationId{}, [] {
      return Value{1, 1};
    });
  });

  const auto stale_second = second_actor;
  (void)second_box.restart();
  const auto restarted_second = second_box.context();
  registry_error(SingletonRegistryErrorCode::Denied, [&] {
    registry.get(stale_second, settings, [] { return Value{1, 1}; });
  });
  check(registry.get(restarted_second, settings, [] {
          return Value{0, 0};
        }) == values[0],
        "restarted same-domain actor did not resolve existing singleton");

  auto other_value = registry.get(first_actor, other, [] {
    return Value{7, 8};
  });
  check(other_value != values[0] && (*other_value)[0] == 7,
        "distinct declaration IDs aliased one singleton cell");

  int failure_attempts = 0;
  for (int i = 0; i < 2; ++i)
    throws<std::logic_error>([&] {
      registry.get(i == 0 ? first_actor : restarted_second, failing,
                   [&]() -> Value {
                     ++failure_attempts;
                     throw std::logic_error("registry init failure");
                   });
    });
  check(failure_attempts == 1,
        "registry retried a failed singleton declaration");

  (void)registry.get(first_actor, fourth, [] { return Value{4, 4}; });
  registry_error(SingletonRegistryErrorCode::Capacity, [&] {
    registry.get(first_actor, over_budget, [] { return Value{5, 5}; });
  });

  registry.close();
  registry.close();
  check((*values[0])[1] == 42,
        "registry close invalidated an existing immutable reader lease");
  registry_error(SingletonRegistryErrorCode::Closed, [&] {
    registry.get(first_actor, settings, [] { return Value{0, 0}; });
  });

  throws<std::invalid_argument>([] {
    SingletonRegistry<int, 1> invalid_domain{0};
  });
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
    actor_context_lifecycle();
    table_owner_handoff();
    table_handoff_race();
    actor_owned_resource_budget();
    table_atomic_failures();
    table_concurrency();
    singleton_domain_registry();
    singleton_once_and_failure();
    singleton_dependencies_and_close();
    std::cout << "PASS: runtime-minted actor identity, checked owner handoff and 128 teardown races, bounded automatic table retirement, domain singleton identity/aliasing, authority, capacity, overflow, 80000 atomic increments, insert/CAS winners, once publication, sticky failure, cycles and close\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
