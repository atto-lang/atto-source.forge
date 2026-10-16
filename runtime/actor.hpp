#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <vector>
namespace oresforge::runtime {
inline constexpr std::size_t kMaxMessageBytes=1024*1024;
// This process-local handle is minted only by Mailbox. Numeric actor IDs and
// generations are routing metadata, not transferable authority or a wire codec.
// An untrusted remote sender must pass a separately authenticated boundary.
class ActorRef {
 public:
  ActorRef() = default; // invalid handle, rejected by Mailbox::send
  std::uint64_t id = 0;
  std::uint64_t generation = 0;
 private:
  std::uint64_t mailbox_identity_ = 0;
  ActorRef(std::uint64_t actor, std::uint64_t epoch, std::uint64_t identity)
      : id(actor), generation(epoch), mailbox_identity_(identity) {}
  friend class Mailbox;
};
class Mailbox {
 public:
  Mailbox(std::uint64_t id,std::size_t capacity=1024,
          std::size_t byte_capacity=4*kMaxMessageBytes);
  ActorRef ref() const;
  void send(ActorRef,std::span<const std::uint8_t>);
  std::optional<std::vector<std::uint8_t>> receive();
  void close();
  ActorRef restart();
  std::size_t queued() const;
 private:
  void discardPending() noexcept;
  const std::uint64_t id_;
  const std::size_t capacity_,byte_capacity_;
  mutable std::mutex mutex_;
  std::uint64_t generation_=1;
  const std::uint64_t mailbox_identity_;
  std::size_t queued_bytes_=0;
  bool closed_=false;
  std::deque<std::vector<std::uint8_t>> queue_;
};
}
