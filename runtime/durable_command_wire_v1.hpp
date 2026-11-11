#pragma once
// Trusted host event-batch framing only; NOT durable or compiled .atto guest.
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace oresforge::runtime::persistence {
inline constexpr std::size_t max_events_v1 = 256;
inline constexpr std::size_t max_event_bytes_v1 = 65536;
inline constexpr std::size_t max_batch_bytes_v1 = 1048576;
inline constexpr std::size_t max_command_bytes_v1 = 65536;
inline constexpr std::size_t max_reply_bytes_v1 = 65536;

struct DomainEventV1 {
  std::uint32_t schema_version;
  std::vector<std::uint8_t> payload;
  bool operator==(const DomainEventV1&) const = default;
};
struct CommandEnvelopeV1 {
  std::vector<std::uint8_t> request;
  std::vector<std::uint8_t> reply;
  std::vector<std::uint8_t> event_batch;
};

inline void put_u32_v1(std::vector<std::uint8_t>& out, std::uint32_t n) {
  for (int shift = 24; shift >= 0; shift -= 8)
    out.push_back(static_cast<std::uint8_t>(n >> shift));
}
inline std::uint32_t take_u32_v1(std::span<const std::uint8_t> in,
                                  std::size_t& at) {
  if (at > in.size() || in.size() - at < 4)
    throw std::invalid_argument("DURABLE_COMMAND_WIRE_V1_TRUNCATED");
  std::uint32_t n = 0;
  for (int i = 0; i < 4; ++i) n = (n << 8) | in[at++];
  return n;
}
inline std::vector<std::uint8_t> encode_event_batch_v1(
    std::span<const DomainEventV1> events) {
  if (events.empty() || events.size() > max_events_v1)
    throw std::invalid_argument("DURABLE_COMMAND_WIRE_V1_COUNT");
  std::size_t size = 4;
  for (const auto& event : events) {
    if (!event.schema_version || event.payload.size() > max_event_bytes_v1)
      throw std::invalid_argument("DURABLE_COMMAND_WIRE_V1_SCHEMA_OR_PAYLOAD");
    if (8 + event.payload.size() > max_batch_bytes_v1 - size)
      throw std::invalid_argument("DURABLE_COMMAND_WIRE_V1_BATCH_BUDGET");
    size += 8 + event.payload.size();
  }
  std::vector<std::uint8_t> out;
  out.reserve(size);
  put_u32_v1(out, static_cast<std::uint32_t>(events.size()));
  for (const auto& event : events) {
    put_u32_v1(out, event.schema_version);
    put_u32_v1(out, static_cast<std::uint32_t>(event.payload.size()));
    out.insert(out.end(), event.payload.begin(), event.payload.end());
  }
  return out;
}
inline std::vector<DomainEventV1> decode_event_batch_v1(
    std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 12 || bytes.size() > max_batch_bytes_v1)
    throw std::invalid_argument("DURABLE_COMMAND_WIRE_V1_SIZE");
  std::size_t at = 0;
  const auto count = take_u32_v1(bytes, at);
  if (!count || count > max_events_v1)
    throw std::invalid_argument("DURABLE_COMMAND_WIRE_V1_COUNT");
  std::vector<DomainEventV1> result;
  result.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto schema = take_u32_v1(bytes, at);
    const auto size = take_u32_v1(bytes, at);
    if (!schema || size > max_event_bytes_v1)
      throw std::invalid_argument("DURABLE_COMMAND_WIRE_V1_SCHEMA_OR_PAYLOAD");
    if (at > bytes.size() || size > bytes.size() - at)
      throw std::invalid_argument("DURABLE_COMMAND_WIRE_V1_TRUNCATED");
    DomainEventV1 event{schema, {}};
    event.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                         bytes.begin() + static_cast<std::ptrdiff_t>(at + size));
    at += size;
    result.push_back(std::move(event));
  }
  if (at != bytes.size())
    throw std::invalid_argument("DURABLE_COMMAND_WIRE_V1_TRAILING_BYTES");
  return result;
}
inline CommandEnvelopeV1 snapshot_command_envelope_v1(
    std::span<const std::uint8_t> request,
    std::span<const std::uint8_t> reply,
    std::span<const DomainEventV1> events) {
  if (request.size() > max_command_bytes_v1 || reply.size() > max_reply_bytes_v1)
    throw std::invalid_argument("DURABLE_COMMAND_WIRE_V1_COMMAND_OR_REPLY");
  return {{request.begin(), request.end()}, {reply.begin(), reply.end()},
          encode_event_batch_v1(events)};
}
} // namespace oresforge::runtime::persistence
