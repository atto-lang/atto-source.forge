#pragma once
#include <cstdint>

namespace oresforge::shared {

// Compiler/runtime identity for one singleton declaration after module/import
// resolution. Guest code must never construct or supply these numeric IDs.
struct SingletonDeclarationId final {
  std::uint64_t module = 0;
  std::uint64_t declaration = 0;

  bool valid() const noexcept { return module != 0 && declaration != 0; }
  bool operator==(const SingletonDeclarationId&) const noexcept = default;
};

} // namespace oresforge::shared
