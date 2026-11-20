#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <memory>
#include <span>
#include <vector>
#include <utility>
#include "runtime/TenantIsolationAdmission.hpp"
namespace oresforge::runtime {
inline constexpr std::size_t kMaxMessageBytes=1024*1024;
inline constexpr std::size_t kMaxMailboxMessages=4096;
inline constexpr std::size_t kMaxMailboxBytes=16*kMaxMessageBytes;
struct CapabilityToken; // Opaque, local-only authority; never serialize it.
class Mailbox;
struct ActorRef {
  std::uint64_t id;
  std::uint64_t generation;
 private:
  std::shared_ptr<const CapabilityToken> authority_;
  ActorRef(std::uint64_t id, std::uint64_t generation,
           std::shared_ptr<const CapabilityToken> authority)
      : id(id), generation(generation), authority_(std::move(authority)) {}
  friend class Mailbox;
};
class Mailbox {
 public:
  Mailbox(std::uint64_t id,std::size_t capacity=1024,
          std::size_t byte_capacity=4*kMaxMessageBytes,
          TenantIsolationMode isolation=TenantIsolationMode::TrustedInProcess,
          bool untrusted=false);
  // No concurrent calls may outlive mailbox destruction.
  ~Mailbox() noexcept;
  ActorRef ref() const;
  bool authorizes(const ActorRef& owner) const;
  // Hold mailbox-generation authority through a trusted native byte operation.
  // No guest code/callback may run while this lease owns the mailbox mutex.
  [[nodiscard]] std::unique_lock<std::mutex> lock_authorized(
      const ActorRef& owner, const ActorRef& caller) const;
  // Capacity pressure is normal for asynchronous actor producers: return
  // false without admitting DATA. Invalid/retired authority still throws.
  // This is a host-native admission primitive, not guest Result lowering.
  [[nodiscard]] bool try_send(ActorRef,std::span<const std::uint8_t>);
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
  std::shared_ptr<const CapabilityToken> authority_;
  std::size_t queued_bytes_=0;
  bool closed_=false;
  std::deque<std::vector<std::uint8_t>> queue_;
};
}
