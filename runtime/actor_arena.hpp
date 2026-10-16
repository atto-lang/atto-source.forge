#pragma once
// Host-only fixed-capacity actor-owned byte arena, not guest allocation lowering.
// Every actor, including a SHARED actor, owns its own persistent and scratch space.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
      : actor_(actor), incarnation_(incarnation),
        domain_(next_domain_.fetch_add(1, std::memory_order_relaxed)) {
    if (!actor || !incarnation || !domain_ ||
        domain_ == std::numeric_limits<std::uint64_t>::max())
      throw std::invalid_argument("invalid actor arena identity");
  }
  ActorArena(const ActorArena&) = delete;
  ActorArena& operator=(const ActorArena&) = delete;
  ~ActorArena() { scrub(); }

  Turn begin_turn(std::uint64_t actor, std::uint64_t incarnation) {
    std::scoped_lock lock(mutex_);
    if (retired_ || active_ || actor != actor_ || incarnation != incarnation_ ||
        epoch_ == std::numeric_limits<std::uint64_t>::max())
      throw std::logic_error("actor arena admission denied");
    active_ = true;
    owner_ = std::this_thread::get_id();
    return {domain_, ++epoch_};
  }
  void end_turn(Turn turn) {
    std::scoped_lock lock(mutex_);
    require_turn(turn);
    scratch_.fill(std::byte{0});
    scratch_top_ = 0;
    active_ = false;
    owner_ = {};
  }
  Handle allocate(Turn turn, std::size_t length) {
    std::scoped_lock lock(mutex_);
    require_turn(turn);
    if (length == 0 || length > SlotBytes) throw std::out_of_range("slot length");
    for (std::size_t i = 0; i < Slots; ++i) {
      auto& s = slots_[i];
      if (s.live || !s.generation) continue;
      s.live = true;
      s.length = length;
      s.data.fill(std::byte{0});
      return {domain_, s.generation, i};
    }
    throw std::out_of_range("actor arena quota");
  }
  void write(Turn turn, Handle handle, std::span<const std::byte> bytes) {
    std::scoped_lock lock(mutex_);
    auto& s = slot(turn, handle);
    if (bytes.size() != s.length) throw std::out_of_range("write length");
    std::memcpy(s.data.data(), bytes.data(), bytes.size());
  }
  void read(Turn turn, Handle handle, std::span<std::byte> out) {
    std::scoped_lock lock(mutex_);
    auto& s = slot(turn, handle);
    if (out.size() != s.length) throw std::out_of_range("read length");
    std::memcpy(out.data(), s.data.data(), out.size());
  }
  void drop(Turn turn, Handle handle) {
    std::scoped_lock lock(mutex_);
    auto& s = slot(turn, handle);
    s.data.fill(std::byte{0});
    s.live = false;
    s.length = 0;
    s.generation = s.generation == std::numeric_limits<std::uint64_t>::max()
        ? 0 : s.generation + 1;
  }
  Scratch scratch_alloc(Turn turn, std::size_t length) {
    std::scoped_lock lock(mutex_);
    require_turn(turn);
    if (!length || length > ScratchBytes - scratch_top_)
      throw std::out_of_range("scratch quota");
    const auto offset = scratch_top_;
    scratch_top_ += length;
    return {domain_, epoch_, offset, length};
  }
  void scratch_write(Turn turn, Scratch h, std::span<const std::byte> bytes) {
    std::scoped_lock lock(mutex_);
    require_scratch(turn, h, bytes.size());
    std::memcpy(scratch_.data() + h.offset, bytes.data(), bytes.size());
  }
  void scratch_read(Turn turn, Scratch h, std::span<std::byte> out) {
    std::scoped_lock lock(mutex_);
    require_scratch(turn, h, out.size());
    std::memcpy(out.data(), scratch_.data() + h.offset, out.size());
  }
  void retire() {
    std::scoped_lock lock(mutex_);
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
  std::uint64_t actor_, incarnation_, domain_, epoch_ = 0;
  std::array<Slot, Slots> slots_{};
  std::array<std::byte, ScratchBytes> scratch_{};
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
    if (h.domain != domain_ || h.epoch != epoch_ || h.offset > scratch_top_ ||
        h.length != bytes || h.length > scratch_top_ - h.offset)
      throw std::logic_error("stale or oversized scratch");
  }
  void scrub() noexcept {
    for (auto& s : slots_) {
      s.data.fill(std::byte{0});
      s.live = false;
      s.length = 0;
    }
    scratch_.fill(std::byte{0});
    scratch_top_ = 0;
  }
};
} // namespace oresforge::runtime
