#include "compiler/frontend.hpp"
#include "compiler/singleton_stage0.hpp"
#include "runtime/actor.hpp"
#include "runtime/singleton_registry.hpp"
#include <array>
#include <atomic>
#include <barrier>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

template<class F>
void expect_error(std::string_view category, F&& action) {
  bool rejected = false;
  try {
    action();
  } catch (const std::runtime_error& error) {
    rejected = true;
    check(std::string(error.what()).find(category) != std::string::npos,
          "unexpected singleton stage0 diagnostic");
  }
  check(rejected, "singleton stage0 source unexpectedly admitted");
}

void compiler_identity_and_gate() {
  const std::string source =
      "define singleton Settings as end\n"
      "fnc main() -> int { return 42; }\n";

  // The normal frontend remains fail-closed. Stage0 is an explicit internal
  // compiler/runtime bridge, not general singleton language admission.
  expect_error("ORES-SINGLETON-UNSUPPORTED", [&] {
    (void)oresforge::parse_source(source);
  });

  auto first = oresforge::compile_singleton_stage0(source, "app/settings.ores");
  auto again = oresforge::compile_singleton_stage0(source, "app/settings.ores");
  auto foreign = oresforge::compile_singleton_stage0(source, "other/settings.ores");

  check(first.declaration_name == "Settings", "wrong declaration name");
  check(first.declaration_id.valid(), "compiler emitted invalid declaration identity");
  check(first.declaration_id == again.declaration_id,
        "same resolved module/declaration produced unstable identity");
  check(first.declaration_id.module != foreign.declaration_id.module,
        "different resolved modules aliased stage0 module identity");
  check(first.declaration_id.declaration == foreign.declaration_id.declaration,
        "same declaration spelling should retain declaration component");
  check(oresforge::interpret(first.initializer_program) == 42,
        "stage0 scalar initializer did not execute guest source");

  // Comments are not semantic receiver references.
  const std::string comment_only =
      "define singleton Settings as /* Settings */ end\n"
      "fnc main() -> int { return 42; }\n";
  (void)oresforge::compile_singleton_stage0(comment_only, "app/settings.ores");
}

void prepublication_escape_rejected() {
  const std::string self_escape =
      "define singleton Settings as\n"
      "  Settings\n"
      "end\n"
      "fnc main() -> int { return 42; }\n";

  expect_error("ORES-SINGLETON-PREPUBLICATION-ESCAPE", [&] {
    (void)oresforge::compile_singleton_stage0(
        self_escape, "app/settings.ores");
  });

  // Non-empty singleton member syntax remains deliberately unsupported rather
  // than being guessed into the language surface.
  const std::string unknown_body =
      "define singleton Settings as\n"
      "  value\n"
      "end\n"
      "fnc main() -> int { return 42; }\n";
  expect_error("ORES-SINGLETON-STAGE0-BODY-UNSUPPORTED", [&] {
    (void)oresforge::compile_singleton_stage0(
        unknown_body, "app/settings.ores");
  });
}

void concurrent_first_access_uses_compiled_plan() {
  const std::string source =
      "define pub singleton Settings as end\n"
      "fnc main() -> int { return 42; }\n";
  const auto plan =
      oresforge::compile_singleton_stage0(source, "app/settings.ores");

  constexpr std::uint64_t domain = 17;
  oresforge::runtime::Mailbox first_box{901, 1024,
      4 * oresforge::runtime::kMaxMessageBytes, domain};
  oresforge::runtime::Mailbox second_box{902, 1024,
      4 * oresforge::runtime::kMaxMessageBytes, domain};
  const auto first_actor = first_box.context();
  const auto second_actor = second_box.context();

  // Mirror the conformance 3/7/15-carrier admission sizes. These remain
  // two trusted host actor identities: no claim of 3/7/15 distinct guest heaps.
  for (const int workers : {3, 7, 15}) {
    oresforge::shared::SingletonRegistry<std::int64_t, 4> registry{domain};
    std::barrier start(workers);
    std::atomic<int> initializer_runs{0};
    std::vector<std::shared_ptr<const std::int64_t>> values(
        static_cast<std::size_t>(workers));
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(workers));

    for (int i = 0; i < workers; ++i) {
      const auto actor = (i % 2 == 0) ? first_actor : second_actor;
      threads.emplace_back([&, i, actor] {
        start.arrive_and_wait();
        values[static_cast<std::size_t>(i)] = registry.get(
            actor, plan.declaration_id, [&] {
              ++initializer_runs;
              return oresforge::interpret(plan.initializer_program);
            });
      });
    }
    for (auto& thread : threads) thread.join();

    check(initializer_runs == 1,
          "3/7/15 compiled singleton initializer executed more than once");
    for (const auto& value : values) {
      check(value && *value == 42, "compiled singleton published wrong value");
      check(value == values[0],
            "same-domain actors did not observe one singleton identity");
    }

    registry.close();
    check(*values[0] == 42,
          "immutable singleton reader lease invalidated by registry close");
  }
}

} // namespace

int main() {
  try {
    compiler_identity_and_gate();
    prepublication_escape_rejected();
    concurrent_first_access_uses_compiled_plan();
    std::cout
        << "PASS: stage0 singleton compiler identity, semantic self-escape rejection, "
           "compiled scalar initializer, and concurrent once publication\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
