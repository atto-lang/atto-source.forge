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
class ActorArena final {
 public:
  struct Turn { std::uint64_t domain, epoch; };
  struct Handle { std::uint64_t domain, generation; std::size_t slot; };
  struct Scratch { std::uint64_t domain, epoch; std::size_t offset, length; };
  static constexpr std::size_t Slots = 64;
  static constexpr std::size_t SlotBytes = 256;
  static constexpr std::size_t ScratchBytes = 16 * 1024;
  explicit ActorArena(std::uint64_t actor, std::uint64_t incarnation)
      : actor_(actor), incarnation_(incarnation), domain_(allocate_domain(actor, incarnation)) {}
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
    if (retired_ || active_ || actor != actor_ || incarnation != incarnation_ ||
        epoch_ == std::numeric_limits<std::uint64_t>::max())
      throw std::logic_error("actor arena admission denied");
    active_ = true;
    owner_ = std::this_thread::get_id();
    return {domain_, ++epoch_};
  }
  void end_turn(Turn turn) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    require_turn(turn);
    zero_bytes(scratch_.data(), scratch_.size());
    scratch_lengths_.fill(0);
    scratch_top_ = 0;
    active_ = false;
    owner_ = {};
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
    std::memcpy(scratch_.data() + h.offset, bytes.data(), bytes.size());
  }
  void scratch_read(Turn turn, Scratch h, std::span<std::byte> out) {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) throw std::logic_error("actor arena busy");
    require_scratch(turn, h, out.size());
    std::memcpy(out.data(), scratch_.data() + h.offset, out.size());
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
  // Domain identity is a non-recyclable authority boundary. Fail closed at
  // exhaustion, rather than letting fetch_add wrap into stale token values.
  static std::uint64_t allocate_domain(std::uint64_t actor, std::uint64_t incarnation) {
    // Invalid requests cannot burn identities in the non-recyclable domain.
    if (!actor || !incarnation)
      throw std::invalid_argument("invalid actor arena identity");
    auto current = next_domain_.load(std::memory_order_relaxed);
    for (;;) {
      if (!current || current == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("actor arena domain identity exhausted");
      if (next_domain_.compare_exchange_weak(
              current, current + 1, std::memory_order_relaxed,
              std::memory_order_relaxed))
        return current;
    }
  }
  std::uint64_t actor_, incarnation_, domain_, epoch_ = 0;
  std::array<Slot, Slots> slots_{};
  std::array<std::byte, ScratchBytes> scratch_{};
  // Each real scratch allocation records its exact starting offset and length.
  // Bounds alone would authorize a forged subrange of another allocation.
  // Fixed-size metadata avoids dynamic allocation during an admitted turn.
  std::array<std::uint16_t, ScratchBytes> scratch_lengths_{};
  std::size_t scratch_top_ = 0;
  std::mutex mutex_;
  std::thread::id owner_{};
  bool active_ = false, retired_ = false;
  void require_turn(Turn t) const {
    if (!active_ || retired_ || owner_ != std::this_thread::get_id() ||
        t.domain != domain_ || t.epoch != epoch_)
      throw std::logic_error("wrong or stale actor turn");
  }
  Slot& slot(Turn t, Handle h) {
    require_turn(t);
    if (h.domain != domain_ || h.slot >= Slots) throw std::logic_error("foreign handle");
    auto& s = slots_[h.slot];
    if (!s.live || s.generation != h.generation) throw std::logic_error("stale handle");
    return s;
  }
  void require_scratch(Turn t, Scratch h, std::size_t bytes) const {
    require_turn(t);
    if (h.domain != domain_ || h.epoch != epoch_ || h.offset >= ScratchBytes ||
        h.offset >= scratch_top_ || h.length != bytes ||
        h.length > scratch_top_ - h.offset ||
        scratch_lengths_[h.offset] != h.length)
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
