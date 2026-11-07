#include "runtime/ActorCooperativeScheduler.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace oresforge::runtime;

template<class F> void rejected(F&& f) {
  bool failed = false;
  try { f(); } catch(const std::exception&) { failed = true; }
  assert(failed);
}
int main() {
  ActorCooperativeScheduler s(2);
  std::vector<int> order;
  ActorHeapHandle a_slot, b_slot;
  const auto a = s.spawn(1,128*1024,4096,
    [&](ActorStackDomain& d,std::span<const std::uint8_t> data,
        std::uint64_t& processed) {
      assert(d.actor_id()==1);
#if !ORESFORGE_ACTOR_ASAN
      assert(d.contains_stack_address(&processed));
#endif
      assert(data.size()==1);
      assert(processed<=1);
      order.push_back(100+data[0]);
      if (processed==0) {
        a_slot=d.allocate(sizeof(std::uint64_t));
        const std::uint64_t value=41;
        d.write(a_slot,0,&value,sizeof(value));
        rejected([&]{s.run_one();});
      } else {
        std::uint64_t value=0;
        d.read(a_slot,0,&value,sizeof(value));
        assert(value==41);
        rejected([&]{d.read(b_slot,0,&value,sizeof(value));});
      }
    },2,4);
  const auto b = s.spawn(2,128*1024,4096,
    [&](ActorStackDomain& d,std::span<const std::uint8_t> data,
        std::uint64_t& processed) {
      assert(d.actor_id()==2);
#if !ORESFORGE_ACTOR_ASAN
      assert(d.contains_stack_address(&processed));
#endif
      assert(processed==0);
      order.push_back(200+data[0]);
      b_slot=d.allocate(1);
      std::uint8_t value=17;
      d.write(b_slot,0,&value,1);
      rejected([&]{d.write(a_slot,0,&value,1);});
    });
  const std::array<std::uint8_t,1> one{1}, two{2}, three{3};
  s.send(a,one);s.send(a,two);s.send(b,three);
  assert(s.queued_turns()==2);
  assert(s.run_ready(1)==1);
  assert(s.queued_turns()==2);
  assert(s.run_ready(1)==1);
  assert(s.run_ready(1)==1);
  assert((order==std::vector<int>{101,203,102}));
  assert(s.run_ready(1)==0);
  s.stop_actor(1);
  rejected([&]{s.send(a,one);});
  assert(s.run_ready(1)==1);
  assert(s.live_actors()==1);
  rejected([&]{s.spawn(1,128*1024,4096,[](auto&,auto,auto&){});});
  s.stop_all();
  assert(s.live_actors()==0);
  rejected([&]{s.send(b,one);});
  rejected([&]{s.run_ready(0);});
  std::thread foreign([&]{rejected([&]{s.run_ready(1);});});
  foreign.join();

  // Stackful suspension preserves stack-local RAII objects, and cancellation
  // unwinds them before any native stack can be scrubbed and unmapped.
  ActorStackDomain task(900,128*1024,2048);
  int cleanup=0;
  auto state=task.start_task([&](ActorStackDomain& d) {
    struct Guard {int& n; ~Guard() {++n;}} guard{cleanup};
    std::uint64_t local=55;
#if !ORESFORGE_ACTOR_ASAN
    assert(d.contains_stack_address(&local));
#endif
    d.yield();
    assert(local==55);
  });
  assert(state==ActorStackDomain::TaskState::Suspended);
  rejected([&]{task.stop();});
  task.request_cancel();
  bool cancelled=false;
  try { (void)task.resume_task(); }
  catch(const ActorStackDomain::Cancelled&) {cancelled=true;}
  assert(cancelled && cleanup==1);
  assert(task.task_state()==ActorStackDomain::TaskState::Finished);
  task.stop();

  // Direct turn callbacks cannot misuse the suspension-only entrypoint.
  ActorStackDomain one_shot(901,128*1024,1024);
  one_shot.run_turn([&](auto& d){rejected([&]{d.yield();});});
  one_shot.stop();

  // Bounded mailbox admission, invalid actor budgets, and stopped ingress.
  ActorCooperativeScheduler tiny(1);
  const auto full=tiny.spawn(3,128*1024,2048,[](auto&,auto,auto&) {},1,1);
  tiny.send(full,one);
  rejected([&]{tiny.send(full,two);});
  rejected([&]{tiny.spawn(4,128*1024,2048,[](auto&,auto,auto&) {});});
  assert(tiny.run_ready(1)==1);
  tiny.stop_all();
  rejected([&]{tiny.send(full,one);});

  // Authority is opaque: matching actor ids from another scheduler do not
  // confer access to a different mailbox incarnation.
  ActorCooperativeScheduler foreign_scheduler(1);
  const auto separate=foreign_scheduler.spawn(2,128*1024,2048,
    [](auto&,auto,auto&){});
  rejected([&]{foreign_scheduler.send(b,one);});
  foreign_scheduler.send(separate,one);
  assert(foreign_scheduler.run_ready(1)==1);
  foreign_scheduler.stop_all();

  // Handler fault seals the actor; retained mail is erased and no turn may
  // be resumed after stack retirement.
  ActorCooperativeScheduler faults(2);
  auto bad=faults.spawn(71,128*1024,2048,
    [](auto&,auto,auto&)->void{throw std::runtime_error("guest callback fault");});
  faults.send(bad,one);
  rejected([&]{faults.run_ready(1);});
  assert(faults.live_actors()==0);
  rejected([&]{faults.send(bad,one);});

  // STOP before first scheduling never executes a user's callback.
  int never_called=0;
  auto stopped=faults.spawn(72,128*1024,2048,
    [&](auto&,auto,auto&){++never_called;});
  faults.send(stopped,one);
  faults.stop_actor(72);
  assert(never_called==0);
  assert(faults.queued_turns()==0);
  assert(faults.run_ready(1)==0);
  faults.stop_all();

  rejected([&]{ActorCooperativeScheduler invalid(0);});
  rejected([&]{ActorCooperativeScheduler invalid(65537);});
  return 0;
}
