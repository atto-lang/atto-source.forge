// Authorization must be checked before exposing per-mailbox message limits.
// This test is deliberately independent of the broader draft mailbox suite.
#include "runtime/actor.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

using oresforge::runtime::ActorRef;
using oresforge::runtime::Mailbox;
using oresforge::runtime::kMaxMessageBytes;

static_assert(!std::is_constructible_v<ActorRef, std::uint64_t, std::uint64_t>);

template<class E, class F>
void expect_exception(F&& action, const char* message) {
  bool matched = false;
  try { action(); }
  catch (const E&) { matched = true; }
  if (!matched) throw std::runtime_error(message);
}

int main() {
  try {
    Mailbox box(501, 3, 4);
    Mailbox other(501, 3, 4);
    const ActorRef fresh = box.ref();
    std::vector<std::uint8_t> oversized(kMaxMessageBytes + 1, 0xa5);
    ActorRef wrong_id = fresh;
    ++wrong_id.id;
    ActorRef wrong_generation = fresh;
    ++wrong_generation.generation;
    expect_exception<std::runtime_error>(
        [&] { box.send(other.ref(), oversized); },
        "foreign capability exposed size error");
    expect_exception<std::runtime_error>(
        [&] { box.send(wrong_id, oversized); },
        "tampered id exposed size error");
    expect_exception<std::runtime_error>(
        [&] { box.send(wrong_generation, oversized); },
        "tampered generation exposed size error");
    expect_exception<std::length_error>(
        [&] { box.send(fresh, oversized); },
        "valid capability bypassed message size limit");
    if (box.queued() != 0) throw std::runtime_error("invalid send changed queue");
    box.close();
    expect_exception<std::runtime_error>(
        [&] { box.send(fresh, oversized); },
        "closed mailbox exposed size error");
    const ActorRef renewed = box.restart();
    expect_exception<std::runtime_error>(
        [&] { box.send(fresh, oversized); },
        "revoked capability exposed size error");
    expect_exception<std::length_error>(
        [&] { box.send(renewed, oversized); },
        "renewed authority bypassed size limit");
    box.send(renewed, std::vector<std::uint8_t>{1, 2, 3});
    auto result = box.receive();
    if (!result || *result != std::vector<std::uint8_t>({1, 2, 3}))
      throw std::runtime_error("valid delivery regressed");
    if (oversized.front() != 0xa5 || oversized.back() != 0xa5)
      throw std::runtime_error("sender buffer changed");
    std::cout << "PASS authorization-first mailbox admission\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL authorization-first mailbox: " << error.what() << '\n';
    return 1;
  }
}
