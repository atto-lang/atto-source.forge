#include "runtime/isolate.hpp"
#include <cassert>
#include <cstdint>
#include <stdexcept>

// Linked from the ACTUAL Forge emit-runtime-asm output. The callable x86-64
// guest has exactly two signed i64 params and no mutable process-global data.
extern "C" std::int64_t oresforge_entry(std::int64_t, std::int64_t);

template<class F> bool rejects(F&& fn) {
  try {fn();} catch(const std::exception&) {return true;} return false;
}
int main() {
  oresforge::runtime::OresVM vm{2};
  auto a=vm.create_isolate("tenant-a",1);
  auto b=vm.create_isolate("tenant-b",1);
  vm.start(a);vm.start(b);
  assert(vm.invoke_trusted_scalar(a,oresforge_entry,-1)==-1);
  assert(vm.invoke_trusted_scalar(b,oresforge_entry,100)==100);
  assert(vm.invoke_trusted_scalar(a,oresforge_entry,-2)==-3);
  assert(vm.invoke_trusted_scalar(b,oresforge_entry,2)==102);
  assert(vm.snapshot(a).scalar_state==-3);
  assert(vm.snapshot(b).scalar_state==102);
  vm.begin_shutdown(a);
  assert(rejects([&]{vm.invoke_trusted_scalar(a,oresforge_entry,1);}));
  assert(vm.invoke_trusted_scalar(b,oresforge_entry,1)==103);
  vm.complete_shutdown(a);vm.destroy(a);
  assert(rejects([&]{vm.snapshot(a);}));
  auto again=vm.create_isolate("tenant-a",1);
  vm.start(again);
  assert(vm.invoke_trusted_scalar(again,oresforge_entry,8)==8);
  assert(rejects([&]{vm.snapshot(a);}));
  vm.begin_shutdown(again);vm.complete_shutdown(again);vm.destroy(again);
  vm.begin_shutdown(b);vm.complete_shutdown(b);vm.destroy(b);
}
