#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>
#include <utility>
namespace oresforge::runtime {
inline constexpr std::size_t kMaxMessageBytes=1024*1024;
// Per-mailbox limits; callers cannot disable bounded queue admission.
inline constexpr std::size_t kMaxMailboxMessages=4096;
inline constexpr std::size_t kMaxMailboxBytes=16*kMaxMessageBytes;
// Local authority is never a network-serializable actor address.
struct ActorRef {
  std::uint64_t id;
  std::uint64_t generation;
  ActorRef(std::uint64_t value,std::uint64_t epoch):id(value),generation(epoch) {}
 private:
  friend class Mailbox;
  std::shared_ptr<const std::uint8_t> authority_;
  ActorRef(std::uint64_t value,std::uint64_t epoch,
           std::shared_ptr<const std::uint8_t> authority):
      id(value),generation(epoch),authority_(std::move(authority)) {}
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
  const std::uint64_t id_;
  const std::size_t capacity_,byte_capacity_;
  mutable std::mutex mutex_;
  std::uint64_t generation_=1;
  std::shared_ptr<const std::uint8_t> authority_=std::make_shared<const std::uint8_t>(0);
  void discard_pending();
  std::size_t queued_bytes_=0;
  bool closed_=false;
  std::deque<std::vector<std::uint8_t>> queue_;
};
}
