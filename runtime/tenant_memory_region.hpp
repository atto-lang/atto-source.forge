#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>

namespace oresforge::runtime {
// Trusted host-side memory authority, NOT an untrusted native-code sandbox.
// Generated protected guest loads/stores are forbidden until the compiler
// independently proves that all accesses go through equivalent checks.
class TenantMemoryRegion final {
  struct State final {
    explicit State(std::uint32_t size, std::uint64_t generation)
        : bytes(size, 0), generation(generation) {}
    std::vector<std::uint8_t> bytes;
    const std::uint64_t generation;
    bool retired = false;
    std::mutex mutex;
    ~State() { std::fill(bytes.begin(), bytes.end(), std::uint8_t{0}); }
  };
 public:
  class Lease final {
    friend class TenantMemoryRegion;
    Lease(const std::shared_ptr<State>& state, std::uint32_t offset,
          std::uint32_t length, bool writable)
        : owner_(state), generation_(state->generation), offset_(offset),
          length_(length), writable_(writable) {}
    std::weak_ptr<State> owner_;
    std::uint64_t generation_;
    std::uint32_t offset_, length_;
    bool writable_;
  public:
    Lease(const Lease&) = default;
    Lease& operator=(const Lease&) = default;
  };

  explicit TenantMemoryRegion(std::uint32_t capacity) {
    if (capacity == 0 || capacity > 16u * 1024u * 1024u)
      throw std::invalid_argument("invalid tenant region capacity");
    auto generation = next_generation_.fetch_add(1, std::memory_order_relaxed);
    if (generation == 0 || generation == UINT64_MAX)
      throw std::overflow_error("tenant region generation exhausted");
    state_ = std::make_shared<State>(capacity, generation);
  }
  TenantMemoryRegion(const TenantMemoryRegion&) = delete;
  TenantMemoryRegion& operator=(const TenantMemoryRegion&) = delete;

  Lease grant(std::uint32_t offset, std::uint32_t length, bool writable) const {
    std::lock_guard lock(state_->mutex);
    require_live();
    const auto size = state_->bytes.size();
    if (offset > size || length > size - offset)
      throw std::out_of_range("tenant region grant outside bounds");
    return Lease(state_, offset, length, writable);
  }

  std::vector<std::uint8_t> read(const Lease& lease,
                                 std::uint32_t relative, std::uint32_t size) const {
    std::lock_guard lock(state_->mutex);
    validate(lease, relative, size, false);
    const auto begin = state_->bytes.begin() + lease.offset_ + relative;
    return {begin, begin + size};
  }

  void write(const Lease& lease, std::uint32_t relative,
             std::span<const std::uint8_t> bytes) {
    std::lock_guard lock(state_->mutex);
    if (bytes.size() > UINT32_MAX)
      throw std::out_of_range("tenant write too large");
    validate(lease, relative, static_cast<std::uint32_t>(bytes.size()), true);
    std::copy(bytes.begin(), bytes.end(), state_->bytes.begin() + lease.offset_ + relative);
  }

  void retire() {
    std::lock_guard lock(state_->mutex);
    if (state_->retired) throw std::logic_error("tenant region already retired");
    state_->retired = true;
    std::fill(state_->bytes.begin(), state_->bytes.end(), std::uint8_t{0});
  }

 private:
  void require_live() const {
    if (state_->retired) throw std::logic_error("tenant region retired");
  }
  void validate(const Lease& lease, std::uint32_t relative,
                std::uint32_t size, bool write) const {
    require_live();
    auto owner = lease.owner_.lock();
    if (!owner || owner != state_ || lease.generation_ != state_->generation)
      throw std::logic_error("foreign or stale tenant lease");
    if (write && !lease.writable_)
      throw std::logic_error("tenant lease is read-only");
    if (relative > lease.length_ || size > lease.length_ - relative)
      throw std::out_of_range("tenant lease bounds violation");
  }
  inline static std::atomic<std::uint64_t> next_generation_{1};
  std::shared_ptr<State> state_;
};
} // namespace oresforge::runtime
