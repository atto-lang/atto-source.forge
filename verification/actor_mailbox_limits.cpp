#include "runtime/actor.hpp"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

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
    std::vector<std::uint8_t> meg(kMaxMailboxBytes / 16, 0xb7);
    if (meg.size() != oresforge::runtime::kMaxMessageBytes)
      throw std::runtime_error("regression expects sixteen 1 MiB messages");
    for (int i = 0; i < 16; ++i) boundary.send(ref, meg);
    bool full_rejected = false;
    try { boundary.send(ref, meg); }
    catch (const std::length_error&) { full_rejected = true; }
    if (!full_rejected || boundary.queued() != 16)
      throw std::runtime_error("16 MiB mailbox byte cap not enforced");
    auto delivered = boundary.receive();
    if (!delivered || delivered->size() != meg.size() || delivered->at(0) != 0xb7)
      throw std::runtime_error("owned message corrupt or lost");
    boundary.send(ref, meg);  // reclaimed exactly 1 MiB credit
    boundary.close();
    if (boundary.queued() != 0 || meg[0] != 0xb7)
      throw std::runtime_error("mailbox close retained entries or modified sender-owned bytes");
    const std::uint8_t payload[] = {1};
    standard.send(standard.ref(), payload);
    if (standard.queued() != 1)
      throw std::runtime_error("default mailbox incorrectly rejected normal send");
    // A zero-length DATA message uses a real bounded slot; a default empty
    // span must not be passed as a possibly-null iterator range.
    Mailbox empty(3, 1, 1);
    const auto empty_ref = empty.ref();
    empty.send(empty_ref, std::span<const std::uint8_t>{});
    if (empty.queued() != 1)
      throw std::runtime_error("empty DATA message was dropped");
    bool full_empty = false;
    try { empty.send(empty_ref, std::span<const std::uint8_t>{}); }
    catch (const std::length_error&) { full_empty = true; }
    if (!full_empty) throw std::runtime_error("empty DATA bypassed slot budget");
    auto empty_value = empty.receive();
    if (!empty_value || !empty_value->empty() || empty.queued() != 0)
      throw std::runtime_error("empty DATA delivery or credit reclaim failed");
    empty.send(empty_ref, payload);
    if (empty.queued() != 1)
      throw std::runtime_error("empty DATA failed to reclaim capacity");
    std::cout << "forge actor mailbox bounds: pass\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "forge actor mailbox bounds: " << error.what() << '\n';
    return 1;
  }
}
