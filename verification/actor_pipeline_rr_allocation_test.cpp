// An allocation failure while copying a selected native host route must not
// consume the preallocated round-robin cursor. Does not certify guest actors.
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
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

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

  // If forward_round_robin advanced before the edge copy, this first choice
  // would incorrectly be 'second' even though the prior call never returned.
  if (policy.forward_round_robin(root).target != first ||
      policy.forward_round_robin(root).target != second ||
      policy.forward_round_robin(root).target != first)
    throw std::runtime_error("failed route selection consumed RR credit");
  policy.close();
  std::cout << "ACTOR_PIPELINE_RR_ALLOCATION_ATOMIC_PASS\n";
}
