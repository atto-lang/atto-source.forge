#pragma once

// Trusted host-only actor-incarnation lifecycle adapter. This is NOT a
// compiler-backed .atto actor scheduler, FFI boundary or OS stack isolation.
#include "runtime/ActorIncarnationMemoryV1.hpp"
#include "runtime/actor_arena.hpp"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace oresforge::runtime {

class ActorIncarnationOwnerV1 final {
 public:
  class Turn final {
    friend class ActorIncarnationOwnerV1;
    ActorIncarnationOwnerV1& host_;
    const ActorRef actor_;
    const ActorArena::Turn physical_;
    Turn(ActorIncarnationOwnerV1& host, ActorRef actor, ActorArena::Turn physical)
        : host_(host), actor_(std::move(actor)), physical_(physical) {}
   public:
    Turn(const Turn&) = delete;
    Turn& operator=(const Turn&) = delete;
    ActorRef actor() const { return actor_; }

    // All native access is charged to the exact live physical actor turn.
    // No raw memory pointer, callback, or scheduler continuation escapes.
    ActorMemoryHeapHandle heap_alloc(std::size_t bytes) {
      return host_.with_memory(*this, [&](auto& mem, const auto& id) {
        return mem.alloc(id, bytes);
      });
    }
    void heap_write(const ActorMemoryHeapHandle& h, std::size_t offset,
                    std::span<const std::uint8_t> bytes) {
      host_.with_memory(*this, [&](auto& mem, const auto& id) {
        mem.heap_write(id, h, offset, bytes);
      });
    }
    void heap_read(const ActorMemoryHeapHandle& h, std::size_t offset,
                   std::span<std::uint8_t> out) {
      host_.with_memory(*this, [&](auto& mem, const auto& id) {
        mem.heap_read(id, h, offset, out);
      });
    }
    void heap_drop(const ActorMemoryHeapHandle& h) {
      host_.with_memory(*this, [&](auto& mem, const auto& id) {
        mem.drop(id, h);
      });
    }
    ActorMemoryExecution begin_frames() {
      return host_.with_memory(*this, [](auto& mem, const auto& id) {
        return mem.begin(id);
      });
    }
    ActorMemoryFrameHandle frame_push(const ActorMemoryExecution& e,
                                      std::size_t bytes) {
      return host_.with_memory(*this, [&](auto& mem, const auto& id) {
        return mem.push(id, e, bytes);
      });
    }
    void frame_write(const ActorMemoryExecution& e,
                     const ActorMemoryFrameHandle& h, std::size_t offset,
                     std::span<const std::uint8_t> bytes) {
      host_.with_memory(*this, [&](auto& mem, const auto& id) {
        mem.frame_write(id, e, h, offset, bytes);
      });
    }
    void frame_read(const ActorMemoryExecution& e,
                    const ActorMemoryFrameHandle& h, std::size_t offset,
                    std::span<std::uint8_t> out) {
      host_.with_memory(*this, [&](auto& mem, const auto& id) {
        mem.frame_read(id, e, h, offset, out);
      });
    }
    void frame_pop(const ActorMemoryExecution& e,
                   const ActorMemoryFrameHandle& h) {
      host_.with_memory(*this, [&](auto& mem, const auto& id) {
        mem.pop(id, e, h);
      });
    }
    void finish_frames(const ActorMemoryExecution& e) {
      host_.with_memory(*this, [&](auto& mem, const auto& id) {
        mem.finish(id, e);
      });
    }
    // Deliberately NO suspend/resume method: synchronous dispatch cannot
    // park an actor until CONTROL/scheduler continuation ownership exists.
  };

  explicit ActorIncarnationOwnerV1(
      std::uint64_t actor_id,
      ActorMemoryAggregateBudgetV1& budget = ActorMemoryAggregateBudgetV1::process(),
      std::size_t heap_bytes = 1024 * 1024,
      std::size_t stack_bytes = 64 * 1024,
      std::size_t heap_slots = 256,
      std::size_t max_frames = 64)
      : mailbox_(actor_id), current_(mailbox_.ref()), budget_(budget),
        heap_bytes_(heap_bytes), stack_bytes_(stack_bytes),
        heap_slots_(heap_slots), max_frames_(max_frames) {
    make_incarnation_locked(current_);
  }

  ActorIncarnationOwnerV1(const ActorIncarnationOwnerV1&) = delete;
  ActorIncarnationOwnerV1& operator=(const ActorIncarnationOwnerV1&) = delete;
  ~ActorIncarnationOwnerV1() noexcept {
    // Destruction with an outstanding callback is a host lifetime violation.
    // The supervisor must first join the dispatching carrier.
    std::scoped_lock lock(gate_);
    if (in_turn_) std::terminate();
    mailbox_.close();
    stopping_ = true;
    retire_locked();
  }

  ActorRef ref() const {
    std::scoped_lock lock(gate_);
    if (stopping_ || !memory_)
      throw std::logic_error("stopped actor has no live reference");
    return current_;
  }

  void send(const ActorRef& target, std::span<const std::uint8_t> message) {
    std::scoped_lock lock(gate_);
    if (stopping_ || !memory_ || !mailbox_.authorizes(target))
      throw std::logic_error("stale or stopped actor destination");
    mailbox_.send(target, message);
  }

  std::size_t queued() const {
    std::scoped_lock lock(gate_);
    return mailbox_.queued();
  }

  // Execute at most one queued mailbox message under a native physical-turn
  // lease. The handler is trusted host code; it must return and must not
  // capture a Turn pointer beyond dispatch. STOP is allowed while it runs:
  // CONTROL revokes mailbox authority immediately and defers reclamation
  // until the carrier finishes this exact turn.
  template <typename Handler>
  bool dispatch_one(const ActorRef& actor, Handler&& handler) {
    ActorArena::Turn physical{};
    std::optional<std::vector<std::uint8_t>> message;
    {
      std::scoped_lock lock(gate_);
      if (stopping_ || !memory_ || !arena_ || in_turn_ ||
          !mailbox_.authorizes(actor))
        throw std::logic_error("stale or non-reentrant actor dispatch denied");
      message = mailbox_.receive();
      if (!message) return false;
      try {
        physical = arena_->begin_turn(actor.id, actor.generation);
      } catch (...) {
        // This message was already dequeued. Failed physical-turn
        // admission is terminal, not a recoverable message drop.
        wipe(*message);
        mailbox_.close();
        stopping_ = true;
        try {
          retire_locked();
        } catch (...) {
          // Do not release a possibly active arena after admission failed.
          quarantined_ = true;
        }
        throw;
      }
      in_turn_ = true;
      carrier_ = std::this_thread::get_id();
      physical_ = physical;
    }

    std::exception_ptr failure;
    {
      Turn context(*this, actor, physical);
      try {
        std::invoke(std::forward<Handler>(handler), context,
                    std::span<const std::uint8_t>(*message));
        // Caught/ignored frame quota, invalid stack discipline, or an
        // unimplemented suspended handler must not look like success.
        std::scoped_lock lock(gate_);
        if (!stopping_) memory_->verify_handler_exit(actor);
      } catch (...) {
        failure = std::current_exception();
      }
    }

    {
      std::scoped_lock lock(gate_);
      try {
        arena_->end_turn(physical);
      } catch (...) {
        if (!failure) failure = std::current_exception();
      }
      in_turn_ = false;
      carrier_ = {};
      physical_ = {};
      if (failure) {
        mailbox_.close();
        stopping_ = true;
      }
      if (stopping_) retire_locked();
    }
    wipe(*message);
    if (failure) std::rethrow_exception(failure);
    return true;
  }

  // Close message admission first; an active handler may finish unwinding
  // but can no longer access native actor memory through its scoped Turn.
  void stop() {
    std::scoped_lock lock(gate_);
    mailbox_.close();
    stopping_ = true;
    if (!in_turn_) retire_locked();
  }

  // Only CONTROL may request reincarnation after carrier quiescence.
  // Constructor failure leaves the successor CLOSED, not partially live.
  ActorRef restart() {
    std::scoped_lock lock(gate_);
    if (in_turn_) throw std::logic_error("active actor blocks supervisor restart");
    mailbox_.close();
    stopping_ = true;
    retire_locked();
    auto successor = mailbox_.restart();
    try {
      make_incarnation_locked(successor);
      current_ = successor;
      stopping_ = false;
      return current_;
    } catch (...) {
      mailbox_.close();
      throw;
    }
  }

  bool stopped() const {
    std::scoped_lock lock(gate_);
    return stopping_;
  }

 private:
  // Mailbox must be constructed first and destroyed after the actor memory.
  // scoped budget must outlive this host owner.
  mutable std::mutex gate_;
  Mailbox mailbox_;
  ActorRef current_;
  ActorMemoryAggregateBudgetV1& budget_;
  const std::size_t heap_bytes_, stack_bytes_, heap_slots_, max_frames_;
  std::unique_ptr<ActorArena> arena_;
  std::unique_ptr<ActorIncarnationMemoryV1> memory_;
  ActorArena::Turn physical_{};
  std::thread::id carrier_{};
  bool in_turn_ = false;
  bool stopping_ = false;

  template <typename Operation>
  decltype(auto) with_memory(const Turn& turn, Operation&& op) {
    std::scoped_lock lock(gate_);
    if (!in_turn_ || stopping_ || !memory_ ||
        carrier_ != std::this_thread::get_id() ||
        turn.physical_.domain != physical_.domain ||
        turn.physical_.epoch != physical_.epoch ||
        turn.actor_.id != current_.id ||
        turn.actor_.generation != current_.generation ||
        !mailbox_.authorizes(turn.actor_))
      throw std::logic_error("stale, off-carrier or stopped physical actor turn");
    return std::forward<Operation>(op)(*memory_, turn.actor_);
  }

  void make_incarnation_locked(const ActorRef& actor) {
    // Construct both allocations locally; failed admission exposes neither.
    auto fresh_arena = std::make_unique<ActorArena>(actor.id, actor.generation);
    auto fresh_memory = std::make_unique<ActorIncarnationMemoryV1>(
        mailbox_, actor, heap_bytes_, stack_bytes_, heap_slots_, max_frames_,
        budget_);
    arena_ = std::move(fresh_arena);
    memory_ = std::move(fresh_memory);
  }

  void retire_locked() {
    if (in_turn_ || quarantined_) std::terminate();
    // Prove that no physical carrier is active BEFORE scrubbing and
    // refunding the native heap/frame quota. An arena retirement failure
    // must retain the native memory and fail closed.
    if (arena_) arena_->retire();
    if (memory_) memory_->retire();
    memory_.reset();
    arena_.reset();
  }

  static void wipe(std::vector<std::uint8_t>& payload) noexcept {
    volatile std::uint8_t* p = payload.data();
    for (std::size_t i = 0; i < payload.size(); ++i) p[i] = 0;
    payload.clear();
  }
};

} // namespace oresforge::runtime
