#include "runtime/isolate.hpp"
#include <cassert>
#include <stdexcept>
using namespace oresforge::runtime;
template<class F> static void denies(F f) {
  bool rejected=false;try{f();}catch(const std::logic_error&){rejected=true;}
  assert(rejected);
}
int main(){
  OresVM vm;
  auto a=vm.create_isolate("tenant-a",2);
  auto b=vm.create_isolate("tenant-b",1);
  vm.start(a);vm.start(b);
  auto actor1=vm.register_actor(a,1);
  auto actor2=vm.register_actor(a,2);
  auto actorB=vm.register_actor(b,1);
  auto x=vm.enqueue_durable(a,actor1,7,1,3);
  auto y=vm.enqueue_durable(a,actor1,7,2,3);
  auto z=vm.enqueue_durable(a,actor1,9,3,3);
  auto foreign=vm.enqueue_durable(b,actorB,7,1,3);
  assert(x.lock_head&&!y.lock_head&&z.lock_head&&foreign.lock_head);
  denies([&]{vm.claim_durable(a,foreign.ticket);});
  denies([&]{vm.retire_actor(a,actor1);});
  assert(vm.claim_durable(a,x.ticket));
  assert(!vm.claim_durable(a,z.ticket));
  denies([&]{vm.finish_durable(a,y.ticket);});
  assert(vm.finish_durable(a,x.ticket)==y.ticket);
  assert(vm.claim_durable(a,z.ticket));
  assert(!vm.claim_durable(a,y.ticket));
  vm.finish_durable(a,z.ticket);
  assert(vm.claim_durable(a,y.ticket));
  vm.finish_durable(a,y.ticket);
  auto last=vm.enqueue_durable(a,actor2,7,4,1);
  vm.begin_shutdown(a);
  denies([&]{vm.claim_durable(a,last.ticket);});
  denies([&]{vm.complete_shutdown(a);});
  vm.cancel_durable_head(a,last.ticket);
  vm.retire_actor(a,actor1);vm.retire_actor(a,actor2);
  vm.complete_shutdown(a);vm.destroy(a);
  denies([&]{vm.enqueue_durable(a,actor1,7,5,1);});
  assert(vm.claim_durable(b,foreign.ticket));
  vm.finish_durable(b,foreign.ticket);
  vm.retire_actor(b,actorB);
  vm.begin_shutdown(b);vm.complete_shutdown(b);vm.destroy(b);
}
