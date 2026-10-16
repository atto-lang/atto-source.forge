// Stateful actor-mailbox differential fuzzing: private authority, owned data,
// bounded credits, restart fencing and FIFO behavior. No test-framework deps.
#include "runtime/actor.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <span>
#include <stdexcept>
#include <vector>

using oresforge::runtime::ActorRef;
using oresforge::runtime::Mailbox;

namespace {
[[noreturn]] void bug() { std::abort(); }
void check(bool condition) { if (!condition) bug(); }

enum class Outcome { admitted, capacity, invalid_ref };

template <class Payload>
Outcome attempt(Mailbox& mailbox, const ActorRef& ref, const Payload& data) {
  try {
    mailbox.send(ref, data);
    return Outcome::admitted;
  } catch (const std::length_error&) {
    return Outcome::capacity;
  } catch (const std::runtime_error&) {
    return Outcome::invalid_ref;
  }
}
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* input,
                                      std::size_t length) {
  if (length < 2 || length > 4096) return 0;
  const std::size_t count_limit = 1 + (input[0] % 24);
  const std::size_t byte_limit = 1 + (input[1] % 192);
  Mailbox mailbox(42, count_limit, byte_limit);
  Mailbox other(42, count_limit, byte_limit);
  ActorRef current = mailbox.ref();
  ActorRef revoked = current;
  bool has_revoked = false;
  bool closed = false;
  std::size_t queued_bytes = 0;
  std::deque<std::vector<std::uint8_t>> expected;

  for (std::size_t i = 2; i < length;) {
    const std::uint8_t op = input[i++] % 8;
    if (op <= 2) {  // Valid sender with a possibly-empty, owned payload.
      const std::size_t size = i < length ? input[i++] % 193 : 0;
      std::vector<std::uint8_t> payload;
      payload.reserve(size);
      for (std::size_t j = 0; j < size; ++j)
        payload.push_back(input[(i + j) % length]);
      i += (length - i < size ? length - i : size);
      const Outcome expected_outcome = closed ? Outcome::invalid_ref :
          (expected.size() >= count_limit || size > byte_limit - queued_bytes
               ? Outcome::capacity : Outcome::admitted);
      const Outcome observed = attempt(mailbox, current,
          std::span<const std::uint8_t>(payload.data(), payload.size()));
      check(observed == expected_outcome);
      if (observed == Outcome::admitted) {
        expected.push_back(payload);
        queued_bytes += size;
        if (!payload.empty()) payload[0] ^= 0xff; // sender must not alias queue
      }
    } else if (op == 3) {  // Receive may return zero-length DATA.
      auto actual = mailbox.receive();
      check(actual.has_value() == !expected.empty());
      if (actual) {
        check(*actual == expected.front());
        queued_bytes -= expected.front().size();
        expected.pop_front();
      }
    } else if (op == 4) {  // Close discards pending owned data.
      mailbox.close();
      expected.clear();
      queued_bytes = 0;
      closed = true;
    } else if (op == 5) {  // Fresh generation fences all previous refs.
      revoked = current;
      has_revoked = true;
      current = mailbox.restart();
      expected.clear();
      queued_bytes = 0;
      closed = false;
    } else {
      const ActorRef attacker = op == 6
          ? ActorRef(current.id, current.generation)
          : (has_revoked ? revoked : other.ref());
      check(attempt(mailbox, attacker, std::vector<std::uint8_t>{}) ==
            Outcome::invalid_ref);
      check(attempt(mailbox, other.ref(), std::vector<std::uint8_t>{}) ==
            Outcome::invalid_ref);
    }
    check(mailbox.queued() == expected.size());
    check(queued_bytes <= byte_limit);
  }
  while (!expected.empty()) {
    auto value = mailbox.receive();
    check(value && *value == expected.front());
    expected.pop_front();
  }
  check(mailbox.queued() == 0);
  return 0;
}
