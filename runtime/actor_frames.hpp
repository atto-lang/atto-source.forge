#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>
#include <utility>
#include <cstdlib>

namespace oresforge::runtime {
// Host-owned logical actor frames. This is NOT an alternative machine stack:
// native Forge callbacks still use the current OS carrier's C++ call stack.
// Separate actor storage is bounded, zeroized and survives a suspended turn.
class ActorFrameStack final {
    struct Authority final {};
public:
    static constexpr std::size_t max_frames = 64;
    static constexpr std::size_t max_bytes = 64 * 1024;
    class Token final {
        std::shared_ptr<const Authority> authority_;
        std::uint64_t epoch_ = 0;
        Token(std::shared_ptr<const Authority> a, std::uint64_t e)
            : authority_(std::move(a)), epoch_(e) {}
        friend class ActorFrameStack;
    public:
        Token() = default; // invalid until issued by the active carrier
    };
    class Frame final {
        std::shared_ptr<const Authority> authority_;
        std::size_t index_ = 0;
        std::uint64_t serial_ = 0;
        Frame(std::shared_ptr<const Authority> a, std::size_t i, std::uint64_t s)
            : authority_(std::move(a)), index_(i), serial_(s) {}
        friend class ActorFrameStack;
    public:
        Frame() = default; // invalid until push()
    };
    class Turn final {
        ActorFrameStack& stack_;
        std::uint64_t epoch_ = 0;
    public:
        explicit Turn(ActorFrameStack& stack) : stack_(stack) {
            std::lock_guard lock(stack_.mutex_);
            if (stack_.active_ || stack_.retired_ || stack_.poisoned_)
                throw std::logic_error("actor frame turn denied");
            if (stack_.next_epoch_ == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("actor frame turn epoch exhausted");
            epoch_ = ++stack_.next_epoch_;
            stack_.carrier_ = std::this_thread::get_id();
            stack_.active_ = true;
        }
        Turn(const Turn&) = delete;
        Turn& operator=(const Turn&) = delete;
        Turn(Turn&&) = delete;
        Turn& operator=(Turn&&) = delete;
        ~Turn() noexcept {
            std::lock_guard lock(stack_.mutex_);
            if (!stack_.active_ || stack_.next_epoch_ != epoch_ ||
                stack_.carrier_ != std::this_thread::get_id()) std::abort();
            stack_.active_ = false;
            stack_.carrier_ = {};
            if (stack_.retired_) stack_.wipe_all();
        }
    };

    ActorFrameStack(std::uint64_t actor_id, std::uint64_t incarnation,
                    std::size_t frames = max_frames, std::size_t bytes = max_bytes)
        : storage_(validate(actor_id, incarnation, frames, bytes), 0), slots_(frames),
          authority_(std::make_shared<Authority>()) {}
    ActorFrameStack(const ActorFrameStack&) = delete;
    ActorFrameStack& operator=(const ActorFrameStack&) = delete;
    ~ActorFrameStack() noexcept {
        std::lock_guard lock(mutex_);
        if (active_) std::abort(); // scheduler must quiesce its guest callbacks
        wipe_all();
    }

    // Only the current admitted carrier may obtain a token. ActorRefs and
    // arbitrary numeric actor ids never grant frame-storage authority.
    Token current_turn() const {
        std::lock_guard lock(mutex_);
        if (!active_ || retired_ || poisoned_ || carrier_ != std::this_thread::get_id())
            throw std::logic_error("no active actor frame turn");
        return Token(authority_, next_epoch_);
    }
    Frame push(const Token& token, std::size_t bytes) {
        std::lock_guard lock(mutex_);
        check_turn(token);
        if (bytes == 0 || depth_ == slots_.size() ||
            bytes > storage_.size() - used_ ||
            slots_[depth_].serial == std::numeric_limits<std::uint64_t>::max()) {
            poison();
            throw std::length_error("actor frame capacity exceeded");
        }
        auto& slot = slots_[depth_];
        slot.start = used_;
        slot.size = bytes;
        ++slot.serial;
        used_ += bytes;
        return Frame(authority_, depth_++, slot.serial);
    }
    void pop(const Token& token, const Frame& frame) {
        std::lock_guard lock(mutex_);
        check_turn(token);
        check_frame(frame);
        if (frame.index_ + 1 != depth_) {
            poison();
            throw std::logic_error("actor frame stack order violated");
        }
        const auto& slot = slots_[--depth_];
        wipe_bytes(slot.start, slot.size);
        used_ = slot.start;
    }
    void write(const Token& token, const Frame& frame,
               std::size_t offset, std::span<const std::uint8_t> input) {
        std::lock_guard lock(mutex_);
        check_turn(token);
        const auto& slot = check_frame(frame);
        if (offset > slot.size || input.size() > slot.size - offset)
            throw std::out_of_range("actor frame write outside bounds");
        std::copy(input.begin(), input.end(), storage_.begin() +
                  static_cast<std::ptrdiff_t>(slot.start + offset));
    }
    void read(const Token& token, const Frame& frame,
              std::size_t offset, std::span<std::uint8_t> output) const {
        std::lock_guard lock(mutex_);
        check_turn(token);
        const auto& slot = check_frame(frame);
        if (offset > slot.size || output.size() > slot.size - offset)
            throw std::out_of_range("actor frame read outside bounds");
        std::copy_n(storage_.begin() + static_cast<std::ptrdiff_t>(slot.start + offset),
                    output.size(), output.begin());
    }
    void retire() noexcept {
        std::lock_guard lock(mutex_);
        retired_ = true; // revoke access before waiting for the active turn
        if (!active_) wipe_all();
    }
    bool retired() const noexcept {
        std::lock_guard lock(mutex_);
        return retired_;
    }
    bool poisoned() const noexcept {
        std::lock_guard lock(mutex_);
        return poisoned_;
    }
    std::size_t depth(const Token& token) const {
        std::lock_guard lock(mutex_);
        check_turn(token);
        return depth_;
    }
private:
    struct Slot {std::size_t start = 0, size = 0; std::uint64_t serial = 0;};
    static std::size_t validate(std::uint64_t id, std::uint64_t incarnation,
                                  std::size_t frames, std::size_t bytes) {
        if (!id || !incarnation || !frames || frames > max_frames ||
            !bytes || bytes > max_bytes)
            throw std::invalid_argument("invalid actor frame limits or identity");
        return bytes;
    }
    void check_turn(const Token& token) const {
        if (!active_ || retired_ || poisoned_ || token.authority_ != authority_ ||
            token.epoch_ != next_epoch_ || carrier_ != std::this_thread::get_id())
            throw std::logic_error("stale, foreign or inactive actor frame turn");
    }
    const Slot& check_frame(const Frame& frame) const {
        if (frame.authority_ != authority_ || frame.index_ >= depth_ ||
            slots_[frame.index_].serial != frame.serial_)
            throw std::logic_error("stale or foreign actor frame");
        return slots_[frame.index_];
    }
    void wipe_bytes(std::size_t start, std::size_t len) noexcept {
        volatile std::uint8_t* dest = storage_.data();
        for (std::size_t i = 0; i < len; ++i) dest[start + i] = 0;
    }
    void wipe_all() noexcept {
        wipe_bytes(0, storage_.size());
        depth_ = used_ = 0;
    }
    void poison() noexcept {
        poisoned_ = true; // cannot reacquire an erroneous actor incarnation
        wipe_all();
    }
    mutable std::mutex mutex_;
    std::vector<std::uint8_t> storage_;
    std::vector<Slot> slots_;
    const std::shared_ptr<const Authority> authority_;
    std::thread::id carrier_{};
    std::uint64_t next_epoch_ = 0;
    std::size_t depth_ = 0, used_ = 0;
    bool active_ = false, retired_ = false, poisoned_ = false;
};
} // namespace oresforge::runtime
