// An allocation failure after selecting a native host route still consumes
// that preallocated round-robin turn. Does not certify guest actors.
#include "runtime/actor_pipeline_policy.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

static std::atomic<bool> reject_allocation{false};

void* operator new(std::size_t size) {
  if (reject_allocation.load(std::memory_order_relaxed)) throw std::bad_alloc();
  if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
  throw std::bad_alloc();
}
// Avoid GCC's mismatched-new-delete false positive for this deliberate
// malloc-backed allocation fault injector; the hook owns both delete forms.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static void release_allocation(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory) noexcept { release_allocation(memory); }
void operator delete(void* memory, std::size_t) noexcept { release_allocation(memory); }

int main() {
  using oresforge::runtime::ActorPipePolicy;
  using oresforge::runtime::PipeEdge;
  using oresforge::runtime::PipeStage;
  const std::string root(192, 'r');
  const std::string first(192, 'a');
  const std::string second(192, 'b');
  const std::string payload(192, 'p');
  ActorPipePolicy policy(
      std::vector<PipeStage>{{root, "Producer", "readable", "", payload},
                             {first, "Worker", "writable", payload, ""},
                             {second, "Worker", "writable", payload, ""}},
      std::vector<PipeEdge>{{root, first, payload}, {root, second, payload}});
  bool allocation_rejected = false;
  reject_allocation.store(true, std::memory_order_relaxed);
  try {
    (void)policy.forward_round_robin(root);
  } catch (const std::bad_alloc&) {
    allocation_rejected = true;
  }
  reject_allocation.store(false, std::memory_order_relaxed);
  if (!allocation_rejected)
    throw std::runtime_error("allocation-failure fixture did not trigger");

  // The failed copy happened after selecting the first edge, so routing-v1
  // requires that attempted turn to remain consumed.
  if (policy.forward_round_robin(root).target != second ||
      policy.forward_round_robin(root).target != first ||
      policy.forward_round_robin(root).target != second)
    throw std::runtime_error("failed route selection did not consume RR turn");
  policy.close();
  std::cout << "ACTOR_PIPELINE_RR_ALLOCATION_ATOMIC_PASS\n";
}
