#pragma once
// AttoLang hybrid actor-memory ABI v1: offset8+1 reference *word* only.
// This header does not mint an ActorRef, native pointer, lease or authority.
// Forge's existing actor-private host arena remains independently checked,
// and compiler-produced guest object lowering is NOT certified by this codec.
#include <cstdint>
#include <limits>

namespace oresforge::runtime {
inline constexpr std::uint64_t ref32_max_logical_start_v1 =
    (static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) - 1u) * 8u;

[[nodiscard]] constexpr bool encode_ref32_offset_v1(
    std::uint64_t aligned_offset, std::uint32_t& out_word) noexcept {
  if (aligned_offset > ref32_max_logical_start_v1 ||
      (aligned_offset % 8u) != 0u) return false;
  out_word = static_cast<std::uint32_t>(aligned_offset / 8u + 1u);
  return true;
}
[[nodiscard]] constexpr bool decode_ref32_word_v1(
    std::uint32_t word, std::uint64_t& out_aligned_offset) noexcept {
  if (!word) return false; // Null must never denote an object start.
  out_aligned_offset = (static_cast<std::uint64_t>(word) - 1u) * 8u;
  return true;
}
} // namespace oresforge::runtime
