#include "runtime/ErrorArena.hpp"
#include <cassert>
#include <string_view>
#include <type_traits>
#include <utility>
#include <optional>

using oresforge::runtime::ErrorAllocation;
using oresforge::runtime::ErrorArena;
using oresforge::runtime::ErrorKind;

int main() {
  using Arena = ErrorArena<4, 16>;
  static_assert(!std::is_copy_constructible_v<Arena>);
  static_assert(!std::is_move_constructible_v<Arena>);
  static_assert(noexcept(std::declval<Arena&>().create(ErrorKind::Error, {})));
  Arena arena;
  Arena other;
  const Arena::Handle empty;
  assert(arena.view(empty) == nullptr);
  assert(!arena.same_identity(empty, empty));

  char message[] = "missing user";
  const auto first = arena.create(ErrorKind::Error, message);
  assert(first.status == ErrorAllocation::Created);
  message[0] = 'X';
  const auto second = arena.create(ErrorKind::Error, "missing user");
  assert(second.status == ErrorAllocation::Created);
  assert(arena.view(first.handle)->message() == "missing user");
  assert(arena.view(second.handle)->message() == "missing user");
  assert(!arena.same_identity(first.handle, second.handle));
  const auto forwarded = first.handle;
  assert(arena.same_identity(first.handle, forwarded));
  assert(arena.view(first.handle) == arena.view(forwarded));

  const auto foreign = other.create(ErrorKind::Error, "missing user");
  assert(foreign.status == ErrorAllocation::Created);
  assert(arena.view(foreign.handle) == nullptr);
  assert(other.view(first.handle) == nullptr);
  assert(!arena.same_identity(first.handle, foreign.handle));

  assert(arena.create(ErrorKind::Error, "01234567890123456").status ==
         ErrorAllocation::MessageTooLong);
  assert(arena.create(static_cast<ErrorKind>(255), {}).status ==
         ErrorAllocation::InvalidKind);
  assert(arena.size() == 2);
  const auto overflow = arena.create(ErrorKind::ArithmeticOverflow, "same");
  const auto divzero = arena.create(ErrorKind::DivisionByZero, std::string_view("a\0b", 3));
  assert(overflow.status == ErrorAllocation::Created);
  assert(divzero.status == ErrorAllocation::Created);
  assert(arena.view(overflow.handle)->kind == ErrorKind::ArithmeticOverflow);
  assert(arena.view(divzero.handle)->kind == ErrorKind::DivisionByZero);
  assert(arena.view(divzero.handle)->message() == std::string_view("a\0b", 3));
  assert(arena.size() == 4);
  const auto exhausted = arena.create(ErrorKind::Error, "not stored");
  assert(exhausted.status == ErrorAllocation::Exhausted);
  assert(arena.view(exhausted.handle) == nullptr);
  assert(arena.same_identity(first.handle, forwarded));
  assert(arena.view(first.handle)->message() == "missing user");

  ErrorArena<1, 3> boundary;
  const auto exact = boundary.create(ErrorKind::Error, "abc");
  assert(exact.status == ErrorAllocation::Created);
  assert(boundary.view(exact.handle)->message() == "abc");
  // Storage reuse must not revive a handle from an expired invocation.
  std::optional<Arena> reused;
  reused.emplace();
  const auto stale = reused->create(ErrorKind::Error, "old").handle;
  const auto address = static_cast<const void*>(&*reused);
  reused.reset();
  reused.emplace();
  assert(static_cast<const void*>(&*reused) == address);
  const auto fresh = reused->create(ErrorKind::Error, "new").handle;
  assert(reused->view(stale) == nullptr);
  assert(!reused->same_identity(stale, fresh));
  assert(reused->view(fresh)->message() == "new");
  assert(reused->same_identity(fresh, fresh));
  return 0;
}
