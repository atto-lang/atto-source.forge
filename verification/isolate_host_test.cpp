#include "runtime/isolate.hpp"
#include <cassert>
#include <stdexcept>
#include <limits>
#include <atomic>
#include <thread>
using namespace oresforge::runtime;
template<class F>bool rejected(F&& f){try{f();}catch(const std::exception&){return true;}return false;}
static std::atomic<bool> entered{false}, can_exit{false};
static std::int64_t blocked_guest(std::int64_t old, std::int64_t delta){
  entered.store(true,std::memory_order_release);
  while(!can_exit.load(std::memory_order_acquire))std::this_thread::yield();
  return old+delta;
}
int main(){
 OresVM vm{2};OresVM::TenantVMHandle a=vm.create_tenant_vm("tenant-a",1),b=vm.create_isolate("tenant-b",1);
 assert(rejected([&]{vm.create_tenant_vm(".",1);}));
 assert(rejected([&]{vm.create_tenant_vm("..",1);}));
 assert(rejected([&]{vm.create_isolate("bad\nnamespace",1);}));
 assert(rejected([&]{vm.create_isolate("bad\ttenant",1);}));
 assert(rejected([&]{vm.create_isolate("non\x7fprint",1);}));
 assert(rejected([&]{vm.create_isolate("spaces invalid",1);}));
 assert(rejected([&]{vm.create_isolate("tenant-c",1);}));
 vm.start(a);vm.start(b);
 auto actor=vm.register_actor(a,7);auto other=vm.register_actor(b,7);
 auto pin=vm.pin_operation(a,actor);
 assert(rejected([&]{vm.retire_actor(a,actor);}));
 assert(rejected([&]{vm.retire_operation(b,other,*pin);}));
 vm.begin_shutdown(a);assert(rejected([&]{vm.complete_shutdown(a);}));
 assert(rejected([&]{vm.pin_operation(a,actor);}));
 vm.retire_operation(a,actor,*pin);
 assert(rejected([&]{vm.retire_operation(a,actor,*pin);}));
 vm.retire_actor(a,actor);vm.complete_shutdown(a);vm.destroy(a);
 assert(rejected([&]{vm.snapshot(a);}));
 auto anew=vm.create_isolate("tenant-a",1);vm.start(anew);
 assert(rejected([&]{vm.retire_actor(anew,actor);}));
 vm.begin_shutdown(anew);vm.complete_shutdown(anew);vm.destroy(anew);
 vm.retire_actor(b,other);vm.begin_shutdown(b);vm.complete_shutdown(b);vm.destroy(b);
 // Shutdown must wait for a physically running native invocation, not only
 // empty actor and I/O resource registries.
 OresVM nativeVm{1};auto native=nativeVm.create_isolate("native",1);nativeVm.start(native);
 std::thread worker([&]{assert(nativeVm.invoke_trusted_scalar(native,blocked_guest,7)==7);});
 while(!entered.load(std::memory_order_acquire))std::this_thread::yield();
 nativeVm.begin_shutdown(native);
 assert(nativeVm.snapshot(native).active_guest_calls==1);
 assert(rejected([&]{nativeVm.complete_shutdown(native);}));
 can_exit.store(true,std::memory_order_release);
 worker.join();
 assert(nativeVm.snapshot(native).scalar_state==7);
 nativeVm.complete_shutdown(native);nativeVm.destroy(native);
 OresVM overflowVm{1};auto ov=overflowVm.create_isolate("ov",1);overflowVm.start(ov);
 auto plus=[](std::int64_t lhs,std::int64_t rhs){return lhs+rhs;};
 assert(overflowVm.invoke_trusted_scalar(ov,plus,std::numeric_limits<std::int64_t>::max())==std::numeric_limits<std::int64_t>::max());
 assert(rejected([&]{overflowVm.invoke_trusted_scalar(ov,plus,1);}));
 assert(overflowVm.snapshot(ov).scalar_state==std::numeric_limits<std::int64_t>::max());
 assert(overflowVm.snapshot(ov).active_guest_calls==0);
 overflowVm.begin_shutdown(ov);overflowVm.complete_shutdown(ov);overflowVm.destroy(ov);
}
