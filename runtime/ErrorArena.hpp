#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace oresforge::runtime {
// Private host representation for a bounded, synchronous invocation.
// This is not a guest ABI or a cross-actor handle. The invocation owner must
// outlive all handles; no recycling, reset, or transfer is admitted here.
enum class ErrorKind : std::uint8_t { Error, ArithmeticOverflow, DivisionByZero };
enum class ErrorAllocation : std::uint8_t { Created, Exhausted, MessageTooLong, InvalidKind };

template<std::size_t Slots, std::size_t MessageBytes>
class ErrorArena final {
  static_assert(Slots > 0 && MessageBytes > 0);
 public:
  class Handle final {
    friend class ErrorArena;
    const ErrorArena* owner_ = nullptr;
    std::size_t index_ = 0;
    constexpr Handle(const ErrorArena* owner, std::size_t index) noexcept
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

  ErrorArena() noexcept = default;
  ErrorArena(const ErrorArena&) = delete;
  ErrorArena& operator=(const ErrorArena&) = delete;
  ErrorArena(ErrorArena&&) = delete;
  ErrorArena& operator=(ErrorArena&&) = delete;

  [[nodiscard]] Allocation create(ErrorKind kind, std::string_view message) noexcept {
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
    return {ErrorAllocation::Created, Handle(this, index)};
  }

  // Lookups and identity comparison never dereference foreign handles.
  // Handles must not survive the destruction of their owning invocation.
  [[nodiscard]] const Record* view(Handle handle) const noexcept {
    if (handle.owner_ != this || handle.index_ >= used_) return nullptr;
    return &records_[handle.index_];
  }
  [[nodiscard]] bool same_identity(Handle left, Handle right) const noexcept {
    return view(left) != nullptr && view(right) != nullptr && left.index_ == right.index_;
  }
  [[nodiscard]] std::size_t size() const noexcept { return used_; }

 private:
  std::array<Record, Slots> records_{};
  std::size_t used_ = 0;
};
}  // namespace oresforge::runtime
