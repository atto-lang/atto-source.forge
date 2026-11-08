#include "compiler/actor_subset.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
using oresforge::actors::ActorExecutionContext;
using oresforge::actors::Program;
using oresforge::actors::compile;
using oresforge::runtime::ScalarActor;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Error, class Fn> void rejects(Fn&& action) {
    bool rejected = false;
    try { action(); } catch (const Error&) { rejected = true; }
    check(rejected, "expected checked rejection");
}
constexpr const char* counter = R"atto(
define actor Counter as
  state { count: int = 0; }
  pub fnc on_message(int delta): int {
    self.count = self.count + delta;
    return self.count;
  }
end
)atto";
constexpr const char* recursion = R"atto(
define actor Recurse as
  pub fnc on_message(int n): int { return countdown(n); }
  pub fnc countdown(int n): int {
    if n == 0 then return 0; else return countdown(n - 1); fi
  }
end
)atto";
}
int main() {
    const auto p = compile(counter);
    check(p.actor_name == "Counter", "actor nominal name lost");
    check(p.field_name == "count", "typed i64 field not lowered");
    check(p.handler == Program::Handler::CheckedFieldAdd, "incorrect handler");
    check(oresforge::actors::dump_ir(p).find("checked-add-field") !=
          std::string::npos, "missing compiled field IR");

    // Distinct source-compiled actor instances must never share state/authority.
    ActorExecutionContext a(p, 900), b(p, 901);
    const auto original = a.ref();
    check(a.receive(3) == 3, "actor A initial state");
    check(b.receive(8) == 8, "actor B state leaked");
    check(a.receive(4) == 7, "actor A mutable field not retained");
    check(b.receive(-2) == 6, "actor B isolated signed update");
    check(a.try_send(b.ref(), 999) == ScalarActor::Send::Foreign,
          "cross-actor reference admitted");
    check(a.receive(0) == 7, "foreign send modified actor A");
    check(b.receive(0) == 6, "foreign send modified actor B");
    std::atomic<bool> moved = false;
    std::thread other_carrier([&] {
        try { moved = (a.receive(5) == 12); }
        catch (...) { moved = false; }
    });
    other_carrier.join();
    check(moved, "actor state could not migrate at a turn boundary");

    // Concurrent execution is on separate carriers, with separate heaps.
    ActorExecutionContext left(p, 904), right(p, 905);
    std::atomic<bool> good = true;
    std::atomic<bool> start = false;
    auto pump = [&](ActorExecutionContext& actor, std::int64_t delta) {
        try {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (int i = 1; i <= 64; ++i)
                if (actor.receive(delta) != delta * i) good = false;
        } catch (...) { good = false; }
    };
    std::thread l([&] { pump(left, 2); });
    std::thread rr([&] { pump(right, 3); });
    start.store(true, std::memory_order_release);
    l.join(); rr.join();
    check(good, "actor-local fields interfered across carriers");
    check(left.receive(0) == 128 && right.receive(0) == 192,
          "actor-local fields lost after independent turns");

    a.stop();
    check(a.done(), "STOP did not drain actor");
    rejects<std::logic_error>([&] { (void)a.receive(1); });
    ActorExecutionContext restarted(p, 900);
    check(restarted.try_send(original, 42) == ScalarActor::Send::Foreign,
          "stale actor reference accepted across numeric ID reuse");
    check(restarted.receive(2) == 2, "restart reused stale heap state");

    // Compiled recursive method uses bounded logical actor frames.
    const auto depth_program = compile(recursion);
    check(depth_program.recursive_method == "countdown", "method symbol lost");
    check(oresforge::actors::dump_ir(depth_program).find("stackless-loop") !=
          std::string::npos, "source compiler skipped stackless lowering");
    ActorExecutionContext depth(depth_program, 902);
    check(depth.receive(0) == 0, "zero-depth recursion");
    check(depth.receive(32) == 32, "middle recursion");
    check(depth.receive(63) == 63, "64-frame boundary");
    rejects<std::length_error>([&] { (void)depth.receive(64); });
    check(depth.done(), "overflowing compiled actor remained live");
    // The same process must remain usable after a sibling actor fault.
    check(b.receive(1) == 7, "healthy actor not alive after other overflow");
    ActorExecutionContext depth_restart(depth_program, 902);
    check(depth_restart.receive(3) == 3, "fresh recursion incarnation denied");

    // Mainline process ledger must be charged before source actor creation
    // and refunded exactly once after the native actor is quiesced.
    auto& process_budget = oresforge::runtime::ActorMemoryAggregateBudgetV1::process();
    const auto initial_reserved = process_budget.reserved();
    {
        ActorExecutionContext metered(p, 920);
        check(process_budget.reserved() ==
              initial_reserved + 128 * 1024, "actor budget not admitted");
        check(metered.receive(2) == 2, "budgeted actor did not run");
        metered.stop();
    }
    check(process_budget.reserved() == initial_reserved,
          "actor budget was not refunded after teardown");
    rejects<std::invalid_argument>([&] {
        ActorExecutionContext failed(p, 0);
    });
    check(process_budget.reserved() == initial_reserved,
          "actor constructor failure leaked process memory quota");
    // The source compiler must bound the number of live actor incarnations,
    // even when each individual actor's own heap/stack quota is available.
    const auto actor_head = std::string(counter);
    auto actor_main = [&](int spawns, int sends) {
        std::string result = actor_head;
        result += "pub fnc main(): int {\n";
        for (int i = 0; i < spawns; ++i)
            result += "const a" + std::to_string(i) +
                      " = spawn Counter();\n";
        for (int i = 0; i < sends; ++i)
            result += "a0.on_message(1);\n";
        result += "return a0.on_message(2) + a1.on_message(3);\n}\n";
        return result;
    };
    rejects<std::invalid_argument>([&] {
        (void)compile(actor_main(9, 0));
    });
    rejects<std::invalid_argument>([&] {
        (void)compile(actor_main(2, 257));
    });
    check(oresforge::actors::execute_main(compile(actor_main(8, 256))) == 261,
          "boundary quota of 8 actors and 256 messages rejected");

    // The new stage-0 source-main profile actually constructs multiple
    // independent actors from static .atto spawn bindings.
    constexpr const char* program = R"atto(
define actor Counter as
  state { count: int = 0; }
  pub fnc on_message(int delta): int {
    self.count = self.count + delta; return self.count;
  }
end
pub fnc main(): int {
  const left = spawn Counter();
  const right = spawn Counter();
  left.on_message(3);
  right.on_message(5);
  return left.on_message(4) + right.on_message(30);
}
)atto";
    const auto assembled = compile(program);
    check(assembled.has_main && assembled.spawn_bindings.size() == 2 &&
          assembled.sends.size() == 2 && assembled.result.has_value(),
          "main spawn/send/reply did not generate typed plan");
    check(oresforge::actors::dump_ir(assembled).find("spawn Counter as left") !=
          std::string::npos, "source main spawn missing from IR");
    check(oresforge::actors::execute_main(assembled) == 42,
          "source-level two actor program returned wrong result");
    rejects<std::invalid_argument>([&] {
        auto forged = assembled;
        forged.sends.front().actor_index = 99;
        (void)oresforge::actors::execute_main(forged);
    });
    rejects<std::invalid_argument>([] {
        (void)compile(R"atto(
define actor Counter as
  state { n: int = 0; }
  pub fnc on_message(int x): int {
    self.n = self.n + x; return self.n;
  }
end
pub fnc main(): int {
  const same = spawn Counter();
  const same = spawn Counter();
  return same.on_message(1) + same.on_message(2);
}
)atto");
    });
    // Renaming every user-defined symbol and changing initial value must
    // change compiled behavior, not just match a canned fixture pattern.
    const auto renamed_field = compile(R"atto(
define actor Ledger as
  state { total: int = -13; }
  pub fnc on_message(int amount): int {
    self.total = self.total + amount;
    return self.total;
  }
end
)atto");
    check(renamed_field.actor_name == "Ledger" &&
          renamed_field.field_name == "total" &&
          renamed_field.parameter_name == "amount" &&
          renamed_field.initial == -13, "renamed source AST not preserved");
    ActorExecutionContext ledger(renamed_field, 910);
    check(ledger.receive(10) == -3, "source initial state ignored");
    check(ledger.receive(-7) == -10, "source field update not generalized");
    ledger.stop();

    const auto renamed_recursive = compile(R"atto(
define actor Walker as
  pub fnc on_message(int steps): int { return recur(steps); }
  pub fnc recur(int remaining): int {
    if remaining == 0 then return 0; else return recur(remaining - 1); fi
  }
end
)atto");
    check(renamed_recursive.actor_name == "Walker" &&
          renamed_recursive.recursive_method == "recur", "method symbol ignored");
    ActorExecutionContext walker(renamed_recursive, 911);
    check(walker.receive(9) == 9, "renamed recursive method not executed");
    walker.stop();

    rejects<std::invalid_argument>([] {
        (void)compile(R"atto(
define actor self as
  state { total: int = 0; }
  pub fnc on_message(int x): int {
    self.total = self.total + x; return self.total;
  }
end
)atto");
    });
    rejects<std::invalid_argument>([] {
        (void)compile(R"atto(
define actor Reserved as
  state { self: int = 0; }
  pub fnc on_message(int x): int {
    self.self = self.self + x; return self.self;
  }
end
)atto");
    });
    // Arithmetic overflow terminates only the guilty actor.
    const auto p_max = compile(R"atto(
define actor MaxCounter as
  state { counter: int = 9223372036854775807; }
  pub fnc on_message(int x): int {
    self.counter = self.counter + x;
    return self.counter;
  }
end
)atto");
    ActorExecutionContext over(p_max, 906);
    rejects<std::overflow_error>([&] { (void)over.receive(1); });
    check(over.done(), "overflow did not stop offending actor");
    check(b.receive(-1) == 6, "overflow contaminated healthy heap");

    // Reject syntactically valid-looking but unsupported actor extensions.
    rejects<std::invalid_argument>([] { (void)compile("define actor X as end"); });
    rejects<std::invalid_argument>([] { (void)compile("define actor X as // no body"); });
    rejects<std::invalid_argument>([] { (void)compile("define actor X as /* unterminated"); });
    rejects<std::invalid_argument>([] {
        (void)compile(R"atto(define actor X as
state { counter: int = 9223372036854775808; }
pub fnc on_message(int x): int {
self.counter = self.counter + x; return self.counter;
} end)atto");
    });
    // Embedded actor-looking comments may not be executed as declarations.
    const auto with_comments = compile(std::string("// actor Hidden\n") + counter +
                                       "/* define actor Spoof as end */");
    check(with_comments.actor_name == "Counter", "comments changed nominal AST");
    b.stop(); left.stop(); right.stop(); restarted.stop(); depth_restart.stop();
    std::puts("ATTO_FORGE_COMPILED_SOURCE_ACTOR_HOST|field=1|recursion=1|sibling_survival=1|carrier_migration=1|restart_fence=1|guest_source=1|general_spawn=NOT_EXECUTED");
}
