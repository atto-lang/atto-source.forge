#include "runtime/ControlRef32CodecV1.hpp"
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); return 1; } } while (false)
int main() {
  using namespace oresforge::runtime;
  static_assert(control_ref32_slot_bits_v1 + control_ref32_generation_bits_v1 == 32);
  static_assert(control_ref32_slot_mask_v1 == 1048575u);
  static_assert(control_ref32_max_generation_v1 == 4095u);
  CHECK(require_control_ref32_abi_v1("control_generation12_slot20_v1"));
  for (const auto invalid : {"control_generation20_slot12_v1", "generation16_slot16",
       "generation20_slot12", "offset8_plus_one", ""}) {
    CHECK(!require_control_ref32_abi_v1(invalid));
  }
  std::uint32_t word = 0, index = 0, generation = 0;
  for (const auto value : {UINT32_C(0), UINT32_C(1), UINT32_C(0x00100000),
                           UINT32_C(0x80000000), UINT32_C(0xffffffff)}) {
    const bool accepted = decode_control_ref32_v1(value, index, generation);
    CHECK(accepted == (value == UINT32_C(0xffffffff)));
  }
  for (const auto v : {UINT32_C(1), UINT32_C(2048), UINT32_C(4095)}) {
    CHECK(encode_control_ref32_v1(0u, v, word));
    CHECK(word == (v << 20u) + 1u);
    CHECK(decode_control_ref32_v1(word, index, generation));
    CHECK(index == 0u && generation == v);
  }
  CHECK(encode_control_ref32_v1(1048574u, 4095u, word) && word == UINT32_MAX);
  CHECK(decode_control_ref32_v1(word, index, generation));
  CHECK(index == 1048574u && generation == 4095u);
  CHECK(!encode_control_ref32_v1(1048575u, 1u, word));
  CHECK(!encode_control_ref32_v1(UINT32_MAX, 1u, word));
  CHECK(!encode_control_ref32_v1(0u, 0u, word));
  CHECK(!encode_control_ref32_v1(0u, 4096u, word));
  CHECK(!encode_control_ref32_v1(0u, UINT32_MAX, word));
  // A matching numeric word across domains is NOT a capability or valid issuer.
  CHECK(encode_control_ref32_v1(0u, 1u, word));
  const std::uint32_t foreign_domain_word = word;
  CHECK(word == foreign_domain_word);
  std::puts("PASS forge control Ref32 C++20 host-only layout/ABI boundary vectors; no guest/scheduler/GC proof");
  return 0;
}
