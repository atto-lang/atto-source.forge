#pragma once
// AttoLang CONTROL-plane storage ABI, host-only. NOT an ActorRef, actor-local
// offset, native pointer, issuer credential, serializable reference, or GC proof.
// The trusted runtime must independently authenticate issuer, epoch, lifetime,
// slot liveness, type, permissions, and bounds before any dereference.
#include <cstdint>
#include <string_view>

namespace oresforge::runtime {
inline constexpr std::string_view control_ref32_storage_abi_v1 =
    "control_generation12_slot20_v1";
inline constexpr std::uint32_t control_ref32_slot_bits_v1 = 20;
inline constexpr std::uint32_t control_ref32_generation_bits_v1 = 12;
inline constexpr std::uint32_t control_ref32_slot_mask_v1 = (1u << 20) - 1u;
inline constexpr std::uint32_t control_ref32_max_generation_v1 = (1u << 12) - 1u;

[[nodiscard]] constexpr bool require_control_ref32_abi_v1(
    std::string_view requested) noexcept {
  return requested == control_ref32_storage_abi_v1;
}

[[nodiscard]] constexpr bool encode_control_ref32_v1(
    std::uint32_t slot_index, std::uint32_t generation,
    std::uint32_t& out_word) noexcept {
  if (slot_index >= control_ref32_slot_mask_v1 ||
      generation == 0u || generation > control_ref32_max_generation_v1) return false;
  out_word = (generation << control_ref32_slot_bits_v1) | (slot_index + 1u);
  return true;
}

// Zero is null and therefore invalid as an object reference. A successful
// decode proves only a well-formed storage word, NEVER its authority/liveness.
[[nodiscard]] constexpr bool decode_control_ref32_v1(
    std::uint32_t word, std::uint32_t& out_slot_index,
    std::uint32_t& out_generation) noexcept {
  const std::uint32_t low = word & control_ref32_slot_mask_v1;
  const std::uint32_t generation = word >> control_ref32_slot_bits_v1;
  if (low == 0u || generation == 0u) return false;
  out_slot_index = low - 1u;
  out_generation = generation;
  return true;
}
} // namespace oresforge::runtime
