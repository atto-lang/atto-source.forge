#include "runtime/ActorStackDomain.hpp"
#include "runtime/ActorCheckedFrameBudget.hpp"
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
    // Two actors share this native process. A real guard page hit currently
    // kills the process, NOT just the failing actor. Do not report this as
    // an actor-only stack overflow containment test.
    ActorStackDomain sibling(778, ActorStackDomain::kMinStackBytes, 4096);
    sibling.run_turn([](auto& a) {
      int local=13;
      assert(a.contains_stack_address(&local));
    });
    ActorStackDomain tiny(777, ActorStackDomain::kMinStackBytes, 4096);
    tiny.run_turn([](auto&){exhaust_actor_stack(100);});
    ::_exit(0);
  }
  int status = 0;
  assert(::waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
  // Parent survives only because the overflow ran in a child PROCESS.
  // This does not establish sibling survival in a single runtime process.
  ActorStackDomain parent_survivor(779, ActorStackDomain::kMinStackBytes, 4096);
  parent_survivor.run_turn([](auto& a) {
    int local=17;
    assert(a.contains_stack_address(&local));
  });
  parent_survivor.stop();
}
#endif

static void verify_checked_frame_budget() {
  using oresforge::runtime::ActorCheckedFrameBudget;
  using oresforge::runtime::ActorFrameOverflowLimit;
  ActorCheckedFrameBudget fatal(64, 2);
  auto first = fatal.enter_frame(24);
  auto second = fatal.enter_frame(32);
  assert(fatal.active_frames() == 2 && fatal.used_bytes() == 56);
  must_fail([&] { fatal.leave_frame(first); });
  bool depth_exhausted = false;
  try { fatal.enter_frame(1); }
  catch (const ActorCheckedFrameBudget::Overflow& overflow) {
    depth_exhausted = overflow.limit() == ActorFrameOverflowLimit::FrameCount;
  }
  assert(depth_exhausted && fatal.terminal_overflow() ==
      ActorFrameOverflowLimit::FrameCount);
  fatal.leave_frame(second);
  fatal.leave_frame(first);
  assert(fatal.used_bytes() == 0 && fatal.active_frames() == 0);
  // Even a caught native exception cannot resume an already terminal actor.
  must_fail([&] { fatal.enter_frame(1); });

  ActorCheckedFrameBudget bytes(48, 4);
  auto owner = bytes.enter_frame(32);
  bool exceeded = false;
  try { bytes.enter_frame(17); }
  catch (const ActorCheckedFrameBudget::Overflow& overflow) {
    exceeded = overflow.limit() == ActorFrameOverflowLimit::ByteCapacity;
  }
  assert(exceeded && bytes.terminal_overflow() ==
      ActorFrameOverflowLimit::ByteCapacity);
  bytes.leave_frame(owner);
  ActorCheckedFrameBudget sibling(48, 2);
  auto token = sibling.enter_frame(16);
  must_fail([&] { bytes.leave_frame(token); });
  sibling.leave_frame(token);
  must_fail([&] { sibling.leave_frame(token); });
  auto refreshed = sibling.enter_frame(16);
  must_fail([&] { sibling.leave_frame(token); });
  sibling.leave_frame(refreshed);
  assert(!sibling.terminal_overflow());
  must_fail([] { ActorCheckedFrameBudget invalid(0, 1); });

  // A real actor on an independent native stack survives a sibling's
  // logical checked-frame terminal overflow. Physical SIGSEGV isolation is
  // still process-only and tested separately below.
  ActorStackDomain healthy(801, 128 * 1024, 4096);
  healthy.run_turn([&](ActorStackDomain& actor) {
    int local = 3;
    assert(actor.contains_stack_address(&local));
    auto frame = sibling.enter_frame(16);
    sibling.leave_frame(frame);
    assert(!sibling.terminal_overflow());
  });
  healthy.stop();
}

int main() {
  verify_checked_frame_budget();
  ActorStackDomain a(1, 128*1024, 4096), b(2, 128*1024, 4096);
  int host_local = 0;
  ActorHeapHandle a_handle, b_handle;
  std::uintptr_t a_addr=0, b_addr=0;
  must_fail([&]{ a.allocate(8); });
  a.run_turn([&](ActorStackDomain& actor){
    int local=41;
    a_addr=reinterpret_cast<std::uintptr_t>(&local);
    assert(actor.contains_stack_address(&local));
    assert(!b.contains_stack_address(&local));
    a_handle=actor.allocate(sizeof(local));
    actor.write(a_handle, 0, &local, sizeof(local));
    must_fail([&]{ b.allocate(1); });
    must_fail([&]{ actor.write(a_handle, 3, &local, 4); });
    must_fail([&]{ b.run_turn([](auto&){}); });
  });
  b.run_turn([&](ActorStackDomain& actor){
    int local=13;
    b_addr=reinterpret_cast<std::uintptr_t>(&local);
    assert(actor.contains_stack_address(&local));
    assert(!a.contains_stack_address(&local));
    b_handle=actor.allocate(sizeof(local));
    actor.write(b_handle, 0, &local, sizeof(local));
    must_fail([&]{ actor.read(a_handle,0,&local,sizeof(local)); });
  });
  assert(a_addr!=b_addr && a_addr!=reinterpret_cast<std::uintptr_t>(&host_local));
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
  // A language-level ordinary throw does not corrupt the sibling's stack or
  // heap: unlike an actual guard page overflow, this unwinds normally.
  b.run_turn([&](ActorStackDomain& actor) {
    int local=0;
    assert(actor.contains_stack_address(&local));
    actor.read(b_handle, 0, &local, sizeof(local));
    assert(local==13);
  });
  // An exception unwinds on the actor's own stack; later turns still work.
  a.run_turn([&](ActorStackDomain& actor){int n=1;assert(actor.contains_stack_address(&n));});
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
