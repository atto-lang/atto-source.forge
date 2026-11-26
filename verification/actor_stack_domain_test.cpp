#include "runtime/ActorStackDomain.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <signal.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stdexcept>
#include <thread>
#include <vector>
using oresforge::runtime::ActorHeapHandle;
using oresforge::runtime::ActorStackDomain;

template<class F> void must_fail(F&& f) {
  bool failed=false;
  try { f(); } catch (const std::exception&) { failed=true; }
  assert(failed);
}

#if !ORESFORGE_ACTOR_ASAN
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void exhaust_actor_stack(unsigned depth) {
  volatile char padding[4096] = {};
  padding[depth % sizeof(padding)] = 1;
  if (depth) exhaust_actor_stack(depth - 1);
  if (padding[depth % sizeof(padding)] != 1) std::abort();
}
void verify_guard_page_fault() {
  const pid_t child = ::fork();
  assert(child >= 0);
  if (child == 0) {
    struct rlimit no_core{0,0};
    ::setrlimit(RLIMIT_CORE, &no_core);
    ActorStackDomain tiny(777, ActorStackDomain::kMinStackBytes, 4096);
    tiny.run_turn([](auto&){exhaust_actor_stack(100);});
    ::_exit(0);
  }
  int status = 0;
  assert(::waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
}
#endif
int main() {
  // Exercise the production reservation helper at exhaustion without
  // modifying the process-global allocator.
  const auto max=std::numeric_limits<std::uint64_t>::max();
  std::atomic<std::uint64_t> next{max-32};
  std::atomic<unsigned> count{0};
  std::atomic<std::uint64_t> bits{0};
  std::vector<std::thread> claimers;
  for(unsigned i=0;i<8;++i) claimers.emplace_back([&]{
    for(;;) {
      try {
        const auto id=oresforge::runtime::detail::reserve_stack_incarnation(next);
        assert(id>=max-32 && id<max);
        const auto bit=std::uint64_t{1}<<(id-(max-32));
        assert((bits.fetch_or(bit)&bit)==0);
        ++count;
      } catch(const std::overflow_error&) {break;}
    }
  });
  for(auto& t:claimers)t.join();
  assert(count==32 && bits==0xffffffffULL && next==max);
  for(unsigned i=0;i<128;++i)
    must_fail([&]{(void)oresforge::runtime::detail::reserve_stack_incarnation(next);});
  assert(next==max);
  std::atomic<std::uint64_t> invalid{0};
  must_fail([&]{(void)oresforge::runtime::detail::reserve_stack_incarnation(invalid);});
  assert(invalid==0);
  ActorStackDomain a(1, 128*1024, 4096), b(2, 128*1024, 4096);
  int host_local = 0;
  ActorHeapHandle a_handle, b_handle;
  std::uintptr_t a_addr=0, b_addr=0;
  must_fail([&]{ a.allocate(8); });
  a.run_turn([&](ActorStackDomain& actor){
    int local=41;
    a_addr=reinterpret_cast<std::uintptr_t>(&local);
#if !ORESFORGE_ACTOR_ASAN
    assert(actor.contains_stack_address(&local));
    assert(!b.contains_stack_address(&local));
#endif
    a_handle=actor.allocate(sizeof(local));
    actor.write(a_handle, 0, &local, sizeof(local));
    must_fail([&]{ b.allocate(1); });
    must_fail([&]{ actor.write(a_handle, 3, &local, 4); });
    must_fail([&]{ b.run_turn([](auto&){}); });
  });
  b.run_turn([&](ActorStackDomain& actor){
    int local=13;
    b_addr=reinterpret_cast<std::uintptr_t>(&local);
#if !ORESFORGE_ACTOR_ASAN
    assert(actor.contains_stack_address(&local));
    assert(!a.contains_stack_address(&local));
#endif
    b_handle=actor.allocate(sizeof(local));
    actor.write(b_handle, 0, &local, sizeof(local));
    must_fail([&]{ actor.read(a_handle,0,&local,sizeof(local)); });
  });
#if !ORESFORGE_ACTOR_ASAN
  assert(a_addr!=b_addr && a_addr!=reinterpret_cast<std::uintptr_t>(&host_local));
#else
  // ASan may reuse fake-stack addresses across non-overlapping turns.
  // Physical-address isolation is asserted by the unsanitized gate;
  // authority, bounds, cancellation and incarnation assertions remain live.
  (void)a_addr; (void)b_addr; (void)host_local;
#endif
  a.run_turn([&](ActorStackDomain& actor){
    int result=0;
    actor.read(a_handle,0,&result,sizeof(result));
    assert(result==41);
    must_fail([&]{ actor.read(b_handle,0,&result,sizeof(result)); });
    actor.release(a_handle);
    must_fail([&]{ actor.read(a_handle,0,&result,sizeof(result)); });
    must_fail([&]{ actor.allocate(5000); });
  });
  must_fail([&]{ a.run_turn([](auto&){throw std::runtime_error("actor failure");}); });
  // An exception unwinds on the actor's own stack; later turns still work.
  a.run_turn([&](ActorStackDomain& actor){
    int n=1;
#if !ORESFORGE_ACTOR_ASAN
    assert(actor.contains_stack_address(&n));
#else
    (void)actor; (void)n; // ASan may substitute a fake stack address.
#endif
  });
  std::thread outsider([&]{must_fail([&]{a.run_turn([](auto&){});});});
  outsider.join();
  b.stop();b.stop();
  must_fail([&]{b.run_turn([](auto&){});});
  must_fail([&]{ActorStackDomain bad(0,128*1024,4096);});
  must_fail([&]{ActorStackDomain bad(3,16*1024,4096);});
  must_fail([&]{ActorStackDomain bad(3,128*1024,0);});
#if !ORESFORGE_ACTOR_ASAN
  verify_guard_page_fault();
#endif
  return 0;
}
