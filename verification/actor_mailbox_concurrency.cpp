#include "runtime/actor.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <thread>
#include <vector>

using oresforge::runtime::ActorRef;
using oresforge::runtime::Mailbox;

namespace {
void require(bool condition, const char* why) {
  if (!condition) throw std::runtime_error(why);
}
template <typename F> void must_reject(F&& callback) {
  bool rejected = false;
  try { callback(); } catch (const std::exception&) { rejected = true; }
  require(rejected, "untrusted or stale mailbox send unexpectedly succeeded");
}
void test_capacity_and_restart() {
  Mailbox box(101, 2, 4);
  const ActorRef first = box.ref();
  const std::array<std::uint8_t, 3> a{1, 2, 3};
  const std::array<std::uint8_t, 2> b{4, 5};
  box.send(first, a);
  must_reject([&] { box.send(first, b); });  // byte limit, not just count
  require(box.queued() == 1, "byte credit exceeded");
  auto received = box.receive();
  require(received && received->size() == 3 && received->at(0) == 1,
          "owned payload was not delivered");
  box.send(first, b);  // reclaimed byte credit
  box.close();
  require(box.queued() == 0, "closed mailbox retained pending bytes");
  must_reject([&] { box.send(first, a); });
  const ActorRef second = box.restart();
  must_reject([&] { box.send(first, a); });
  static_assert(!std::is_constructible_v<ActorRef, std::uint64_t, std::uint64_t>,
                "numeric ActorRef construction must be inaccessible");
  box.send(second, a);
  require(box.queued() == 1, "fresh authority was rejected");
  Mailbox other(101, 2, 4);
  must_reject([&] { other.send(second, a); });  // ID is not authority
}
void test_parallel_producers() {
  constexpr int kProducers = 8;
  constexpr int kPerProducer = 3000;
  constexpr int kTotal = kProducers * kPerProducer;
  Mailbox box(202, 1024, 1024 * 8);
  const ActorRef ref = box.ref();
  std::atomic<bool> failed{false};
  std::atomic<int> sent{0};
  std::atomic<int> delivered{0};
  std::vector<std::uint8_t> seen(kTotal, 0);
  std::thread consumer([&] {
    while (delivered.load() < kTotal && !failed.load()) {
      auto payload = box.receive();
      if (!payload) {
        std::this_thread::yield();
        continue;
      }
      if (payload->size() != sizeof(std::uint64_t)) {
        failed = true;
        break;
      }
      std::uint64_t id = 0;
      for (int byte = 0; byte < 8; ++byte)
        id |= std::uint64_t((*payload)[static_cast<std::size_t>(byte)]) << (8 * byte);
      if (id >= kTotal || seen[id] != 0) {
        failed = true;
        break;
      }
      seen[id] = 1;
      ++delivered;
    }
  });
  std::vector<std::thread> writers;
  for (int producer = 0; producer < kProducers; ++producer) {
    writers.emplace_back([&, producer] {
      for (int index = 0; index < kPerProducer && !failed.load(); ++index) {
        const std::uint64_t id = std::uint64_t(producer) * std::uint64_t(kPerProducer) + std::uint64_t(index);
        std::array<std::uint8_t, 8> bytes{};
        for (int byte = 0; byte < 8; ++byte)
          bytes[static_cast<std::size_t>(byte)] = std::uint8_t(id >> (8 * byte));
        while (!failed.load()) {
          try {
            box.send(ref, bytes);
            ++sent;
            break;
          } catch (const std::length_error&) {
            std::this_thread::yield();  // backpressure: retry same message
          } catch (const std::exception&) {
            failed = true;
          }
        }
      }
    });
  }
  for (auto& writer : writers) writer.join();
  consumer.join();
  require(!failed.load(), "parallel transport rejected/corrupted payload");
  require(sent == kTotal && delivered == kTotal, "lost message under contention");
  require(std::all_of(seen.begin(), seen.end(), [](auto v) { return v == 1; }),
          "duplicate or missing sequence identity");
  require(box.queued() == 0, "pending message leaked");
}
void test_restart_race() {
  Mailbox box(303, 64, 1024);
  const ActorRef stale = box.ref();
  const std::array<std::uint8_t, 1> value{5};
  std::atomic<bool> started{false}, finished{false};
  std::thread writer([&] {
    started = true;
    while (!finished.load()) {
      try { box.send(stale, value); }
      catch (const std::exception&) { /* expected capacity/stale failures */ }
    }
  });
  while (!started.load()) std::this_thread::yield();
  const ActorRef fresh = box.restart();
  finished = true;
  writer.join();
  for (int i = 0; i < 500; ++i)
    must_reject([&] { box.send(stale, value); });
  box.close();
  must_reject([&] { box.send(fresh, value); });
}
} // namespace

int main() {
  try {
    test_capacity_and_restart();
    test_parallel_producers();
    test_restart_race();
    std::cout << "actor mailbox concurrency: 24000 messages and stale ref fencing passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "actor mailbox concurrency FAILED: " << error.what() << '\n';
    return 1;
  }
}
