#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
cc="${CC:-gcc}"
cxx="${CXX:-g++}"
mode="${1:-optimized}"
flags=(-O2 -DNDEBUG)
wrap=(-DORES_ARENA_TEST_ALLOCATIONS=1 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free)
case "$mode" in
  optimized) ;;
  sanitized) flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer); wrap=() ;;
  *) echo 'expected optimized or sanitized' >&2; exit 64 ;;
esac
common=(-Wall -Wextra -Wpedantic -Werror -I"$root" -I"$root/runtime/arena")
"$cc" -std=c11 "${common[@]}" "${flags[@]}" -c "$root/runtime/arena/ActorArena.c" -o "$work/arena.o"
"$cxx" -std=c++20 "${common[@]}" "${flags[@]}" "$root/verification/actor_arena.cpp" "$work/arena.o" -pthread "${wrap[@]}" -o "$work/arena"
"$work/arena"
"$cc" -std=c11 "${common[@]}" "${flags[@]}" -c "$root/runtime/arena/ActorOwnedI64Heap.c" -o "$work/heap.o"
"$cxx" -std=c++20 "${common[@]}" "${flags[@]}" "$root/verification/scalar_heap_safety.cpp" "$work/heap.o" -pthread "${wrap[@]}" -o "$work/heap"
"$work/heap"
"$cxx" -std=c++20 "${common[@]}" "${flags[@]}" "$root/runtime/scalar_actor.cpp" "$root/verification/scalar_actor.cpp" "$work/heap.o" -pthread "${wrap[@]}" -o "$work/actor"
# New actor-owned logical frame storage, including a real scalar actor
# callback bridge; both modes use the exact source-local C++ implementation.
"$cxx" -std=c++20 "${common[@]}" "${flags[@]}" \
  "$root/verification/actor_frames.cpp" -pthread -o "$work/frames"
"$work/frames"
"$cxx" -std=c++20 "${common[@]}" "${flags[@]}" \
  "$root/verification/scalar_actor_frames.cpp" "$root/runtime/scalar_actor.cpp" \
  "$work/heap.o" -pthread -o "$work/actor-frames"
"$work/actor-frames"
"$work/actor"
"$cxx" -std=c++20 "${common[@]}" "${flags[@]}" "$root/verification/native_memory_model_test.cpp" \
  "$work/arena.o" "$work/heap.o" -pthread -o "$work/memory-model"
"$cxx" -std=c++20 "${common[@]}" "${flags[@]}" -DORES_PARITY_FORGE \
  "$root/verification/native_actor_model_test.cpp" "$root/runtime/scalar_actor.cpp" "$work/heap.o" \
  -pthread -o "$work/actor-model"
python3 "$root/scripts/test_native_memory_parity.py" --runner "$work/memory-model" --actor-runner "$work/actor-model"
