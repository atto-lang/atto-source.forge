#pragma once
// Closed-world candidate classifier for proposed allocation MIR. NOT codegen.
// No candidate is promoted without subsequent ownership/lifetime verification.
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace oresforge::escape {

enum class ValueKind { NominalRecord, FixedArray, Closure, Unknown };
enum class UseKind {
  FieldRead, FieldWrite, LocalBorrow,
  Return, Capture, ActorSend, ChannelSend, Ffi, UnknownCall,
  Suspend, IdentityObserve, StoreGlobal, UnsafePointer, Transfer
};
enum class Storage { ScalarCandidate, StackCandidate, PreserveAllocation };

struct Allocation {
  std::string name;
  ValueKind type = ValueKind::Unknown;
  std::size_t size_bytes = 0;
  std::size_t alignment = 1;
  std::size_t fields = 0;
  bool custom_drop = false;
  bool allocator_effect_observable = true;
  bool dynamic_extent = false;
  bool volatile_or_atomic = false;
};

struct Use {
  std::size_t allocation = 0;
  UseKind kind = UseKind::UnknownCall;
  std::size_t field = 0;
  // For LocalBorrow only: certified by a separate lifetime/loan verifier.
  bool lifetime_proven = false;
};

struct Program {
  std::vector<Allocation> allocations;
  std::vector<Use> uses;
  // Must be set only by a verified MIR builder that accounted for every use,
  // exceptional edge and opaque effect. False always preserves allocations.
  bool complete_effect_graph = false;
};

struct Decision {
  Storage storage = Storage::PreserveAllocation;
  std::string reason;
};

inline const char* storage_name(Storage storage) {
  switch (storage) {
    case Storage::ScalarCandidate: return "scalar-candidate";
    case Storage::StackCandidate: return "stack-candidate";
    case Storage::PreserveAllocation: return "preserve-allocation";
  }
  throw std::logic_error("unknown allocation decision");
}

inline bool is_power_of_two(std::size_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

// Conservative: a modeled allocation has no reference relationships between
// allocations. Unknown aliasing, inter-object stores and call effects MUST be
// encoded as an UnknownCall or escape use, never silently dropped.
inline std::vector<Decision> analyze(const Program& program,
                                     std::size_t stack_budget_bytes = 4096) {
  if (stack_budget_bytes == 0)
    throw std::invalid_argument("stack budget must be positive");
  std::unordered_set<std::string> names;
  for (const auto& alloc : program.allocations) {
    if (alloc.name.empty() || !names.insert(alloc.name).second)
      throw std::invalid_argument("allocation names must be unique and nonempty");
    if (alloc.size_bytes == 0 || !is_power_of_two(alloc.alignment))
      throw std::invalid_argument("invalid allocation size or alignment");
    if (alloc.fields == 0 && (alloc.type == ValueKind::NominalRecord ||
                              alloc.type == ValueKind::FixedArray))
      throw std::invalid_argument("aggregate must declare nonzero field count");
  }
  std::vector<bool> borrowed(program.allocations.size(), false);
  std::vector<bool> preserved(program.allocations.size(), false);
  std::vector<std::string> reason(program.allocations.size());
  auto preserve = [&](std::size_t i, const char* why) {
    if (!preserved[i]) {
      preserved[i] = true;
      reason[i] = why;
    }
  };
  for (const auto& use : program.uses) {
    if (use.allocation >= program.allocations.size())
      throw std::invalid_argument("allocation use index out of range");
    const auto& alloc = program.allocations[use.allocation];
    switch (use.kind) {
      case UseKind::FieldRead:
      case UseKind::FieldWrite:
        if (use.field >= alloc.fields)
          throw std::invalid_argument("field index out of range");
        break;
      case UseKind::LocalBorrow:
        if (use.lifetime_proven) borrowed[use.allocation] = true;
        else preserve(use.allocation, "unproven-local-borrow-lifetime");
        break;
      case UseKind::Return: preserve(use.allocation, "return-escape"); break;
      case UseKind::Capture: preserve(use.allocation, "closure-capture"); break;
      case UseKind::ActorSend: preserve(use.allocation, "actor-send"); break;
      case UseKind::ChannelSend: preserve(use.allocation, "channel-send"); break;
      case UseKind::Ffi: preserve(use.allocation, "ffi-boundary"); break;
      case UseKind::UnknownCall: preserve(use.allocation, "unknown-call-effects"); break;
      case UseKind::Suspend: preserve(use.allocation, "suspension-boundary"); break;
      case UseKind::IdentityObserve: preserve(use.allocation, "identity-observable"); break;
      case UseKind::StoreGlobal: preserve(use.allocation, "global-store"); break;
      case UseKind::UnsafePointer: preserve(use.allocation, "unsafe-pointer"); break;
      case UseKind::Transfer: preserve(use.allocation, "ownership-transfer"); break;
    }
  }
  std::vector<Decision> decisions;
  decisions.reserve(program.allocations.size());
  std::size_t stack_remaining = stack_budget_bytes;
  for (std::size_t i = 0; i < program.allocations.size(); ++i) {
    const auto& alloc = program.allocations[i];
    if (!program.complete_effect_graph) {
      decisions.push_back({Storage::PreserveAllocation, "incomplete-effect-graph"});
    } else if (alloc.allocator_effect_observable) {
      decisions.push_back({Storage::PreserveAllocation, "allocator-effects-observable"});
    } else if (alloc.custom_drop) {
      decisions.push_back({Storage::PreserveAllocation, "custom-drop-not-proven"});
    } else if (alloc.volatile_or_atomic) {
      decisions.push_back({Storage::PreserveAllocation, "volatile-or-atomic"});
    } else if (alloc.dynamic_extent) {
      decisions.push_back({Storage::PreserveAllocation, "dynamic-extent"});
    } else if (alloc.type != ValueKind::NominalRecord &&
               alloc.type != ValueKind::FixedArray) {
      decisions.push_back({Storage::PreserveAllocation, "unmodeled-value-type"});
    } else if (preserved[i]) {
      decisions.push_back({Storage::PreserveAllocation, reason[i]});
    } else if (!borrowed[i]) {
      decisions.push_back({Storage::ScalarCandidate, "all-uses-scalar"});
    } else {
      // Count actual rounded slot size, not just payload, to bound stack usage.
      const std::size_t mask = alloc.alignment - 1;
      if (alloc.alignment > 64 || alloc.size_bytes > stack_remaining ||
          mask > stack_remaining - alloc.size_bytes) {
        decisions.push_back({Storage::PreserveAllocation, "stack-budget-or-alignment"});
      } else {
        const std::size_t slot = (alloc.size_bytes + mask) & ~mask;
        if (slot > stack_remaining) {
          decisions.push_back({Storage::PreserveAllocation, "stack-budget-or-alignment"});
        } else {
          stack_remaining -= slot;
          decisions.push_back({Storage::StackCandidate, "proven-local-borrow"});
        }
      }
    }
  }
  return decisions;
}
} // namespace oresforge::escape
