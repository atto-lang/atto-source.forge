#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

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

    void begin_dispatch() {
        if (servicing_checkpoint_)
            throw std::logic_error("reentrant cooperation dispatch reset");
        completed_ = 0;
        phase_ = 0;
    }
    std::uint64_t completed() const noexcept { return completed_; }
    std::uint32_t phase() const noexcept { return phase_; }

    // The caller must prove the committed work is bounded; an arbitrary
    // guest-directed skip could otherwise hide an unbounded loop or FFI call.
    // No scheduler, clock or callback is used between check boundaries.
    template <typename OnCheckpoint>
    bool commit_batch(std::uint32_t work, OnCheckpoint&& on_checkpoint) {
        // A scheduler continuation must not silently return a future/status
        // that this synchronous helper would discard.
        static_assert(std::is_void_v<std::invoke_result_t<OnCheckpoint&, std::uint64_t, bool>>,
                      "cooperation checkpoint callback must return void");
        if (servicing_checkpoint_)
            throw std::logic_error("reentrant cooperation charge");
        if (work == 0 || work > interval_
            throw std::invalid_argument("invalid cooperation batch");
        const std::uint32_t next = phase_ + work; // <= 1024, cannot overflow
        phase_ = next % interval_;
        if (completed_ > std::numeric_limits<std::uint64_t>::max() - work)
            completed_ = std::numeric_limits<std::uint64_t>::max();
        else
            completed_ += work;
        if (next >= interval_) {
            // Commit before invoking the trusted scheduler. Exception
            // propagation cannot replay the same boundary or leave the
            // reentrancy fence stuck in the raised state.
            servicing_checkpoint_ = true;
            struct Reset {
                bool& flag;
                ~Reset() noexcept { flag = false; }
            } reset{servicing_checkpoint_};
            on_checkpoint(completed_, completed_ >= kDispatchBudget);
            return true;
        }
        return false;
    }

private:
    const std::uint32_t interval_;
    std::uint32_t phase_ = 0;
    std::uint64_t completed_ = 0;
    bool servicing_checkpoint_ = false;
};
} // namespace oresforge::runtime
