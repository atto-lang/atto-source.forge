#include "compiler/escape_analysis.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace oresforge::escape;
static void need(bool ok, const char* description) {
  if (!ok) throw std::runtime_error(description);
}
static void invalid(const std::function<void()>& f) {
  bool rejected = false;
  try { f(); } catch (const std::invalid_argument&) { rejected = true; }
  need(rejected, "invalid MIR must be rejected");
}
static Allocation record(const char* id, std::size_t size = 16,
                         std::size_t align = 8) {
  Allocation a;
  a.name = id; a.type = ValueKind::NominalRecord;
  a.size_bytes = size; a.alignment = align; a.fields = 2;
  a.allocator_effect_observable = false;
  return a;
}
static Program base() {
  return {{record("p")}, {{0, UseKind::FieldWrite, 0},
                           {0, UseKind::FieldWrite, 1},
                           {0, UseKind::FieldRead, 0},
                           {0, UseKind::FieldRead, 1}}, true};
}
int main() {
  try {
    auto p = base();
    need(analyze(p)[0].storage == Storage::ScalarCandidate,
         "closed two-field local record scalar candidate");
    p.complete_effect_graph = false;
    need(analyze(p)[0].storage == Storage::PreserveAllocation,
         "incomplete graph cannot optimize");
    p = base();
    p.uses.push_back({0, UseKind::LocalBorrow, 0, true});
    need(analyze(p)[0].storage == Storage::StackCandidate,
         "verified short local loan permits stack candidate");
    p.uses.push_back({0, UseKind::FieldWrite, 0});
    need(analyze(p)[0].storage == Storage::PreserveAllocation,
         "write following a loan cannot stack-promote without an end-borrow event");
    p.uses.pop_back();
    need(analyze(p, 8)[0].storage == Storage::PreserveAllocation,
         "stack budget enforced");
    p.uses.back().lifetime_proven = false;
    need(analyze(p)[0].storage == Storage::PreserveAllocation,
         "unproven local loan cannot optimize");
    for (auto kind : {UseKind::Return, UseKind::Capture,
                      UseKind::ActorSend, UseKind::ChannelSend,
                      UseKind::Ffi, UseKind::UnknownCall,
                      UseKind::Suspend, UseKind::IdentityObserve,
                      UseKind::StoreGlobal, UseKind::UnsafePointer,
                      UseKind::Transfer}) {
      p = base(); p.uses.push_back({0, kind});
      need(analyze(p)[0].storage == Storage::PreserveAllocation,
           "escaping/opaque boundary must preserve allocation");
    }
    p = base(); p.allocations[0].custom_drop = true;
    need(analyze(p)[0].storage == Storage::PreserveAllocation, "custom drop");
    p = base(); p.allocations[0].allocator_effect_observable = true;
    need(analyze(p)[0].storage == Storage::PreserveAllocation, "allocator effects");
    p = base(); p.allocations[0].volatile_or_atomic = true;
    need(analyze(p)[0].storage == Storage::PreserveAllocation, "volatile/atomic");
    p = base(); p.allocations[0].dynamic_extent = true;
    need(analyze(p)[0].storage == Storage::PreserveAllocation, "dynamic extent");
    p = base(); p.allocations[0].type = ValueKind::Unknown;
    need(analyze(p)[0].storage == Storage::PreserveAllocation, "unmodeled value type");
    p = base(); p.allocations[0].alignment = 128;
    p.uses.push_back({0, UseKind::LocalBorrow, 0, true});
    need(analyze(p)[0].storage == Storage::PreserveAllocation, "overaligned stack");
    p = base(); p.allocations.push_back(record("q", 32, 16));
    p.uses.push_back({0, UseKind::LocalBorrow, 0, true});
    // Initialize q independently before borrowing it. Otherwise q would be
    // preserved for partial initialization, never exercising frame budget.
    p.uses.push_back({1, UseKind::FieldWrite, 0});
    p.uses.push_back({1, UseKind::FieldWrite, 1});
    p.uses.push_back({1, UseKind::LocalBorrow, 0, true});
    auto d = analyze(p, 32);
    need(d[0].storage == Storage::StackCandidate &&
         d[1].storage == Storage::PreserveAllocation &&
         d[1].reason == "stack-budget-or-alignment", "aggregate frame budget");
    d = analyze(p, 48);
    need(d[0].storage == Storage::StackCandidate &&
         d[1].storage == Storage::StackCandidate, "aggregate frame within budget");
    // A small first slot followed by a highly aligned slot needs padding
    // before the second slot. Independent slot rounding undercharges by 56.
    p = base(); p.allocations[0] = record("p", 8, 8);
    p.allocations.push_back(record("q", 64, 64));
    p.uses.push_back({0, UseKind::LocalBorrow, 0, true});
    p.uses.push_back({1, UseKind::FieldWrite, 0});
    p.uses.push_back({1, UseKind::FieldWrite, 1});
    p.uses.push_back({1, UseKind::LocalBorrow, 0, true});
    for (std::size_t budget : {72u, 127u}) {
      d = analyze(p, budget);
      need(d[0].storage == Storage::StackCandidate &&
           d[1].storage == Storage::PreserveAllocation &&
           d[1].reason == "stack-budget-or-alignment", "inter-slot padding charged");
    }
    d = analyze(p, 128);
    need(d[0].storage == Storage::StackCandidate &&
         d[1].storage == Storage::StackCandidate, "exact padded frame fits");
    p.allocations[1] = record("q", 16, 16);
    d = analyze(p, 24);
    need(d[1].storage == Storage::PreserveAllocation, "mixed alignment cannot overbook");
    d = analyze(p, 32);
    need(d[1].storage == Storage::StackCandidate, "mixed alignment exact fit");
    p = base(); p.allocations.push_back(record("p"));
    invalid([&] { (void)analyze(p); });
    p = base(); p.allocations[0].alignment = 3;
    invalid([&] { (void)analyze(p); });
    p = base(); p.allocations[0].size_bytes = 0;
    invalid([&] { (void)analyze(p); });
    p = base(); p.uses.push_back({10, UseKind::ActorSend});
    invalid([&] { (void)analyze(p); });
    p = base(); p.uses.push_back({0, static_cast<UseKind>(255)});
    invalid([&] { (void)analyze(p); });
    p = base(); p.uses.push_back({0, UseKind::FieldRead, 2});
    invalid([&] { (void)analyze(p); });
    p = base(); p.uses.push_back({0, UseKind::FieldRead, 0, true});
    invalid([&] { (void)analyze(p); });
    p = base(); p.uses.push_back({0, UseKind::FieldWrite, 1, true});
    invalid([&] { (void)analyze(p); });
    p = base(); p.uses.push_back({0, UseKind::LocalBorrow, 1, true});
    invalid([&] { (void)analyze(p); });
    p = base(); p.uses.push_back({0, UseKind::ActorSend, 5});
    invalid([&] { (void)analyze(p); });
    p = base(); p.uses.push_back({0, UseKind::UnknownCall, 0, true});
    invalid([&] { (void)analyze(p); });
    p = base();
    p.uses = {{0, UseKind::FieldRead, 0}};
    invalid([&] { (void)analyze(p); });
    p = base();
    p.uses = {{0, UseKind::FieldWrite, 0},
              {0, UseKind::FieldRead, 1},
              {0, UseKind::FieldWrite, 1}};
    invalid([&] { (void)analyze(p); });
    p = base();
    p.uses = {{0, UseKind::FieldWrite, 0},
              {0, UseKind::LocalBorrow, 0, true}};
    need(analyze(p)[0].storage == Storage::PreserveAllocation,
           "partially initialized record cannot be stack-promoted via a borrow");
    p = base();
    p.uses = {{0, UseKind::FieldWrite, 0}};
    need(analyze(p)[0].storage == Storage::PreserveAllocation,
         "partially initialized record cannot be scalar-promoted");
    p = base();
    p.allocations[0].fields = 17;
    invalid([&] { (void)analyze(p); });
    p = base(); p.allocations.push_back(record("another"));
    p.uses = {{0, UseKind::FieldWrite, 0}, {0, UseKind::FieldWrite, 1},
              {1, UseKind::FieldRead, 0}};
    invalid([&] { (void)analyze(p); });
    p = base();
    invalid([&] { (void)analyze(p, 0); });
    need(std::string(storage_name(Storage::ScalarCandidate)) == "scalar-candidate",
         "report storage names");
    std::cout << "escape MIR candidate tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
