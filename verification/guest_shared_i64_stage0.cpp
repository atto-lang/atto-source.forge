#include "compiler/frontend.hpp"
#include "compiler/singleton_stage0.hpp"
#include "runtime/singleton_registry.hpp"
#include "runtime/actor.hpp"
#include "runtime/guest_shared_i64_stage0.hpp"
#include <array>
#include <atomic>
#include <barrier>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace oresforge;
using namespace oresforge::shared;
using oresforge::runtime::Mailbox;

void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template<class F> void error(TableErrorCode code, F&& action) {
  try { action(); }
  catch (const TableError& exception) {
    check(exception.code == code, "unexpected table error category");
    return;
  }
  throw std::runtime_error("table operation was unexpectedly admitted");
}
template<class F> void rejected(const char* category, F&& action) {
  try { action(); }
  catch (const std::runtime_error& exception) {
    check(std::string(exception.what()).find(category) != std::string::npos,
          "unexpected guest stage0 diagnostic");
    return;
  }
  throw std::runtime_error("unadmitted guest source or IR was accepted");
}
template<class Dispatch>
std::int64_t run(const Program& p, Dispatch& dispatch,
                 std::int64_t a, std::int64_t b = 0) {
  const std::array<std::int64_t, 2> args{a,b};
  return interpret_shared_i64_stage0(p,
      std::span<const std::int64_t>(args).first(p.parameter_count), dispatch);
}

void compile_gate_and_ir_verification() {
  constexpr auto program =
      "fnc main(key: int, delta: int) -> int { "
      "return shared_i64_update_counter(key, delta); }";
  const auto compiled = parse_shared_i64_stage0_source(program);
  check(compiled.shared_table_stage0 && compiled.parameter_count == 2,
        "guest profile lost typed entry/authority boundary");
  rejected("ORES-SHARED-TABLE-UNSUPPORTED", [&] {
    (void)parse_source(program);
  });
  rejected("ORES-SHARED-TABLE-UNSUPPORTED", [&] {
    (void)parse_runtime_source(program);
  });
  rejected("matching trusted dispatcher", [&] {
    (void)interpret(compiled, std::array<std::int64_t,2>{1,2});
  });
  rejected("shared-table intrinsic arity mismatch", [&] {
    (void)parse_shared_i64_stage0_source(
        "fnc main() -> int { return shared_i64_lookup(1,2); }");
  });
  rejected("unknown function", [&] {
    (void)parse_shared_i64_stage0_source(
        "fnc main() -> int { return other_shared_call(1); }");
  });
  rejected("ORES-SINGLETON-UNSUPPORTED", [&] {
    (void)parse_shared_i64_stage0_source(
        "define singleton Bad as end "
        "fnc main() -> int { return 1; }");
  });
  Program forged = compiled;
  forged.shared_table_stage0 = false;
  rejected("invalid or unadmitted shared-table IR", [&] { verify(forged); });
  forged = compiled;
  // Program may contain params before guest op; find and corrupt actual op.
  for (auto& instruction : forged.instructions) {
    if (instruction.op == Op::TableUpdateCounter) {
      instruction.b = -1;
      break;
    }
  }
  rejected("invalid or unadmitted shared-table IR", [&] { verify(forged); });
  check(dump_ir(compiled).find("guest.table.update_counter.i64") != std::string::npos,
        "guest opcode not present in verified compiler IR");
}

void compiled_table_ops_and_permissions() {
  Mailbox owner_box{1}, reader_box{2}, other_box{3};
  const auto owner = owner_box.context(), reader = reader_box.context();
  SharedI64Table<4> table(owner, TableAccess::Protected);
  auto own = table.owner_capability(owner);
  auto read = table.delegate(own, owner, reader);
  MailboxBoundI64GuestDispatch<4> owned(owner_box, table, own);
  MailboxBoundI64GuestDispatch<4> readonly(reader_box, table, read);
  MailboxBoundI64GuestDispatch<4> mismatched(other_box, table, read);
  auto insert = parse_shared_i64_stage0_source(
      "fnc main(key: int, value: int) -> int { "
      "return shared_i64_insert(key, value); }");
  auto lookup = parse_shared_i64_stage0_source(
      "fnc main(key: int) -> int { return shared_i64_lookup(key); }");
  auto insert_new = parse_shared_i64_stage0_source(
      "fnc main(key: int, value: int) -> int { "
      "return shared_i64_insert_new(key, value); }");
  auto increment = parse_shared_i64_stage0_source(
      "fnc main(key: int, delta: int) -> int { "
      "return shared_i64_update_counter(key, delta); }");

  check(run(insert, owned, 7, 40) == 40, "compiled guest insert failed");
  check(run(lookup, readonly, 7) == 40, "compiled guest reader cannot read");
  check(run(insert_new, owned, 7, 100) == 0,
        "compiled insert-new replaced an existing key");
  check(run(increment, owned, 7, 2) == 42, "guest atomic counter failed");
  check(run(lookup, readonly, 7) == 42, "guest did not observe atomic update");
  check(run(insert_new, owned, 8, 80) == 1, "compiled insert-new failed to insert");
  error(TableErrorCode::Missing, [&] { (void)run(lookup, readonly, 123); });
  error(TableErrorCode::Denied, [&] { (void)run(increment, readonly, 7, 1); });
  error(TableErrorCode::Denied, [&] { (void)run(lookup, mismatched, 7); });
  table.insert(own, owner, 9, std::numeric_limits<std::int64_t>::max());
  error(TableErrorCode::Overflow, [&] { (void)run(increment, owned, 9, 1); });
  check(run(lookup, readonly, 9) == std::numeric_limits<std::int64_t>::max(),
        "failed compiled overflow changed the cell");

  (void)reader_box.restart();
  error(TableErrorCode::Denied, [&] { (void)run(lookup, readonly, 7); });
  owner_box.close();
  error(TableErrorCode::Denied, [&] { (void)run(lookup, readonly, 7); });
}

void compiled_singleton_to_actor_table_boundary() {
  constexpr std::uint64_t domain = 19;
  const auto plan = compile_singleton_stage0(
      "define singleton Settings as end "
      "fnc main() -> int { return 40 + 2; }",
      "app/settings.ores");
  Mailbox owner_box{701, 1024, 4 * oresforge::runtime::kMaxMessageBytes, domain};
  Mailbox actor_box{702, 1024, 4 * oresforge::runtime::kMaxMessageBytes, domain};
  SingletonRegistry<std::int64_t, 2> registry{domain};
  const auto value = registry.get(owner_box.context(), plan.declaration_id, [&] {
    return interpret(plan.initializer_program);
  });
  auto alias = registry.get(actor_box.context(), plan.declaration_id, [] {
    return std::int64_t{-100};
  });
  check(value == alias && *value == 42, "compiled singleton identity not shared");
  SharedI64Table<2> table(owner_box.context(), TableAccess::Public);
  const auto own = table.owner_capability(owner_box.context());
  table.insert(own, owner_box.context(), 1, *value);
  const auto actor_cap = table.delegate(own, owner_box.context(),
                                        actor_box.context(), true);
  MailboxBoundI64GuestDispatch<2> actor_dispatch(actor_box,table,actor_cap);
  const auto increase = parse_shared_i64_stage0_source(
      "fnc main(key: int, delta: int) -> int { "
      "return shared_i64_update_counter(key, delta); }");
  check(run(increase, actor_dispatch, 1, 8) == 50,
        "compiled singleton initial value not visible to guest table update");
  registry.close();
  check(*value == 42 && table.lookup(own,owner_box.context(),1) == 50,
        "singleton reader lease or shared table corrupted on registry shutdown");
  owner_box.close();
  error(TableErrorCode::Closed, [&] {
    (void)run(increase,actor_dispatch,1,1);
  });
}

void concurrent_compiled_guest_operations() {
  constexpr int workers = 8, rounds = 10000;
  Mailbox owner_box{11};
  const auto owner = owner_box.context();
  SharedI64Table<2> table(owner, TableAccess::Public);
  const auto own = table.owner_capability(owner);
  table.insert(own, owner, 1, 0);
  const auto increment = parse_shared_i64_stage0_source(
      "fnc increment(value: int) -> int { return value + 1; } "
      "fnc main(key: int, delta: int) -> int { "
      "return shared_i64_update_counter(key, increment(delta) - 1); }");
  // This tests a real compiler-lowered callable expression + table effect.
  std::vector<std::unique_ptr<Mailbox>> boxes;
  std::vector<std::thread> threads;
  boxes.reserve(workers);
  std::barrier start(workers);
  std::atomic<int> unexpected{0};
  for (int i=0; i<workers; ++i) {
    boxes.push_back(std::make_unique<Mailbox>(static_cast<std::uint64_t>(100+i)));
    const auto actor = boxes.back()->context();
    const auto cap = table.delegate(own, owner, actor, true);
    auto* mailbox = boxes.back().get();
    threads.emplace_back([&,mailbox,cap] {
      MailboxBoundI64GuestDispatch<2> dispatch(*mailbox,table,cap);
      start.arrive_and_wait();
      try {
        for (int j=0; j<rounds; ++j)
          (void)run(increment,dispatch,1,1);
      } catch (...) { ++unexpected; }
    });
  }
  for (auto& thread : threads) thread.join();
  check(unexpected == 0 && table.lookup(own,owner,1) == workers * rounds,
        "compiled multi-actor increments were not linearizable");
  owner_box.close();
}

} // namespace

int main() {
  try {
    compile_gate_and_ir_verification();
    compiled_table_ops_and_permissions();
    compiled_singleton_to_actor_table_boundary();
    concurrent_compiled_guest_operations();
    std::cout << "PASS: compiled stage0 i64 table operations, runtime-derived "
                 "actor context, stale-handle rejection, overflow, 80000 "
                 "atomic guest updates and IR admission fence\\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\\n';
    return 1;
  }
}
