#include "runtime/durable_command_wire_v1.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using namespace oresforge::runtime::persistence;
static std::vector<std::uint8_t> b(const std::string& text) {
  return {text.begin(), text.end()};
}
static std::string hex(const std::vector<std::uint8_t>& data) {
  const char* alphabet = "0123456789abcdef";
  std::string out;
  for (auto byte : data) {out += alphabet[byte >> 4]; out += alphabet[byte & 15];}
  return out;
}
template<typename F> static void denied(F operation) {
  bool rejected = false;
  try { operation(); }
  catch (const std::invalid_argument&) {rejected = true;}
  assert(rejected && "malformed durable command was accepted");
}
int main() {
  const std::vector<DomainEventV1> events = {
    {1,b("MoneyDeposited(60)")},{1,b("MoneyDeposited(40)")}};
  const auto wire = encode_event_batch_v1(events);
  assert(hex(wire) ==
    "0000000200000001000000124d6f6e65794465706f736974656428363029"
    "00000001000000124d6f6e65794465706f736974656428343029");
  assert(decode_event_batch_v1(wire) == events);
  auto mutated = events;
  auto immutable = snapshot_command_envelope_v1(b("Deposit(100)"),b("Ok(100)"),events);
  mutated[0].payload[0] = 0;
  assert(decode_event_batch_v1(immutable.event_batch) == events);
  assert(immutable.request == b("Deposit(100)"));
  for (std::size_t size : {0u,4u,11u}) {
    denied([&] { (void)decode_event_batch_v1(
      std::span<const std::uint8_t>(wire.data(),size)); });
  }
  denied([&] { (void)decode_event_batch_v1(
    std::span<const std::uint8_t>(wire.data(),wire.size()-1)); });
  auto trailing = wire; trailing.push_back(0);
  denied([&] { (void)decode_event_batch_v1(trailing); });
  auto invalid_count = wire;invalid_count[0] = invalid_count[1] =
    invalid_count[2] = invalid_count[3] = 0;
  denied([&] { (void)decode_event_batch_v1(invalid_count); });
  auto invalid_schema = wire;
  invalid_schema[4] = invalid_schema[5] =
    invalid_schema[6] = invalid_schema[7] = 0;
  denied([&] { (void)decode_event_batch_v1(invalid_schema); });
  denied([&] { (void)encode_event_batch_v1({}); });
  std::vector<DomainEventV1> oversized_count(257,{1,{}});
  denied([&] { (void)encode_event_batch_v1(oversized_count); });
  const std::vector<DomainEventV1> too_large_event = {{1,std::vector<std::uint8_t>(65537)}};
  denied([&] { (void)encode_event_batch_v1(too_large_event); });
  const std::vector<DomainEventV1> too_many_bytes(
    17,{1,std::vector<std::uint8_t>(65536)});
  denied([&] { (void)encode_event_batch_v1(too_many_bytes); });
  const std::vector<std::uint8_t> large(65537);
  denied([&] { (void)snapshot_command_envelope_v1(large,b("ok"),events); });
  denied([&] { (void)snapshot_command_envelope_v1(b("cmd"),large,events); });
  std::cout << "FORGE_HOST_DURABLE_COMMAND_WIRE_V1_PASS|guest=0|durable=0\n";
}
