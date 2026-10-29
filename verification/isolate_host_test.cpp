#include "runtime/isolate.hpp"
#include <cassert>
#include <stdexcept>
using namespace oresforge::runtime;
template<class F>bool rejected(F&& f){try{f();}catch(const std::exception&){return true;}return false;}
int main(){
 OresVM vm{2};auto a=vm.create_isolate("tenant-a",1),b=vm.create_isolate("tenant-b",1);
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
}
