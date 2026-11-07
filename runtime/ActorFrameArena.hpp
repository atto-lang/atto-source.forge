#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace oresforge::runtime {

// Host-only bounded logical frame storage. No C++/OS stack is replaced here:
// compiler lowering and a guarded native/fiber stack remain separate work.
class ActorFrameArena final {
public:
    struct Exhausted final : std::exception {
        const char* what() const noexcept override { return "actor frame budget exceeded"; }
    };
    class Turn final {
        friend class ActorFrameArena;
        std::uint64_t domain_ = 0, epoch_ = 0, execution_ = 0;
        Turn(std::uint64_t domain, std::uint64_t epoch, std::uint64_t execution) noexcept
            : domain_(domain), epoch_(epoch), execution_(execution) {}
    public:
        Turn() noexcept = default;
    };
    class Frame final {
        friend class ActorFrameArena;
        std::uint64_t domain_ = 0, sequence_ = 0, execution_ = 0;
        Frame(std::uint64_t domain, std::uint64_t sequence, std::uint64_t execution) noexcept
            : domain_(domain), sequence_(sequence), execution_(execution) {}
    public:
        Frame() noexcept = default;
    };
    class Suspension final {
        friend class ActorFrameArena;
        std::uint64_t domain_ = 0, epoch_ = 0, execution_ = 0, top_ = 0;
        Suspension(std::uint64_t domain, std::uint64_t epoch, std::uint64_t execution,
                   std::uint64_t top) noexcept
            : domain_(domain), epoch_(epoch), execution_(execution), top_(top) {}
    public:
        Suspension() noexcept = default;
    };
    struct Access { ActorFrameArena* arena; Turn turn; };
    class ScopedCurrent final {
    public:
        ScopedCurrent(ActorFrameArena& arena, Turn turn) {
            // Callback provenance is never independently trusted as authority:
            // operations also validate active turn, thread, epoch, and domain.
            arena.validate(turn);
            if (current_arena_) throw std::logic_error("nested actor frame dispatch denied");
            current_arena_ = &arena;
            current_turn_ = turn;
        }
        ~ScopedCurrent() noexcept { current_turn_.reset(); current_arena_ = nullptr; }
        ScopedCurrent(const ScopedCurrent&) = delete;
        ScopedCurrent& operator=(const ScopedCurrent&) = delete;
    };
    static std::optional<Access> current() noexcept {
        if (!current_arena_ || !current_turn_) return std::nullopt;
        return Access{current_arena_, *current_turn_};
    }

    explicit ActorFrameArena(std::uint64_t actor, std::uint64_t generation,
                             std::size_t frame_limit = 64, std::size_t byte_limit = 64 * 1024)
        : limit_frames_(frame_limit),
          limit_bytes_(byte_limit),
          domain_(validated_domain(actor, generation, frame_limit, byte_limit)),
          storage_(new std::uint8_t[byte_limit]()), frames_(frame_limit) {}
    ActorFrameArena(const ActorFrameArena&) = delete;
    ActorFrameArena& operator=(const ActorFrameArena&) = delete;
    ~ActorFrameArena() noexcept { erase_all(); }

    Turn enter(std::uint64_t execution) {
        std::scoped_lock lock(mu_);
        if (retired_ || poisoned_ || suspended_ || depth_ || !execution)
            throw std::logic_error("actor frames are not eligible");
        if (active_) throw std::logic_error("actor frame carrier busy");
        advance_epoch();
        execution_ = execution;
        carrier_ = std::this_thread::get_id();
        active_ = true;
        return Turn(domain_, epoch_, execution_);
    }
    Frame push(Turn turn, std::size_t bytes) {
        std::scoped_lock lock(mu_);
        verify_turn(turn);
        if (!bytes || depth_ == limit_frames_ || bytes > limit_bytes_ - used_ ||
            sequence_ == std::numeric_limits<std::uint64_t>::max()) {
            poisoned_ = true;
            throw Exhausted{};
        }
        Entry& e = frames_[depth_++];
        e = Entry{used_, bytes, ++sequence_, execution_};
        used_ += bytes;
        return Frame(domain_, e.sequence, execution_);
    }
    void write(Turn turn, Frame frame, std::size_t offset, std::uint8_t value) {
        std::scoped_lock lock(mu_);
        verify_turn(turn);
        const auto& e = verify_frame(frame);
        if (offset >= e.size) fail_protocol();
        storage_[e.offset + offset] = value;
    }
    std::uint8_t read(Turn turn, Frame frame, std::size_t offset) {
        std::scoped_lock lock(mu_);
        verify_turn(turn);
        const auto& e = verify_frame(frame);
        if (offset >= e.size) fail_protocol();
        return storage_[e.offset + offset];
    }
    void pop(Turn turn, Frame frame) {
        std::scoped_lock lock(mu_);
        verify_turn(turn);
        const auto& e = verify_frame(frame);
        if (&e != &frames_[depth_ - 1]) fail_protocol();
        erase_range(e.offset, e.size);
        used_ = e.offset;
        frames_[--depth_] = Entry{};
    }
    void leave(Turn turn) {
        std::scoped_lock lock(mu_);
        verify_turn(turn);
        if (depth_ != 0) fail_protocol(); // a synchronous callback leaked a frame
        active_ = false;
        execution_ = 0;
        carrier_ = std::thread::id{};
        advance_epoch();
    }
    Suspension suspend(Turn turn) {
        std::scoped_lock lock(mu_);
        verify_turn(turn);
        if (!depth_) fail_protocol();
        active_ = false;
        suspended_ = true;
        carrier_ = std::thread::id{};
        advance_epoch();
        return Suspension(domain_, epoch_, execution_, frames_[depth_ - 1].sequence);
    }
    Turn resume(Suspension continuation) {
        std::scoped_lock lock(mu_);
        if (retired_ || poisoned_ || !suspended_ || active_ || !depth_ ||
            continuation.domain_ != domain_ || continuation.epoch_ != epoch_ ||
            continuation.execution_ != execution_ ||
            continuation.top_ != frames_[depth_ - 1].sequence)
            fail_protocol();
        advance_epoch();
        active_ = true;
        suspended_ = false;
        carrier_ = std::this_thread::get_id();
        return Turn(domain_, epoch_, execution_);
    }
    bool cancel_quiesced() noexcept {
        std::scoped_lock lock(mu_);
        if (active_) return false;
        erase_all();
        suspended_ = false;
        execution_ = 0;
        advance_epoch_noexcept();
        return true;
    }
    bool retire_quiesced() noexcept {
        std::scoped_lock lock(mu_);
        if (active_) return false;
        erase_all();
        suspended_ = false;
        execution_ = 0;
        retired_ = true;
        advance_epoch_noexcept();
        return true;
    }
    bool poisoned() const noexcept { std::scoped_lock lock(mu_); return poisoned_; }
    bool retired() const noexcept { std::scoped_lock lock(mu_); return retired_; }
    std::size_t used_bytes() const noexcept { std::scoped_lock lock(mu_); return used_; }
    std::size_t depth() const noexcept { std::scoped_lock lock(mu_); return depth_; }
    void validate(Turn turn) {
        std::scoped_lock lock(mu_);
        verify_turn(turn);
    }
private:
    struct Entry {
        std::size_t offset = 0, size = 0;
        std::uint64_t sequence = 0, execution = 0;
    };
    static std::uint64_t validated_domain(std::uint64_t actor, std::uint64_t generation,
                                          std::size_t frames, std::size_t bytes) {
        if (!actor || !generation || !frames || frames > 4096 ||
            !bytes || bytes > 4 * 1024 * 1024)
            throw std::invalid_argument("unbounded actor frame arena");
        return next_domain();
    }
    static std::uint64_t next_domain() {
        static std::atomic<std::uint64_t> next{1};
        const auto id = next.fetch_add(1, std::memory_order_relaxed);
        if (id == 0 || id == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("frame domains exhausted");
        return id;
    }
    [[noreturn]] void fail_protocol() {
        poisoned_ = true;
        throw std::logic_error("actor frame protocol or ownership violation");
    }
    void verify_turn(Turn turn) {
        if (!active_ || suspended_ || retired_ || poisoned_ ||
            turn.domain_ != domain_ || turn.epoch_ != epoch_ ||
            turn.execution_ != execution_ || carrier_ != std::this_thread::get_id())
            fail_protocol();
    }
    const Entry& verify_frame(Frame frame) {
        if (frame.domain_ != domain_ || frame.execution_ != execution_)
            fail_protocol();
        for (std::size_t i = 0; i < depth_; ++i)
            if (frames_[i].sequence == frame.sequence_) return frames_[i];
        fail_protocol();
    }
    void erase_range(std::size_t offset, std::size_t count) noexcept {
        volatile std::uint8_t* p = storage_.get();
        for (std::size_t i = offset; i < offset + count; ++i) p[i] = 0;
    }
    void erase_all() noexcept {
        if (storage_) erase_range(0, limit_bytes_);
        for (auto& e : frames_) e = Entry{};
        used_ = depth_ = 0;
    }
    void advance_epoch() {
        if (epoch_ == std::numeric_limits<std::uint64_t>::max()) fail_protocol();
        ++epoch_;
    }
    void advance_epoch_noexcept() noexcept {
        if (epoch_ == std::numeric_limits<std::uint64_t>::max()) poisoned_ = true;
        else ++epoch_;
    }
    // ScalarActor invokes this only on the currently admitted carrier after a
    // thrown callback. It stops the actor, rather than letting a catch resume it.
    void abort_active_turn() noexcept {
        std::scoped_lock lock(mu_);
        erase_all();
        active_ = suspended_ = false;
        execution_ = 0;
        carrier_ = std::thread::id{};
        poisoned_ = true;
        advance_epoch_noexcept();
    }
    friend class ScalarActor;
    const std::size_t limit_frames_, limit_bytes_;
    const std::uint64_t domain_;
    std::unique_ptr<std::uint8_t[]> storage_;
    std::vector<Entry> frames_;
    mutable std::mutex mu_;
    std::size_t used_ = 0, depth_ = 0;
    std::uint64_t sequence_ = 0, epoch_ = 0, execution_ = 0;
    bool active_ = false, suspended_ = false, poisoned_ = false, retired_ = false;
    std::thread::id carrier_{};
    inline static thread_local ActorFrameArena* current_arena_ = nullptr;
    inline static thread_local std::optional<Turn> current_turn_ = std::nullopt;
};

} // namespace oresforge::runtime
