#include "runtime/ActorRef32CodecV1.hpp"
#include <cassert>
#include <initializer_list>
#include <cstdint>
#include <limits>

int main() {
  using oresforge::runtime::encode_ref32_offset_v1;
  using oresforge::runtime::decode_ref32_word_v1;
  using oresforge::runtime::ref32_max_logical_start_v1;
  static_assert(ref32_max_logical_start_v1 == UINT64_C(34359738352));
  std::uint32_t word = 0;
  std::uint64_t offset = 0;
  assert(!decode_ref32_word_v1(0, offset));
  assert(encode_ref32_offset_v1(0, word) && word == 1);
  for (const std::uint32_t candidate : {
      UINT32_C(1), UINT32_C(2), UINT32_C(0x7fffffff),
      UINT32_C(0x80000000), UINT32_C(0xfffffffe), UINT32_MAX}) {
    assert(decode_ref32_word_v1(candidate, offset));
    assert(encode_ref32_offset_v1(offset, word));
    assert(word == candidate);
  }
  assert(offset == UINT64_C(34359738352));
  assert(!encode_ref32_offset_v1(ref32_max_logical_start_v1 + 8u, word));
  assert(!encode_ref32_offset_v1(ref32_max_logical_start_v1 - 1u, word));
  assert(!encode_ref32_offset_v1(UINT64_MAX, word));
  // A local Ref32 word is not an actor identity, lease or network capability.
  return 0;
}
