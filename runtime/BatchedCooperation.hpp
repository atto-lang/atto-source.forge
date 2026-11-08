#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace oresforge::runtime {
// Task-owned bounded reduction accounting. This is NOT a guest continuation,
// OS preemptor or substitute for the untrusted-tenant isolation boundary.
class BatchedCooperation final {
public:
    static constexpr std::uint32_t kDispatchBudget = 512;
    explicit BatchedCooperation(std::uint32_t interval = 128) : interval_(interval) {
        if (interval != 128 && interval != 512)
            throw std::invalid_argument("cooperation interval must be 128 or 512");
    }
    BatchedCooperation(const BatchedCooperation&) = delete;
    BatchedCooperation& operator=(const BatchedCooperation&) = delete;

    void begin_dispatch() noexcept { completed_ = 0; phase_ = 0; }
    std::uint64_t completed() const noexcept { return completed_; }
    std::uint32_t phase() const noexcept { return phase_; }

    // The caller must prove the committed work is bounded; an arbitrary
    // guest-directed skip could otherwise hide an unbounded loop or FFI call.
    // No scheduler, clock or callback is used between check boundaries.
    template <typename OnCheckpoint>
    bool commit_batch(std::uint32_t work, OnCheckpoint&& on_checkpoint) {
        if (work == 0 || work > interval_)
            throw std::invalid_argument("invalid cooperation batch");
        const std::uint32_t next = phase_ + work; // <= 1024, cannot overflow
        phase_ = next % interval_;
        if (completed_ > std::numeric_limits<std::uint64_t>::max() - work)
            completed_ = std::numeric_limits<std::uint64_t>::max();
        else
            completed_ += work;
        if (next >= interval_) {
            on_checkpoint(completed_, completed_ >= kDispatchBudget);
            return true;
        }
        return false;
    }

private:
    const std::uint32_t interval_;
    std::uint32_t phase_ = 0;
    std::uint64_t completed_ = 0;
};
} // namespace oresforge::runtime
