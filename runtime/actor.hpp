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
struct ActorRef { std::uint64_t id; std::uint64_t generation; };
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
  std::size_t queued_bytes_=0;
  bool closed_=false;
  std::deque<std::vector<std::uint8_t>> queue_;
};
}
