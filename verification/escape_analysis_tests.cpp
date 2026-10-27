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
    p.uses.push_back({1, UseKind::LocalBorrow, 0, true});
    auto d = analyze(p, 32);
    need(d[0].storage == Storage::StackCandidate &&
         d[1].storage == Storage::PreserveAllocation, "aggregate frame budget");
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
    p = base();
    invalid([&] { (void)analyze(p, 0); });
    need(std::string(storage_name(Storage::ScalarCandidate)) == "scalar-candidate",
         "report storage names");
    std::cout << "escape MIR candidate tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
