#include "runtime/actor.hpp"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>

using oresforge::runtime::Mailbox;
using oresforge::runtime::kMaxMailboxBytes;
using oresforge::runtime::kMaxMailboxMessages;

namespace {
template <typename F>
void must_reject(F action) {
  try { action(); }
  catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("unsafe mailbox capacity was accepted");
}
} // namespace

int main() {
  try {
    must_reject([] { Mailbox box(0, 1, 1); });
    must_reject([] { Mailbox box(1, 0, 1); });
    must_reject([] { Mailbox box(1, 1, 0); });
    must_reject([] { Mailbox box(1, kMaxMailboxMessages + 1, 1024); });
    must_reject([] { Mailbox box(1, 1, kMaxMailboxBytes + 1); });
    must_reject([] { Mailbox box(1, std::numeric_limits<std::size_t>::max(), 1024); });
    must_reject([] { Mailbox box(1, 1, std::numeric_limits<std::size_t>::max()); });
    // Legitimate boundary and historical default remain available.
    Mailbox boundary(1, kMaxMailboxMessages, kMaxMailboxBytes);
    Mailbox standard(2);
    auto ref = boundary.ref();
    const std::uint8_t payload[] = {1};
    boundary.send(ref, payload);
    if (boundary.queued() != 1)
      throw std::runtime_error("bounded mailbox incorrectly rejected normal send");
    std::cout << "forge actor mailbox bounds: pass\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "forge actor mailbox bounds: " << error.what() << '\n';
    return 1;
  }
}
