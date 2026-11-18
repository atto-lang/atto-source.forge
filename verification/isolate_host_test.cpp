#include "runtime/isolate.hpp"
#include <cassert>
#include <stdexcept>
#include <limits>
#include <atomic>
#include <thread>
#include <array>
#include <vector>
using namespace oresforge::runtime;
template<class F>bool rejected(F&& f){try{f();}catch(const std::exception&){return true;}return false;}
static std::atomic<bool> entered{false}, can_exit{false};
static std::int64_t blocked_guest(std::int64_t old, std::int64_t delta){
  entered.store(true,std::memory_order_release);
  while(!can_exit.load(std::memory_order_acquire))std::this_thread::yield();
  return old+delta;
}
int main(){
 // No compiler-enforced native memory sandbox is wired here yet.
 // Admission MUST refuse protected mode, not silently downgrade to trusted.
 OresVM deny_vm{1};
 assert(rejected([&]{deny_vm.create_isolate("hostile",1,TenantIsolationMode::ProtectedNative);}));
 assert(rejected([&]{deny_vm.create_isolate("hostile",1,static_cast<TenantIsolationMode>(255));}));
 auto admitted=deny_vm.create_isolate("hostile",1,TenantIsolationMode::TrustedNative);
 deny_vm.start(admitted);
 deny_vm.begin_shutdown(admitted);
 deny_vm.complete_shutdown(admitted);
 deny_vm.destroy(admitted);
 OresVM vm{2};auto a=vm.create_isolate("tenant-a",1),b=vm.create_isolate("tenant-b",1);
 assert(rejected([&]{vm.create_isolate("bad\nnamespace",1);}));
 assert(rejected([&]{vm.create_isolate("bad\ttenant",1);}));
 assert(rejected([&]{vm.create_isolate("non\x7fprint",1);}));
 assert(rejected([&]{vm.create_isolate("spaces invalid",1);}));
 assert(rejected([&]{vm.create_isolate("tenant-c",1);}));
 vm.start(a);vm.start(b);
 auto actor=vm.register_actor(a,7);auto other=vm.register_actor(b,7);
 // Real OresVM actor-owned arena: same actor ID in another tenant gets
 // independent physical bytes. Neither a foreign ticket nor the wrong tenant
 // handle can authorize a cross-tenant operation.
 const std::array<std::uint8_t,3> p{42,7,1};
 vm.write_actor_memory(a,actor,10,p);
 assert((vm.read_actor_memory(a,actor,10,3)==std::vector<std::uint8_t>{42,7,1}));
 assert((vm.read_actor_memory(b,other,10,3)==std::vector<std::uint8_t>{0,0,0}));
 assert(rejected([&]{vm.read_actor_memory(a,other,10,3);}));
 assert(rejected([&]{vm.write_actor_memory(b,actor,10,p);}));
 assert(rejected([&]{vm.write_actor_memory(a,actor,4095,p);}));
 assert(rejected([&]{vm.read_actor_memory(a,actor,UINT32_MAX,1);}));
 auto pin=vm.pin_operation(a,actor);
 assert(rejected([&]{vm.retire_actor(a,actor);}));
 assert(rejected([&]{vm.retire_operation(b,other,*pin);}));
 vm.begin_shutdown(a);assert(rejected([&]{vm.complete_shutdown(a);}));
 assert(rejected([&]{vm.read_actor_memory(a,actor,10,3);}));
 assert(rejected([&]{vm.write_actor_memory(a,actor,10,p);}));
 assert((vm.read_actor_memory(b,other,10,3)==std::vector<std::uint8_t>{0,0,0}));
 assert(rejected([&]{vm.pin_operation(a,actor);}));
 vm.retire_operation(a,actor,*pin);
 assert(rejected([&]{vm.retire_operation(a,actor,*pin);}));
 vm.retire_actor(a,actor);vm.complete_shutdown(a);vm.destroy(a);
 assert(rejected([&]{vm.snapshot(a);}));
 auto anew=vm.create_isolate("tenant-a",1);vm.start(anew);
 auto new_actor=vm.register_actor(anew,7);
 assert((vm.read_actor_memory(anew,new_actor,10,3)==std::vector<std::uint8_t>{0,0,0}));
 assert(rejected([&]{vm.read_actor_memory(anew,actor,10,3);}));
 vm.retire_actor(anew,new_actor);
 assert(rejected([&]{vm.retire_actor(anew,actor);}));
 vm.begin_shutdown(anew);vm.complete_shutdown(anew);vm.destroy(anew);
 vm.retire_actor(b,other);vm.begin_shutdown(b);vm.complete_shutdown(b);vm.destroy(b);
 // Process aggregate actor memory budget is enforced across tenants,
 // not just per-tenant or per-mailbox quotas. Failed registration must not
 // publish an actor ticket or consume the actor epoch/quota.
 OresVM budget_vm{2,4096};
 auto ma=budget_vm.create_isolate("ma",1),mb=budget_vm.create_isolate("mb",1);
 budget_vm.start(ma);budget_vm.start(mb);
 auto m_actor=budget_vm.register_actor(ma,11);
 assert(rejected([&]{budget_vm.register_actor(mb,11);}));
 budget_vm.retire_actor(ma,m_actor);
 auto m_other=budget_vm.register_actor(mb,11);
 budget_vm.retire_actor(mb,m_other);
 budget_vm.begin_shutdown(ma);budget_vm.complete_shutdown(ma);budget_vm.destroy(ma);
 budget_vm.begin_shutdown(mb);budget_vm.complete_shutdown(mb);budget_vm.destroy(mb);
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
