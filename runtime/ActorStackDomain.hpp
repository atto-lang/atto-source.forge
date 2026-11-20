#pragma once

// Native Forge host-runtime substrate: Linux x86-64 only. This is NOT
// isolation from malicious native code, source-actor lowering or self-hosting.
#if !defined(__linux__) || !defined(__x86_64__)
#error "ActorStackDomain v1 requires Linux x86-64; other targets must fail closed"
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <ucontext.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
#define ORESFORGE_ACTOR_ASAN 1
extern "C" void __sanitizer_start_switch_fiber(void**, const void*, std::size_t);
extern "C" void __sanitizer_finish_switch_fiber(void*, const void**, std::size_t*);
#else
#define ORESFORGE_ACTOR_ASAN 0
#endif

namespace oresforge::runtime {

// An opaque logical heap allocation, never a transferable pointer.
// Its fields cannot be synthesized by native users through the public API.
class ActorHeapHandle final {
  friend class ActorStackDomain;
  const void* owner_ = nullptr;
  std::uint64_t incarnation_ = 0;
  std::uint32_t slot_ = 0;
  std::uint64_t version_ = 0;
  ActorHeapHandle(const void* owner, std::uint64_t incarnation,
                  std::uint32_t slot, std::uint64_t version) noexcept
      : owner_(owner), incarnation_(incarnation), slot_(slot), version_(version) {}
 public:
  ActorHeapHandle() = default;
};

class ActorStackDomain final {
 public:
  static constexpr std::size_t kMinStackBytes = 64 * 1024;
  static constexpr std::size_t kMaxStackBytes = 8 * 1024 * 1024;
  static constexpr std::size_t kMaxHeapBytes = 16 * 1024 * 1024;
  static constexpr std::size_t kMaxSlots = 1024;

  ActorStackDomain(std::uint64_t actor_id, std::size_t stack_bytes,
                   std::size_t heap_bytes)
      : actor_id_(actor_id), owner_thread_(std::this_thread::get_id()) {
    if (actor_id == 0 || stack_bytes < kMinStackBytes ||
        stack_bytes > kMaxStackBytes || heap_bytes == 0 ||
        heap_bytes > kMaxHeapBytes)
      throw std::invalid_argument("invalid actor memory budget");
    const auto page = ::sysconf(_SC_PAGESIZE);
    if (page <= 0) throw std::runtime_error("page size unavailable");
    page_size_ = static_cast<std::size_t>(page);
    if (stack_bytes > kMaxStackBytes - (page_size_ - 1))
      throw std::invalid_argument("actor stack rounding overflow");
    stack_size_ = ((stack_bytes + page_size_ - 1) / page_size_) * page_size_;
    if (stack_size_ > std::numeric_limits<std::size_t>::max() - 2 * page_size_)
      throw std::invalid_argument("actor stack address overflow");
    map_size_ = stack_size_ + 2 * page_size_;
    void* region = ::mmap(nullptr, map_size_, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) throw std::runtime_error("actor stack mmap failed");
    region_ = region;
    stack_base_ = static_cast<std::byte*>(region_) + page_size_;
    if (::mprotect(stack_base_, stack_size_, PROT_READ | PROT_WRITE) != 0) {
      ::munmap(region_, map_size_);
      region_ = nullptr;
      throw std::runtime_error("actor stack mprotect failed");
    }
    try {
      heap_ = std::make_unique<std::byte[]>(heap_bytes);
      std::memset(heap_.get(), 0, heap_bytes);
      heap_size_ = heap_bytes;
      const auto next = next_incarnation_.fetch_add(1, std::memory_order_relaxed);
      if (next == 0 || next == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("actor incarnation exhausted");
      incarnation_ = next;
    } catch (...) {
      ::munmap(region_, map_size_);
      region_ = nullptr;
      throw;
    }
  }

  ActorStackDomain(const ActorStackDomain&) = delete;
  ActorStackDomain& operator=(const ActorStackDomain&) = delete;
  ActorStackDomain(ActorStackDomain&&) = delete;
  ActorStackDomain& operator=(ActorStackDomain&&) = delete;
  ~ActorStackDomain() noexcept {
    // Destroying a live stack from its own execution context would unmap
    // return addresses. Concurrent destruction also requires owner fencing.
    if (active_ == this || in_turn_) std::terminate();
    scrub_and_unmap();
  }

  std::uint64_t actor_id() const noexcept { return actor_id_; }
  std::size_t stack_capacity() const noexcept { return stack_size_; }
  std::size_t heap_capacity() const noexcept { return heap_size_; }
  bool contains_stack_address(const void* addr) const noexcept {
    const auto p = reinterpret_cast<std::uintptr_t>(addr);
    const auto b = reinterpret_cast<std::uintptr_t>(stack_base_);
    return p >= b && p - b < stack_size_;
  }

  // Every callback runs on this actor's separately mapped and guarded stack.
  // Callbacks must return; cooperative suspension is a future contract.
  void run_turn(std::function<void(ActorStackDomain&)> callback) {
    require_owner();
    if (!alive_ || in_turn_ || active_ != nullptr || !callback)
      throw std::logic_error("actor turn admission denied");
    callback_ = std::move(callback);
    callback_error_ = nullptr;
    if (::getcontext(&actor_ctx_) != 0) {
      callback_ = {};
      throw std::runtime_error("actor context capture failed");
    }
    actor_ctx_.uc_stack.ss_sp = stack_base_;
    actor_ctx_.uc_stack.ss_size = stack_size_;
    actor_ctx_.uc_stack.ss_flags = 0;
    actor_ctx_.uc_link = &caller_ctx_;
    ::makecontext(&actor_ctx_, &ActorStackDomain::trampoline, 0);
    in_turn_ = true;
    active_ = this;
#if ORESFORGE_ACTOR_ASAN
    __sanitizer_start_switch_fiber(&caller_fake_stack_, stack_base_, stack_size_);
#endif
    if (::swapcontext(&caller_ctx_, &actor_ctx_) != 0) {
      active_ = nullptr;
      in_turn_ = false;
      callback_ = {};
      throw std::runtime_error("actor context switch failed");
    }
#if ORESFORGE_ACTOR_ASAN
    __sanitizer_finish_switch_fiber(caller_fake_stack_, nullptr, nullptr);
#endif
    active_ = nullptr;
    in_turn_ = false;
    callback_ = {};
    if (callback_error_) std::rethrow_exception(callback_error_);
  }

  ActorHeapHandle allocate(std::size_t bytes) {
    require_turn();
    if (bytes == 0 || used_ > heap_size_ || bytes > heap_size_ - used_ ||
        slots_used_ >= kMaxSlots)
      throw std::length_error("actor heap exhausted");
    const auto slot = static_cast<std::uint32_t>(slots_used_++);
    slots_[slot] = Slot{used_, bytes, 1, true};
    used_ += bytes;
    return ActorHeapHandle(this, incarnation_, slot, 1);
  }
  void write(ActorHeapHandle h, std::size_t offset, const void* src, std::size_t bytes) {
    Slot& s = checked_slot(h);
    if (offset > s.size || bytes > s.size - offset || (bytes && !src))
      throw std::out_of_range("actor heap write bounds");
    if (bytes) std::memcpy(heap_.get() + s.offset + offset, src, bytes);
  }
  void read(ActorHeapHandle h, std::size_t offset, void* dst, std::size_t bytes) {
    Slot& s = checked_slot(h);
    if (offset > s.size || bytes > s.size - offset || (bytes && !dst))
      throw std::out_of_range("actor heap read bounds");
    if (bytes) std::memcpy(dst, heap_.get() + s.offset + offset, bytes);
  }
  void release(ActorHeapHandle h) {
    Slot& s = checked_slot(h);
    secure_wipe(heap_.get() + s.offset, s.size);
    s.live = false;
  }
  // Stop only after a completed turn; no outstanding native callback may use
  // this domain. Nothing is allowed to run after STOP.
  void stop() {
    require_owner();
    if (active_ == this || in_turn_) throw std::logic_error("cannot stop live actor");
    if (!alive_) return;
    scrub_and_unmap();
  }

 private:
  struct Slot {std::size_t offset = 0, size = 0; std::uint64_t version = 0; bool live = false;};
  static inline std::atomic<std::uint64_t> next_incarnation_{1};
  static inline thread_local ActorStackDomain* active_ = nullptr;
  std::uint64_t actor_id_, incarnation_ = 0;
  const std::thread::id owner_thread_;
  std::size_t page_size_ = 0, stack_size_ = 0, map_size_ = 0;
  void* region_ = nullptr;
  std::byte* stack_base_ = nullptr;
  std::unique_ptr<std::byte[]> heap_;
  std::size_t heap_size_ = 0, used_ = 0, slots_used_ = 0;
  std::array<Slot, kMaxSlots> slots_{};
  ucontext_t actor_ctx_{}, caller_ctx_{};
  std::function<void(ActorStackDomain&)> callback_;
  std::exception_ptr callback_error_;
#if ORESFORGE_ACTOR_ASAN
  void* caller_fake_stack_ = nullptr;
  const void* caller_stack_base_ = nullptr;
  std::size_t caller_stack_bytes_ = 0;
#endif
  bool in_turn_ = false, alive_ = true;

  static void secure_wipe(void* ptr, std::size_t len) noexcept {
    auto* p = static_cast<volatile unsigned char*>(ptr);
    while (len--) *p++ = 0;
  }
  void scrub_and_unmap() noexcept {
    if (heap_) secure_wipe(heap_.get(), heap_size_);
    heap_.reset();
    heap_size_ = used_ = slots_used_ = 0;
    if (region_) {
      secure_wipe(stack_base_, stack_size_);
      ::munmap(region_, map_size_);
      region_ = nullptr;
      stack_base_ = nullptr;
    }
    alive_ = false;
  }
  void require_owner() const {
    if (std::this_thread::get_id() != owner_thread_)
      throw std::logic_error("actor stack cannot migrate between carriers");
  }
  void require_turn() const {
    if (!alive_ || active_ != this || !in_turn_)
      throw std::logic_error("actor heap requires own active stack turn");
  }
  Slot& checked_slot(ActorHeapHandle h) {
    require_turn();
    if (h.owner_ != this || h.incarnation_ != incarnation_ ||
        h.slot_ >= slots_used_) throw std::logic_error("foreign/stale actor heap handle");
    Slot& s = slots_[h.slot_];
    if (!s.live || s.version != h.version_)
      throw std::logic_error("retired actor heap handle");
    return s;
  }
  static void trampoline() noexcept {
    auto* self = active_;
    if (!self) std::terminate();
#if ORESFORGE_ACTOR_ASAN
    __sanitizer_finish_switch_fiber(nullptr, &self->caller_stack_base_,
                                    &self->caller_stack_bytes_);
#endif
    try { self->callback_(*self); }
    catch (...) { self->callback_error_ = std::current_exception(); }
#if ORESFORGE_ACTOR_ASAN
    __sanitizer_start_switch_fiber(nullptr, self->caller_stack_base_,
                                   self->caller_stack_bytes_);
#endif
    // Returning through uc_link restores caller context on its own stack.
  }
};
} // namespace oresforge::runtime
