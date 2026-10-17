#pragma once
// Host-only fixed-capacity actor-owned byte arena, not guest allocation lowering.
// Every actor, including a SHARED actor, owns its own persistent and scratch space.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
namespace oresforge::runtime {
namespace detail {
// Domain IDs are actor-capability provenance, never a wrapping ticket queue.
// A saturated domain counter permanently denies new arenas. This helper
// accepts an independent counter so the production algorithm can be tested
// near UINT64_MAX without mutating the actor runtime's global counter.
inline std::uint64_t claim_actor_domain(
    std::atomic<std::uint64_t>& next,
    std::uint64_t actor,
    std::uint64_t incarnation) {
  // Validate owner authority before consuming a non-recyclable domain ID.
  if (!actor || !incarnation)
    throw std::invalid_argument("invalid actor arena identity");
  auto candidate = next.load(std::memory_order_relaxed);
  constexpr auto limit = std::numeric_limits<std::uint64_t>::max();
  while (candidate != 0 && candidate < limit) {
    if (next.compare_exchange_weak(candidate, candidate + 1,
                                   std::memory_order_relaxed,
                                   std::memory_order_relaxed))
      return candidate;
  }
  throw std::overflow_error("actor arena domain exhausted");
}
}  // namespace detail
class ActorArena final {
 public:
  // Capabilities are factory-issued only; public aggregate fields would let
  // callers guess another live slot, generation, or scratch allocation.
  // Copying an issued handle is allowed, but construction and field mutation
  // are restricted to the owning arena (as in .NET and WASM host prototypes).
  class Turn {
    friend class ActorArena;
    std::uint64_t domain_, epoch_;
    Turn(std::uint64_t domain, std::uint64_t epoch)
        : domain_(domain), epoch_(epoch) {}
   public:
    Turn(const Turn&) = default;
    Turn& operator=(const Turn&) = default;
  };
  class Handle {
    friend class ActorArena;
    std::uint64_t domain_, generation_;
    std::size_t slot_;
    Handle(std::uint64_t domain, std::uint64_t generation, std::size_t slot)
        : domain_(domain), generation_(generation), slot_(slot) {}
   public:
    Handle(const Handle&) = default;
    Handle& operator=(const Handle&) = default;
  };
  class Scratch {
    friend class ActorArena;
    std::uint64_t domain_, epoch_;
    std::size_t offset_, length_;
    Scratch(std::uint64_t domain, std::uint64_t epoch,
            std::size_t offset, std::size_t length)
        : domain_(domain), epoch_(epoch), offset_(offset), length_(length) {}
   public:
    Scratch(const Scratch&) = default;
    Scratch& operator=(const Scratch&) = default;
  };
  static constexpr std::size_t Slots = 64;
  static constexpr std::size_t SlotBytes = 256;
  static constexpr std::size_t ScratchBytes = 16 * 1024;
  explicit ActorArena(std::uint64_t actor, std::uint64_t incarnation)
      : actor_(actor), incarnation_(incarnation),
        domain_(detail::claim_actor_domain(next_domain_, actor, incarnation)) {}
  ActorArena(const ActorArena&) = delete;
  ActorArena& operator=(const ActorArena&) = delete;
  ~ActorArena() noexcept {
    // A live carrier must be quiesced by CONTROL before destruction. Never
    // scrub concurrently with an admitted turn or park awaiting its mutex.
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || active_ || !retired_) std::terminate();
    scrub();
  }

  Turn begin_turn(std::uint64_t actor, std::uint64_t incarnation) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    if (carrier_arena_ != nullptr || retired_ || active_ ||
        actor != actor_ || incarnation != incarnation_ ||
        epoch_ == std::numeric_limits<std::uint64_t>::max())
      throw std::logic_error("actor arena admission denied");
    active_ = true;
    ++epoch_;
    carrier_arena_ = this;
    carrier_epoch_ = epoch_;
    return {domain_, epoch_};
  }
  void end_turn(Turn turn) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    require_turn(turn);
    zero_bytes(scratch_.data(), scratch_.size());
    scratch_lengths_.fill(0);
    scratch_top_ = 0;
    active_ = false;
    carrier_epoch_ = 0;
    carrier_arena_ = nullptr;
  }
  Handle allocate(Turn turn, std::size_t length) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    require_turn(turn);
    if (length == 0 || length > SlotBytes) throw std::out_of_range("slot length");
    for (std::size_t i = 0; i < Slots; ++i) {
      auto& s = slots_[i];
      if (s.live || !s.generation) continue;
      s.live = true;
      s.length = length;
      zero_bytes(s.data.data(), s.data.size());
      return {domain_, s.generation, i};
    }
    throw std::out_of_range("actor arena quota");
  }
  void write(Turn turn, Handle handle, std::span<const std::byte> bytes) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    auto& s = slot(turn, handle);
    if (bytes.size() != s.length) throw std::out_of_range("write length");
    std::memcpy(s.data.data(), bytes.data(), bytes.size());
  }
  void read(Turn turn, Handle handle, std::span<std::byte> out) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    auto& s = slot(turn, handle);
    if (out.size() != s.length) throw std::out_of_range("read length");
    std::memcpy(out.data(), s.data.data(), out.size());
  }
  void drop(Turn turn, Handle handle) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    auto& s = slot(turn, handle);
    zero_bytes(s.data.data(), s.data.size());
    s.live = false;
    s.length = 0;
    s.generation = s.generation == std::numeric_limits<std::uint64_t>::max()
        ? 0 : s.generation + 1;
  }
  Scratch scratch_alloc(Turn turn, std::size_t length) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    require_turn(turn);
    if (!length || length > ScratchBytes - scratch_top_)
      throw std::out_of_range("scratch quota");
    const auto offset = scratch_top_;
    scratch_lengths_[offset] = static_cast<std::uint16_t>(length);
    scratch_top_ += length;
    return {domain_, epoch_, offset, length};
  }
  void scratch_write(Turn turn, Scratch h, std::span<const std::byte> bytes) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    require_scratch(turn, h, bytes.size());
    std::memcpy(scratch_.data() + h.offset_, bytes.data(), bytes.size());
  }
  void scratch_read(Turn turn, Scratch h, std::span<std::byte> out) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    require_scratch(turn, h, out.size());
    std::memcpy(out.data(), scratch_.data() + h.offset_, out.size());
  }
  void retire() {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    if (retired_) return;
    if (active_) throw std::logic_error("active actor turn blocks retirement");
    retired_ = true;
    scrub();
  }
 private:
  struct Slot {
    std::array<std::byte, SlotBytes> data{};
    std::uint64_t generation = 1;
    std::size_t length = 0;
    bool live = false;
  };
  inline static std::atomic<std::uint64_t> next_domain_{1};
  inline static thread_local ActorArena* carrier_arena_ = nullptr;
  inline static thread_local std::uint64_t carrier_epoch_ = 0;
  std::uint64_t actor_, incarnation_, domain_, epoch_ = 0;
  std::array<Slot, Slots> slots_{};
  std::array<std::byte, ScratchBytes> scratch_{};
  // Each real scratch allocation records its exact starting offset and length.
  // Bounds alone would authorize a forged subrange of another allocation.
  // Fixed-size metadata avoids dynamic allocation during an admitted turn.
  std::array<std::uint16_t, ScratchBytes> scratch_lengths_{};
  std::size_t scratch_top_ = 0;
  std::mutex mutex_;
  bool active_ = false, retired_ = false;
  void require_turn(Turn t) const {
    if (!active_ || retired_ || carrier_arena_ != this ||
        carrier_epoch_ != t.epoch_ || t.domain_ != domain_ || t.epoch_ != epoch_)
      throw std::logic_error("wrong or stale actor turn");
  }
  Slot& slot(Turn t, Handle h) {
    require_turn(t);
    if (h.domain_ != domain_ || h.slot_ >= Slots) throw std::logic_error("foreign handle");
    auto& s = slots_[h.slot_];
    if (!s.live || s.generation != h.generation_) throw std::logic_error("stale handle");
    return s;
  }
  void require_scratch(Turn t, Scratch h, std::size_t bytes) const {
    require_turn(t);
    if (h.domain_ != domain_ || h.epoch_ != epoch_ || h.offset_ >= ScratchBytes ||
        h.offset_ >= scratch_top_ || h.length_ != bytes ||
        h.length_ > scratch_top_ - h.offset_ ||
        scratch_lengths_[h.offset_] != h.length_)
      throw std::logic_error("stale or oversized scratch");
  }
  // Volatile stores make security zeroization observable to the compiler even
  // during final destruction and prevent deletion of dead-store clearing.
  static void zero_bytes(std::byte* p, std::size_t length) noexcept {
    volatile std::byte* bytes = p;
    for (std::size_t i = 0; i < length; ++i) bytes[i] = std::byte{0};
  }
  void scrub() noexcept {
    for (auto& s : slots_) {
      zero_bytes(s.data.data(), s.data.size());
      s.live = false;
      s.length = 0;
    }
    zero_bytes(scratch_.data(), scratch_.size());
    scratch_lengths_.fill(0);
    scratch_top_ = 0;
  }
};
} // namespace oresforge::runtime
