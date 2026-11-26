#pragma once
#include <array>
#include <atomic>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace oresforge::runtime {
// Private host representation for a bounded, synchronous invocation.
// This is not a guest ABI or a cross-actor handle. The invocation owner must
// outlive record views. Handles retain only a non-recycled domain identity;
// stale handles cannot resolve against a later arena at the same address.
// Arena mutation and view use are confined to the owning invocation.
enum class ErrorKind : std::uint8_t { Error, ArithmeticOverflow, DivisionByZero };
enum class ErrorAllocation : std::uint8_t { Created, Exhausted, MessageTooLong, InvalidKind, IdentityExhausted };

template<std::size_t Slots, std::size_t MessageBytes>
class ErrorArena final {
  static_assert(Slots > 0 && MessageBytes > 0);
 public:
  class Handle final {
    friend class ErrorArena;
    std::uint64_t owner_ = 0;
    std::size_t index_ = 0;
    constexpr Handle(std::uint64_t owner, std::size_t index) noexcept
        : owner_(owner), index_(index) {}
   public:
    constexpr Handle() noexcept = default;
  };
  static_assert(std::is_trivially_copyable_v<Handle>);
  struct Record final {
    ErrorKind kind = ErrorKind::Error;
    std::size_t message_size = 0;
    std::array<char, MessageBytes> message_bytes{};
    [[nodiscard]] std::string_view message() const noexcept {
      return {message_bytes.data(), message_size};
    }
  };
  struct Allocation final {
    ErrorAllocation status;
    Handle handle;
  };

  ErrorArena() noexcept : identity_(next_identity()) {}
  ErrorArena(const ErrorArena&) = delete;
  ErrorArena& operator=(const ErrorArena&) = delete;
  ErrorArena(ErrorArena&&) = delete;
  ErrorArena& operator=(ErrorArena&&) = delete;

  [[nodiscard]] Allocation create(ErrorKind kind, std::string_view message) noexcept {
    if (identity_ == 0) return {ErrorAllocation::IdentityExhausted, {}};
    switch (kind) {
      case ErrorKind::Error:
      case ErrorKind::ArithmeticOverflow:
      case ErrorKind::DivisionByZero: break;
      default: return {ErrorAllocation::InvalidKind, {}};
    }
    if (message.size() > MessageBytes) return {ErrorAllocation::MessageTooLong, {}};
    if (used_ == Slots) return {ErrorAllocation::Exhausted, {}};
    const auto index = used_;
    auto& record = records_[index];
    record.kind = kind;
    record.message_size = message.size();
    for (std::size_t i = 0; i < message.size(); ++i) record.message_bytes[i] = message[i];
    ++used_;
    return {ErrorAllocation::Created, Handle(identity_, index)};
  }

  // Lookups and identity comparison never dereference an owning arena pointer.
  // An expired handle cannot resolve in a later incarnation of this storage.
  [[nodiscard]] const Record* view(Handle handle) const noexcept {
    if (identity_ == 0 || handle.owner_ != identity_ || handle.index_ >= used_) return nullptr;
    return &records_[handle.index_];
  }
  [[nodiscard]] bool same_identity(Handle left, Handle right) const noexcept {
    return view(left) != nullptr && view(right) != nullptr && left.index_ == right.index_;
  }
  [[nodiscard]] std::size_t size() const noexcept { return used_; }

 private:
  // One monotonic identity domain per Handle type. Distinct template
  // instantiations cannot exchange handles. Saturate instead of wrapping.
  inline static std::atomic<std::uint64_t> next_{1};
  static std::uint64_t next_identity() noexcept {
    auto candidate = next_.load(std::memory_order_relaxed);
    while (candidate != std::numeric_limits<std::uint64_t>::max()) {
      if (next_.compare_exchange_weak(candidate, candidate + 1,
                                      std::memory_order_relaxed,
                                      std::memory_order_relaxed))
        return candidate;
    }
    return 0;
  }
  const std::uint64_t identity_;
  std::array<Record, Slots> records_{};
  std::size_t used_ = 0;
};
}  // namespace oresforge::runtime
