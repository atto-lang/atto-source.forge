#include "runtime/actor.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
constexpr std::size_t kBytes = 4093;
void* tracked = nullptr;
bool armed = false, released = false, clean = false, expect_wipe = true;
std::uint8_t fill = 0xa5;
void check_free(void* p) noexcept {
  if (p == tracked) {
    released = true;
    clean = true;
    const auto* bytes = static_cast<const std::uint8_t*>(p);
    for (std::size_t i = 0; i < kBytes; ++i) {
      if (bytes[i] != (expect_wipe ? 0 : fill)) clean = false;
    }
    tracked = nullptr;
  }
  std::free(p);
}
void arm(bool should_wipe) {
  if (tracked) throw std::runtime_error("previous tracked buffer still live");
  released = false;
  clean = false;
  expect_wipe = should_wipe;
  armed = true;
}
void check(const char* label) {
  if (armed || tracked || !released || !clean)
    throw std::runtime_error(label);
}
}
void* operator new(std::size_t size) {
  void* p = std::malloc(size ? size : 1);
  if (!p) throw std::bad_alloc{};
  if (armed && size == kBytes) { tracked = p; armed = false; }
  return p;
}
void operator delete(void* p) noexcept { check_free(p); }
void operator delete(void* p, std::size_t) noexcept { check_free(p); }
void* operator new[](std::size_t size) {
  void* p = std::malloc(size ? size : 1);
  if (!p) throw std::bad_alloc{};
  return p;
}
void operator delete[](void* p) noexcept { check_free(p); }
void operator delete[](void* p, std::size_t) noexcept { check_free(p); }

int main() {
  try {
    std::array<std::uint8_t, kBytes> sender{};
    sender.fill(fill);
    using oresforge::runtime::Mailbox;
    {
      Mailbox box(99001);
      arm(true);
      box.send(box.ref(), sender);
      if (armed) throw std::runtime_error("destructor: payload allocation not observed");
    }
    check("destructor did not wipe undelivered payload");
    {
      Mailbox box(99002);
      arm(true);
      box.send(box.ref(), sender);
      box.close();
      check("close did not wipe undelivered payload");
    }
    {
      Mailbox box(99003);
      arm(true);
      box.send(box.ref(), sender);
      auto fresh = box.restart();
      check("restart did not wipe undelivered payload");
      box.send(fresh, std::span<const std::uint8_t>{});
      if (!box.receive()) throw std::runtime_error("restart lost fresh DATA");
    }
    {
      Mailbox box(99004);
      arm(false);
      box.send(box.ref(), sender);
      auto delivered = box.receive();
      if (!delivered || delivered->size() != kBytes)
        throw std::runtime_error("delivery lost owned payload");
      box.close();
      if ((*delivered)[0] != fill) throw std::runtime_error("close erased delivered payload");
      delivered.reset();
      check("mailbox erased recipient-owned payload");
    }
    for (auto x : sender)
      if (x != fill) throw std::runtime_error("mailbox mutated sender-owned bytes");
    std::cout << "PASS destructor, close, restart wipe queued data; preserve sender and recipient buffers\n";
  } catch (const std::exception& e) {
    std::cerr << "mailbox lifecycle wipe FAILED: " << e.what() << '\n';
    return 1;
  }
}
