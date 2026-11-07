#pragma once

#include "runtime/actor.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace oresforge::runtime {

// Opaque local-only capabilities. Copying a live issued capability is permitted;
// constructing a metadata-equivalent capability is not. None are serializable.
class ActorMemoryHeapHandle {
  friend class ActorIncarnationMemoryV1;
  std::shared_ptr<const std::uint8_t> token_;
  explicit ActorMemoryHeapHandle(std::shared_ptr<const std::uint8_t> token)
      : token_(std::move(token)) {}
};
class ActorMemoryExecution {
  friend class ActorIncarnationMemoryV1;
  std::shared_ptr<const std::uint8_t> token_;
  explicit ActorMemoryExecution(std::shared_ptr<const std::uint8_t> token)
      : token_(std::move(token)) {}
};
class ActorMemoryFrameHandle {
  friend class ActorIncarnationMemoryV1;
  std::shared_ptr<const std::uint8_t> token_;
  explicit ActorMemoryFrameHandle(std::shared_ptr<const std::uint8_t> token)
      : token_(std::move(token)) {}
};
class ActorMemoryContinuation {
  friend class ActorIncarnationMemoryV1;
  std::shared_ptr<const std::uint8_t> token_;
  explicit ActorMemoryContinuation(std::shared_ptr<const std::uint8_t> token)
      : token_(std::move(token)) {}
};

// Stage-0 host substrate: one native heap and one independent native *logical*
// frame stack per mailbox incarnation. This is NOT a fiber, alternate OS stack,
// compiled .atto actor, Cranelift frame layout, or StackOverflow containment.
// The host must own the object, serialize guest turns and retire it on STOP;
// mailbox restarts automatically revoke the old capability at the next call.
class ActorIncarnationMemoryV1 final {
  struct Slot {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::shared_ptr<const std::uint8_t> issued;
    bool live = false;
  };
  struct Frame {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::shared_ptr<const std::uint8_t> issued;
  };
  Mailbox& mailbox_;
  const ActorRef owner_;
  mutable std::mutex gate_;
  std::unique_ptr<std::uint8_t[]> heap_;
  std::unique_ptr<std::uint8_t[]> stack_;
  std::size_t heap_bytes_;
  std::size_t stack_bytes_;
  std::vector<Slot> slots_;
  std::vector<Frame> frames_;
  std::size_t heap_top_ = 0;
  std::size_t stack_top_ = 0;
  std::size_t heap_live_ = 0;
  bool retired_ = false;
  bool stack_poisoned_ = false;
  bool suspended_ = false;
  std::shared_ptr<const std::uint8_t> execution_;
  std::shared_ptr<const std::uint8_t> continuation_;

  static std::shared_ptr<const std::uint8_t> issue() {
    return std::make_shared<const std::uint8_t>(0);
  }
  static void clear(std::uint8_t* buffer, std::size_t bytes) noexcept {
    // Volatile writes make sensitive byte erasure observable to the compiler.
    volatile std::uint8_t* p = buffer;
    for (std::size_t i = 0; i < bytes; ++i) p[i] = 0;
  }
  [[nodiscard]] std::unique_lock<std::mutex> authorize(
      const ActorRef& caller) const {
    if (retired_)
      throw std::runtime_error("actor incarnation memory retired");
    // Invariant: callers already own gate_. Mailbox close/restart acquires
    // only its mailbox mutex, and never calls back into actor memory.
    // The returned lock holds generation authority through the byte copy.
    return mailbox_.lock_authorized(owner_, caller);
  }
  void frame_healthy() const {
    if (stack_poisoned_)
      throw std::runtime_error("actor logical stack permanently poisoned");
  }
  [[noreturn]] void poison(const char* why) {
    stack_poisoned_ = true;
    execution_.reset();
    continuation_.reset();
    suspended_ = false;
    frames_.clear();
    stack_top_ = 0;
    if (stack_) clear(stack_.get(), stack_bytes_);
    throw std::runtime_error(why);
  }
  void execution_active(const ActorMemoryExecution& token) {
    frame_healthy();
    if (suspended_ || !execution_ || execution_ != token.token_)
      poison("stale, suspended or foreign actor execution");
  }
  static void check_range(std::size_t total, std::size_t offset,
                          std::size_t length) {
    if (offset > total || length > total - offset)
      throw std::out_of_range("actor memory byte range");
  }
  std::size_t find_heap(const ActorMemoryHeapHandle& handle) const {
    for (std::size_t i = 0; i < slots_.size(); ++i)
      if (slots_[i].live && slots_[i].issued == handle.token_) return i;
    throw std::runtime_error("stale or foreign actor heap capability");
  }
  std::size_t find_frame(const ActorMemoryFrameHandle& handle) {
    for (std::size_t i = 0; i < frames_.size(); ++i)
      if (frames_[i].issued == handle.token_) return i;
    poison("stale or foreign actor frame capability");
  }

 public:
  static constexpr std::size_t max_heap_bytes = 64U * 1024 * 1024;
  static constexpr std::size_t max_stack_bytes = 4U * 1024 * 1024;

  ActorIncarnationMemoryV1(Mailbox& mailbox, ActorRef actor,
                           std::size_t heap_bytes = 1024 * 1024,
                           std::size_t stack_bytes = 64 * 1024,
                           std::size_t heap_slots = 256,
                           std::size_t max_frames = 64)
      : mailbox_(mailbox), owner_(std::move(actor)), heap_bytes_(heap_bytes),
        stack_bytes_(stack_bytes) {
    if (!mailbox_.authorizes(owner_) || heap_bytes == 0 ||
        heap_bytes > max_heap_bytes || stack_bytes == 0 ||
        stack_bytes > max_stack_bytes || heap_slots == 0 ||
        heap_slots > 65536 || max_frames == 0 || max_frames > 4096)
      throw std::invalid_argument("invalid actor memory incarnation or quota");
    slots_.resize(heap_slots);
    frames_.reserve(max_frames);
    frame_limit_ = max_frames;
    heap_ = std::make_unique<std::uint8_t[]>(heap_bytes);
    stack_ = std::make_unique<std::uint8_t[]>(stack_bytes);
  }
  ActorIncarnationMemoryV1(const ActorIncarnationMemoryV1&) = delete;
  ActorIncarnationMemoryV1& operator=(const ActorIncarnationMemoryV1&) = delete;
  ~ActorIncarnationMemoryV1() noexcept { retire(); }

  void retire() noexcept {
    std::scoped_lock lock(gate_);
    if (retired_) return;
    retired_ = true;
    if (heap_) clear(heap_.get(), heap_bytes_);
    if (stack_) clear(stack_.get(), stack_bytes_);
    heap_.reset();
    stack_.reset();
    slots_.clear();
    frames_.clear();
    heap_live_ = 0;
    heap_top_ = stack_top_ = 0;
    execution_.reset();
    continuation_.reset();
    suspended_ = false;
  }

  ActorMemoryHeapHandle alloc(const ActorRef& actor, std::size_t bytes) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    if (!bytes || bytes > heap_bytes_ - heap_top_)
      throw std::length_error("actor heap quota");
    auto it = std::find_if(slots_.begin(), slots_.end(),
                           [](const Slot& s) { return !s.live; });
    if (it == slots_.end()) throw std::length_error("actor heap slots");
    auto capability = issue();
    const std::size_t offset = heap_top_;
    clear(heap_.get() + offset, bytes);
    it->offset = offset;
    it->length = bytes;
    it->issued = capability;
    it->live = true;
    heap_top_ += bytes;
    ++heap_live_;
    return ActorMemoryHeapHandle(std::move(capability));
  }

  void heap_write(const ActorRef& actor, const ActorMemoryHeapHandle& handle,
                  std::size_t offset, std::span<const std::uint8_t> bytes) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    const Slot& slot = slots_[find_heap(handle)];
    check_range(slot.length, offset, bytes.size());
    std::copy(bytes.begin(), bytes.end(), heap_.get() + slot.offset + offset);
  }
  void heap_read(const ActorRef& actor, const ActorMemoryHeapHandle& handle,
                 std::size_t offset, std::span<std::uint8_t> out) const {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    const Slot& slot = slots_[find_heap(handle)];
    check_range(slot.length, offset, out.size());
    std::copy_n(heap_.get() + slot.offset + offset, out.size(), out.begin());
  }
  void drop(const ActorRef& actor, const ActorMemoryHeapHandle& handle) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    Slot& slot = slots_[find_heap(handle)];
    clear(heap_.get() + slot.offset, slot.length);
    slot = Slot{};
    if (--heap_live_ == 0) {
      clear(heap_.get(), heap_top_);
      heap_top_ = 0;
    }
  }

  ActorMemoryExecution begin(const ActorRef& actor) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    frame_healthy();
    if (execution_) throw std::runtime_error("actor already has a logical stack");
    execution_ = issue();
    return ActorMemoryExecution(execution_);
  }
  ActorMemoryFrameHandle push(const ActorRef& actor,
                              const ActorMemoryExecution& execution,
                              std::size_t bytes) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    execution_active(execution);
    if (!bytes || frames_.size() == frame_limit_ ||
        bytes > stack_bytes_ - stack_top_)
      poison("actor logical frame quota exceeded");
    auto issued = issue();
    frames_.push_back(Frame{stack_top_, bytes, issued});
    clear(stack_.get() + stack_top_, bytes);
    stack_top_ += bytes;
    return ActorMemoryFrameHandle(std::move(issued));
  }
  void frame_write(const ActorRef& actor, const ActorMemoryExecution& execution,
                   const ActorMemoryFrameHandle& handle, std::size_t offset,
                   std::span<const std::uint8_t> bytes) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    execution_active(execution);
    const Frame& frame = frames_[find_frame(handle)];
    check_range(frame.length, offset, bytes.size());
    std::copy(bytes.begin(), bytes.end(), stack_.get() + frame.offset + offset);
  }
  void frame_read(const ActorRef& actor, const ActorMemoryExecution& execution,
                  const ActorMemoryFrameHandle& handle, std::size_t offset,
                  std::span<std::uint8_t> out) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    execution_active(execution);
    const Frame& frame = frames_[find_frame(handle)];
    check_range(frame.length, offset, out.size());
    std::copy_n(stack_.get() + frame.offset + offset, out.size(), out.begin());
  }
  void pop(const ActorRef& actor, const ActorMemoryExecution& execution,
           const ActorMemoryFrameHandle& handle) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    execution_active(execution);
    if (frames_.empty() || frames_.back().issued != handle.token_)
      poison("actor frame pop is not strict LIFO");
    const auto top = frames_.back();
    clear(stack_.get() + top.offset, top.length);
    stack_top_ = top.offset;
    frames_.pop_back();
  }
  void finish(const ActorRef& actor, const ActorMemoryExecution& execution) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    execution_active(execution);
    if (!frames_.empty()) poison("actor handler escaped with live frames");
    execution_.reset();
  }
  ActorMemoryContinuation suspend(const ActorRef& actor,
                                  const ActorMemoryExecution& execution) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    execution_active(execution);
    if (frames_.empty()) poison("actor cannot suspend empty stack");
    continuation_ = issue();
    suspended_ = true;
    return ActorMemoryContinuation(continuation_);
  }
  void resume(const ActorRef& actor, const ActorMemoryExecution& execution,
              const ActorMemoryContinuation& continuation) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    frame_healthy();
    if (!suspended_ || !execution_ || execution_ != execution.token_ ||
        !continuation_ || continuation_ != continuation.token_)
      poison("stale actor frame continuation");
    suspended_ = false;
    continuation_.reset();
  }
  void verify_handler_exit(const ActorRef& actor) {
    std::scoped_lock lock(gate_);
    [[maybe_unused]] auto mailbox_lease = authorize(actor);
    frame_healthy();
    if (execution_ && !suspended_)
      poison("actor handler returned with unbalanced frames");
  }
  bool poisoned() const { std::scoped_lock lock(gate_); return stack_poisoned_; }
  std::size_t depth() const { std::scoped_lock lock(gate_); return frames_.size(); }

 private:
  std::size_t frame_limit_ = 0;
};
} // namespace oresforge::runtime
