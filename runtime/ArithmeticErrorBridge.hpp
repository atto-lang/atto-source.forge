#pragma once
#include "runtime/ErrorArena.hpp"
#include <optional>

namespace oresforge::runtime {
// Host-only adapter for the trusted scalar arithmetic boundary. A status
// code is a classification, never an object identity or a guest handle.
// Capture only a newly produced ordinary arithmetic fault; forwarding an
// existing Error must retain its original handle instead of recapturing it.
template<std::size_t Slots, std::size_t MessageBytes>
class ArithmeticErrorBridge final {
 public:
  using Arena = ErrorArena<Slots, MessageBytes>;
  using Handle = typename Arena::Handle;
  using Allocation = typename Arena::Allocation;
  explicit ArithmeticErrorBridge(Arena& arena) noexcept : arena_(arena) {}

  [[nodiscard]] Allocation capture_new(std::uint64_t trusted_status) noexcept {
    switch (trusted_status) {
      case 65537: return arena_.create(ErrorKind::ArithmeticOverflow, "integer overflow");
      case 65538: return arena_.create(ErrorKind::DivisionByZero, "division by zero");
      default: return {ErrorAllocation::InvalidKind, {}};
    }
  }

  [[nodiscard]] std::optional<Handle> forward(Handle handle) const noexcept {
    if (!arena_.view(handle)) return std::nullopt;
    return handle;
  }

  // A lossy projection for the existing public status-only inspection ABI.
  // Never represent a general Error, invalid handle or success as a fault.
  [[nodiscard]] std::optional<std::uint64_t> legacy_status(Handle handle) const noexcept {
    const auto* record = arena_.view(handle);
    if (!record) return std::nullopt;
    switch (record->kind) {
      case ErrorKind::ArithmeticOverflow: return 65537;
      case ErrorKind::DivisionByZero: return 65538;
      case ErrorKind::Error: return std::nullopt;
    }
    return std::nullopt;
  }

 private:
  Arena& arena_;
};
}  // namespace oresforge::runtime
